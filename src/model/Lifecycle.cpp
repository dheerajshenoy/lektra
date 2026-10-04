#include "Model.hpp"

#include "BrowseLinkItem.hpp"
#include "Commands/TextHighlightAnnotationCommand.hpp"
#include "Config.hpp"
#include "utils.hpp"

#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include "ImageAnimation.hpp"

#include <QMovie>
#include <QPainter>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <qbytearrayview.h>
#include <qregularexpression.h>
#include <qstyle.h>
#include <qtextformat.h>
#include <unordered_map>
#include <unordered_set>
#ifdef HAVE_FONTCONFIG

#include <fontconfig/fontconfig.h>
#endif
#ifdef HAVE_FONTCONFIG
#else // !HAVE_FONTCONFIG

#include <QDirIterator>
#endif
#ifdef HAVE_FONTCONFIG
#else // !HAVE_FONTCONFIG
#include <QFile>
#endif
#ifdef HAVE_FONTCONFIG
#else // !HAVE_FONTCONFIG
#include <QStandardPaths>
#endif
#ifdef HAVE_LIBARCHIVE

#include <archive.h>
#endif
#ifdef HAVE_LIBARCHIVE
#include <archive_entry.h>
#endif
namespace
{


struct RsvgRect
{
    double x, y, w, h;
};
struct GErr
{
    int domain, code;
    char *msg;
};

using PFN_rsvg_new     = void *(*)(const char *, GErr **);
using PFN_rsvg_size    = int (*)(void *, double *, double *);
using PFN_rsvg_render  = int (*)(void *, void *, const RsvgRect *, GErr **);
using PFN_g_unref      = void (*)(void *);
using PFN_g_errfree    = void (*)(GErr *);
using PFN_surf_new     = void *(*)(int, int, int);
using PFN_cr_new       = void *(*)(void *);
using PFN_cr_destroy   = void (*)(void *);
using PFN_surf_flush   = void (*)(void *);
using PFN_surf_destroy = void (*)(void *);
using PFN_surf_data    = unsigned char *(*)(void *);
using PFN_surf_stride  = int (*)(void *);

struct RsvgLib
{
    PFN_rsvg_new new_from_file    = nullptr;
    PFN_rsvg_size get_size        = nullptr;
    PFN_rsvg_render render        = nullptr;
    PFN_g_unref g_unref           = nullptr;
    PFN_g_errfree g_errfree       = nullptr;
    PFN_surf_new surf_new         = nullptr;
    PFN_cr_new cr_new             = nullptr;
    PFN_cr_destroy cr_destroy     = nullptr;
    PFN_surf_flush surf_flush     = nullptr;
    PFN_surf_destroy surf_destroy = nullptr;
    PFN_surf_data surf_data       = nullptr;
    PFN_surf_stride surf_stride   = nullptr;
    bool ok                       = false;

    static RsvgLib &get() noexcept
    {
        static RsvgLib s;
        return s;
    }

private:
#if defined(Q_OS_WIN)
    // MSYS2/vcpkg ship these names on Windows
    QLibrary rsvg_lib{"librsvg-2-2"};
    QLibrary cairo_lib{"libcairo-2"};
#else
    // "rsvg-2", 2  →  librsvg-2.so.2  (Linux) / librsvg-2.2.dylib (macOS)
    // "cairo",  2  →  libcairo.so.2   (Linux) / libcairo.2.dylib   (macOS)
    QLibrary rsvg_lib{"rsvg-2"};
    QLibrary cairo_lib{"cairo"};
#endif

    RsvgLib() noexcept
    {
        if (!rsvg_lib.load() || !cairo_lib.load())
        {
            qCritical() << "Unable to load librsvg or libcairo";
            return;
        }

#define LOADSYM(lib, field, sym)                                               \
    field = reinterpret_cast<decltype(field)>(lib.resolve(sym));               \
    if (!field)                                                                \
        return;

        // g_object_unref / g_error_free live in gobject/glib which are
        // transitive deps of librsvg, so rsvg_lib.resolve() finds them.
        LOADSYM(rsvg_lib, new_from_file, "rsvg_handle_new_from_file")
        LOADSYM(rsvg_lib, get_size, "rsvg_handle_get_intrinsic_size_in_pixels")
        LOADSYM(rsvg_lib, render, "rsvg_handle_render_document")
        LOADSYM(rsvg_lib, g_unref, "g_object_unref")
        LOADSYM(rsvg_lib, g_errfree, "g_error_free")
        LOADSYM(cairo_lib, surf_new, "cairo_image_surface_create")
        LOADSYM(cairo_lib, cr_new, "cairo_create")
        LOADSYM(cairo_lib, cr_destroy, "cairo_destroy")
        LOADSYM(cairo_lib, surf_flush, "cairo_surface_flush")
        LOADSYM(cairo_lib, surf_destroy, "cairo_surface_destroy")
        LOADSYM(cairo_lib, surf_data, "cairo_image_surface_get_data")
        LOADSYM(cairo_lib, surf_stride, "cairo_image_surface_get_stride")
#undef LOADSYM
        ok = true;
    }
};

// ---- DjVu dynamic-loader support ----------------------------------------
// Minimal type definitions matching libdjvulibre ABI (stable since 3.5.x)

// miniexp_t is struct miniexp_s* in libdjvulibre; use void* for opaque handle
using djvu_miniexp_t = void *;

// miniexp_nil and miniexp_dummy are macros in miniexp.h (not exported symbols):
//   miniexp_nil   = (miniexp_t)(size_t)0
//   miniexp_dummy = (miniexp_t)(size_t)2
static const djvu_miniexp_t DJVU_MINIEXP_NIL = nullptr;
static const djvu_miniexp_t DJVU_MINIEXP_DUMMY
    = reinterpret_cast<void *>(static_cast<uintptr_t>(2));
static constexpr int DJVU_MSG_ERROR     = 0; // DDJVU_ERROR
// ddjvu_status_t: NOTSTARTED=0, STARTED=1, OK=2, FAILED=3, STOPPED=4.
// Anything >= OK means the job has terminated; only OK is success.
static constexpr int DJVU_JOB_OK        = 2;

struct DjVuPageInfo
{
    int width, height, dpi, rotation, version;
};
struct DjVuRect
{
    int x, y;
    unsigned w, h;
};

// Memory layout matches libdjvulibre ddjvu_message_s union
struct DjVuMsgAny
{
    int tag;
    void *ctx, *doc, *page, *job;
};
struct DjVuMsgErr
{
    DjVuMsgAny any;
    const char *message;
    int lineno;
    const char *file, *func;
};
union DjVuMsg
{
    DjVuMsgAny m_any;
    DjVuMsgErr m_error;
};

using PFN_djvu_ctx_create   = void *(*)(const char *);
using PFN_djvu_ctx_release  = void (*)(void *);
using PFN_djvu_doc_create   = void *(*)(void *, const char *, int);
using PFN_djvu_job_release  = void (*)(void *);
using PFN_djvu_doc_job      = void *(*)(void *);
using PFN_djvu_doc_pagenum  = int (*)(void *);
using PFN_djvu_doc_pageinfo = int (*)(void *, int, DjVuPageInfo *);
using PFN_djvu_doc_anno     = djvu_miniexp_t (*)(void *, int);
using PFN_djvu_anno_keys    = djvu_miniexp_t *(*)(djvu_miniexp_t);
using PFN_djvu_anno_meta    = const char *(*)(djvu_miniexp_t, djvu_miniexp_t);
using PFN_djvu_anno_xmp     = const char *(*)(djvu_miniexp_t);
using PFN_djvu_mexp_release = void (*)(void *, djvu_miniexp_t);
using PFN_djvu_msg_wait     = DjVuMsg *(*)(void *);
using PFN_djvu_msg_peek     = const DjVuMsg *(*)(void *);
using PFN_djvu_msg_pop      = void (*)(void *);
using PFN_djvu_page_create  = void *(*)(void *, int);
using PFN_djvu_job_status   = int (*)(void *);
using PFN_djvu_page_job     = void *(*)(void *);
using PFN_djvu_page_setrot  = void (*)(void *, int);
using PFN_djvu_page_dpi     = int (*)(void *);
using PFN_djvu_page_width   = int (*)(void *);
using PFN_djvu_page_height  = int (*)(void *);
using PFN_djvu_page_render
    = int (*)(void *, int, const DjVuRect *, const DjVuRect *, void *,
              unsigned long, char *);
using PFN_djvu_fmt_create   = void *(*)(int, int, unsigned int *);
using PFN_djvu_fmt_roworder = void (*)(void *, int);
using PFN_djvu_fmt_release  = void (*)(void *);
using PFN_mexp_symbol       = djvu_miniexp_t (*)(const char *);
using PFN_mexp_to_name      = const char *(*)(djvu_miniexp_t);
using PFN_djvu_version      = const char *(*)();

struct DjVuLib
{
    PFN_djvu_ctx_create ctx_create     = nullptr;
    PFN_djvu_ctx_release ctx_release   = nullptr;
    PFN_djvu_doc_create doc_create     = nullptr;
    // True if doc_create points at ddjvu_document_create_by_filename_utf8
    // (available since libdjvulibre 3.5.24). Otherwise the plain
    // ddjvu_document_create_by_filename is loaded, which takes a path in
    // the OS locale encoding — ANSI on Windows, UTF-8 on modern Linux.
    bool doc_create_is_utf8            = false;
    PFN_djvu_job_release job_release   = nullptr;
    PFN_djvu_doc_job doc_job           = nullptr;
    PFN_djvu_doc_pagenum doc_pagenum   = nullptr;
    PFN_djvu_doc_pageinfo doc_pageinfo = nullptr;
    PFN_djvu_doc_anno doc_anno         = nullptr;
    PFN_djvu_anno_keys anno_keys       = nullptr;
    PFN_djvu_anno_meta anno_meta       = nullptr;
    PFN_djvu_anno_xmp anno_xmp         = nullptr;
    PFN_djvu_mexp_release mexp_release = nullptr;
    PFN_djvu_msg_wait msg_wait         = nullptr;
    PFN_djvu_msg_peek msg_peek         = nullptr;
    PFN_djvu_msg_pop msg_pop           = nullptr;
    PFN_djvu_page_create page_create   = nullptr;
    PFN_djvu_job_status job_status     = nullptr;
    PFN_djvu_page_job page_job         = nullptr;
    PFN_djvu_page_setrot page_setrot   = nullptr;
    PFN_djvu_page_dpi page_dpi         = nullptr;
    PFN_djvu_page_width page_width     = nullptr;
    PFN_djvu_page_height page_height   = nullptr;
    PFN_djvu_page_render page_render   = nullptr;
    PFN_djvu_fmt_create fmt_create     = nullptr;
    PFN_djvu_fmt_roworder fmt_roworder = nullptr;
    PFN_djvu_fmt_release fmt_release   = nullptr;
    PFN_mexp_symbol mexp_symbol        = nullptr;
    PFN_mexp_to_name mexp_to_name      = nullptr;
    PFN_djvu_version version_str       = nullptr;
    bool ok                            = false;

    // miniexp_dummy is a macro in miniexp.h, value = (miniexp_t)(size_t)2
    static djvu_miniexp_t dummy() noexcept
    {
        return DJVU_MINIEXP_DUMMY;
    }

    static DjVuLib &get() noexcept
    {
        static DjVuLib s;
        return s;
    }

private:
    QLibrary lib{"djvulibre"};

    DjVuLib() noexcept
    {
        if (!lib.load())
        {
            qCritical() << "Unable to load djvulibre";
            return;
        }

#define DJLOADSYM(field, sym)                                                  \
    field = reinterpret_cast<decltype(field)>(lib.resolve(sym));               \
    if (!field)                                                                \
    {                                                                          \
        qWarning() << "Missing symbol" << sym;                                 \
        return;                                                                \
    }

        DJLOADSYM(ctx_create, "ddjvu_context_create")
        DJLOADSYM(ctx_release, "ddjvu_context_release")
        // Prefer the UTF-8 variant so non-ASCII paths work on Windows too.
        // Fall back to the plain (locale-encoded) entry point for very old
        // libdjvulibre (< 3.5.24, ~2011); if only the plain one is
        // available the call site must encode the path in the OS locale.
        doc_create = reinterpret_cast<PFN_djvu_doc_create>(
            lib.resolve("ddjvu_document_create_by_filename_utf8"));
        if (doc_create)
        {
            doc_create_is_utf8 = true;
        }
        else
        {
            DJLOADSYM(doc_create, "ddjvu_document_create_by_filename")
        }
        DJLOADSYM(job_release, "ddjvu_job_release")
        DJLOADSYM(doc_job, "ddjvu_document_job")
        DJLOADSYM(doc_pagenum, "ddjvu_document_get_pagenum")
        DJLOADSYM(doc_pageinfo, "ddjvu_document_get_pageinfo")
        DJLOADSYM(doc_anno, "ddjvu_document_get_anno")
        DJLOADSYM(anno_keys, "ddjvu_anno_get_metadata_keys")
        DJLOADSYM(anno_meta, "ddjvu_anno_get_metadata")
        DJLOADSYM(anno_xmp, "ddjvu_anno_get_xmp")
        DJLOADSYM(mexp_release, "ddjvu_miniexp_release")
        DJLOADSYM(msg_wait, "ddjvu_message_wait")
        DJLOADSYM(msg_peek, "ddjvu_message_peek")
        DJLOADSYM(msg_pop, "ddjvu_message_pop")
        DJLOADSYM(page_create, "ddjvu_page_create_by_pageno")
        DJLOADSYM(job_status, "ddjvu_job_status")
        DJLOADSYM(page_job, "ddjvu_page_job")
        DJLOADSYM(page_setrot, "ddjvu_page_set_rotation")
        DJLOADSYM(page_dpi, "ddjvu_page_get_resolution")
        DJLOADSYM(page_width, "ddjvu_page_get_width")
        DJLOADSYM(page_height, "ddjvu_page_get_height")
        DJLOADSYM(page_render, "ddjvu_page_render")
        DJLOADSYM(fmt_create, "ddjvu_format_create")
        DJLOADSYM(fmt_roworder, "ddjvu_format_set_row_order")
        DJLOADSYM(fmt_release, "ddjvu_format_release")
        DJLOADSYM(mexp_symbol, "miniexp_symbol")
        DJLOADSYM(mexp_to_name, "miniexp_to_name")
#undef DJLOADSYM

        // version_str is informational; failure doesn't disable DjVu support
        version_str = reinterpret_cast<PFN_djvu_version>(
            lib.resolve("ddjvu_get_version_string"));

        ok = true;
    }
}; // namespace

// ---- libexif dynamic-loader support -------------------------------------
// ExifEntry / ExifContent struct layouts have been stable since libexif
// 0.6.0 (2004); we mirror the first fields so we can access `tag` and pass
// entries back to libexif calls without linking against the library.

struct ExifEntryC
{
    int tag;    // ExifTag
    int format; // ExifFormat
    unsigned long components;
    unsigned char *data;
    unsigned int size;
    void *parent; // ExifContent *
    void *priv;
};

using PFN_exif_new     = void *(*)(const char *);
using PFN_exif_unref   = void (*)(void *);
using PFN_exif_data_fe = void (*)(void *, void (*)(void *, void *), void *);
using PFN_exif_cont_fe = void (*)(void *, void (*)(void *, void *), void *);
using PFN_exif_get_ifd = int (*)(void *);
using PFN_exif_tagname = const char *(*)(int, int);
using PFN_exif_entval  = const char *(*)(void *, char *, unsigned int);

struct ExifLib
{
    PFN_exif_new new_from_file   = nullptr;
    PFN_exif_unref data_unref    = nullptr;
    PFN_exif_data_fe data_foreach = nullptr;
    PFN_exif_cont_fe cont_foreach = nullptr;
    PFN_exif_get_ifd get_ifd     = nullptr;
    PFN_exif_tagname tag_name    = nullptr;
    PFN_exif_entval entry_value  = nullptr;
    bool ok                      = false;

    static ExifLib &get() noexcept
    {
        static ExifLib s;
        return s;
    }

private:
#if defined(Q_OS_WIN)
    QLibrary lib{"libexif-12"};
#else
    QLibrary lib{"exif", 12};
#endif

    ExifLib() noexcept
    {
        if (!lib.load())
            return;

#define LOADSYM(field, sym)                                                    \
    field = reinterpret_cast<decltype(field)>(lib.resolve(sym));               \
    if (!field)                                                                \
        return;
        LOADSYM(new_from_file, "exif_data_new_from_file")
        LOADSYM(data_unref, "exif_data_unref")
        LOADSYM(data_foreach, "exif_data_foreach_content")
        LOADSYM(cont_foreach, "exif_content_foreach_entry")
        LOADSYM(get_ifd, "exif_content_get_ifd")
        LOADSYM(tag_name, "exif_tag_get_name_in_ifd")
        LOADSYM(entry_value, "exif_entry_get_value")
#undef LOADSYM
        ok = true;
    }
};

struct ExifCollectCtx
{
    Model::Properties *out;
    int ifd;
};

static void
exif_entry_cb(void *entry_v, void *user)
{
    auto *ent = static_cast<ExifEntryC *>(entry_v);
    auto *ctx = static_cast<ExifCollectCtx *>(user);
    auto &el  = ExifLib::get();

    const char *name = el.tag_name(ent->tag, ctx->ifd);
    if (!name || !*name)
        return;

    char buf[512] = {0};
    const char *val = el.entry_value(ent, buf, sizeof(buf));
    if (!val || !*val)
        return;

    QString value = QString::fromUtf8(val).trimmed();
    if (value.isEmpty())
        return;
    if (value.length() > 200)
        value = value.left(200) + QStringLiteral("…");

    ctx->out->emplace_back(QString::fromLatin1(name), value);
}

static void
exif_content_cb(void *content_v, void *user)
{
    auto &el      = ExifLib::get();
    const int ifd = el.get_ifd(content_v);
    if (ifd == 1) // skip thumbnail IFD to avoid duplicates
        return;
    auto *outer = static_cast<Model::Properties *>(user);
    ExifCollectCtx inner{outer, ifd};
    el.cont_foreach(content_v, exif_entry_cb, &inner);
}

static void
populateExifProperties(const QString &path,
                       Model::Properties &props) noexcept
{
    auto &el = ExifLib::get();
    if (!el.ok)
        return;
    void *data = el.new_from_file(path.toUtf8().constData());
    if (!data)
        return;
    el.data_foreach(data, exif_content_cb, &props);
    el.data_unref(data);
}
} // namespace


static bool
isImageFormat(Model::FileType ft) noexcept
{
    switch (ft)
    {
        case Model::FileType::JPG:
        case Model::FileType::PNG:
        case Model::FileType::APNG:
        case Model::FileType::BMP:
        case Model::FileType::GIF:
        case Model::FileType::WEBP:
        case Model::FileType::TIFF:
        case Model::FileType::TGA:
        case Model::FileType::ICO:
        case Model::FileType::PPM:
        case Model::FileType::PGM:
        case Model::FileType::PBM:
        case Model::FileType::SVG:
            return true;
        default:
            return false;
    }
}

static void
handle_djvu_messages(void *ctx, int wait)
{
    auto &djvu = DjVuLib::get();
    const DjVuMsg *msg;
    if (wait)
        djvu.msg_wait(ctx);
    while ((msg = djvu.msg_peek(ctx)))
    {
        if (msg->m_any.tag == DJVU_MSG_ERROR)
            fprintf(stderr, "ddjvu error: %s\n", msg->m_error.message);
        djvu.msg_pop(ctx);
    }
}
#ifdef HAVE_LIBARCHIVE

// Reads a RAR/7z/... comic archive with libarchive and repackages its files
// as an in-memory zip, which MuPDF's comic handler opens. (The installed
// MuPDF is typically built without libarchive, so it can't read them itself.)
// The whole archive is held in memory while the document is open.
static fz_buffer *
archive_to_zip(fz_context *ctx, const char *path)
{
    struct archive *ar = archive_read_new();
    fz_buffer *zipbuf  = nullptr;
    fz_output *out     = nullptr;
    fz_zip_writer *zip = nullptr;

    fz_try(ctx)
    {
        archive_read_support_format_all(ar);
        archive_read_support_filter_all(ar);
        if (archive_read_open_filename(ar, path, 1 << 16) != ARCHIVE_OK)
            fz_throw(ctx, FZ_ERROR_FORMAT, "cannot read archive: %s",
                     archive_error_string(ar));

        zipbuf = fz_new_buffer(ctx, 1 << 20);
        out    = fz_new_output_with_buffer(ctx, zipbuf);
        zip    = fz_new_zip_writer_with_output(ctx, out);
        out    = nullptr; // the zip writer owns it now

        struct archive_entry *entry = nullptr;
        int r;
        while ((r = archive_read_next_header(ar, &entry)) == ARCHIVE_OK
               || r == ARCHIVE_WARN)
        {
            if (archive_entry_filetype(entry) != AE_IFREG)
                continue;
            const char *name = archive_entry_pathname(entry);
            if (!name || !*name)
                continue;

            fz_buffer *data = fz_new_buffer(ctx, 1 << 16);
            fz_try(ctx)
            {
                char block[1 << 16];
                la_ssize_t n;
                while ((n = archive_read_data(ar, block, sizeof block)) > 0)
                    fz_append_data(ctx, data, block, static_cast<size_t>(n));
                if (n < 0)
                    fz_throw(ctx, FZ_ERROR_FORMAT, "cannot read '%s': %s", name,
                             archive_error_string(ar));
                // Pages are already compressed images: store, don't deflate.
                fz_write_zip_entry(ctx, zip, name, data, 0);
            }
            fz_always(ctx)
                fz_drop_buffer(ctx, data);
            fz_catch(ctx)
                fz_rethrow(ctx);
        }
        if (r != ARCHIVE_EOF)
            fz_throw(ctx, FZ_ERROR_FORMAT, "cannot read archive: %s",
                     archive_error_string(ar));

        fz_close_zip_writer(ctx, zip);
    }
    fz_always(ctx)
    {
        fz_drop_zip_writer(ctx, zip);
        fz_drop_output(ctx, out);
        archive_read_free(ar);
    }
    fz_catch(ctx)
    {
        fz_drop_buffer(ctx, zipbuf);
        fz_rethrow(ctx);
    }
    return zipbuf;
}
#endif

// Opens a document. RAR (cbr) and 7z (cb7) comic archives go through
// libarchive when it is available; everything else is MuPDF's own business.
static fz_document *
open_document_any(fz_context *ctx, const char *path)
{
    const QString suffix = QFileInfo(QString::fromUtf8(path)).suffix().toLower();
    if (suffix != "cbr" && suffix != "cb7")
        return fz_open_document(ctx, path);

#ifdef HAVE_LIBARCHIVE
    fz_buffer *zipbuf = archive_to_zip(ctx, path);
    fz_stream *stm    = nullptr;
    fz_document *doc  = nullptr;
    fz_try(ctx)
    {
        stm = fz_open_buffer(ctx, zipbuf);
        doc = fz_open_document_with_stream(ctx, "comic.cbz", stm);
    }
    fz_always(ctx)
    {
        fz_drop_stream(ctx, stm);
        fz_drop_buffer(ctx, zipbuf);
    }
    fz_catch(ctx)
        fz_rethrow(ctx);
    return doc;
#else
    fz_throw(ctx, FZ_ERROR_UNSUPPORTED,
             "this build of Lektra has no support for RAR/7z archives");
#endif
}
namespace
{

// Reads dict[key] as a PDF text string, decoding the UTF-16BE-with-BOM form
// PDF text strings may use (same handling as the Info dict values above).
// Returns an empty string for a missing/non-string value.
QString
pdfStringValue(fz_context *ctx, pdf_obj *dict, pdf_obj *key) noexcept
{
    pdf_obj *val = pdf_dict_get(ctx, dict, key);
    if (!val || !pdf_is_string(ctx, val))
        return {};

    const char *s = pdf_to_str_buf(ctx, val);
    const int slen = pdf_to_str_len(ctx, val);

    if (slen >= 2 && (quint8)s[0] == 0xFE && (quint8)s[1] == 0xFF)
    {
        QStringDecoder decoder(QStringDecoder::Utf16BE);
        return decoder(QByteArray(s + 2, slen - 2));
    }
    return QString::fromUtf8(s, slen);
}
} // namespace


void
Model::cleanup_mupdf() noexcept
{
    fz_drop_outline(m_ctx, m_outline);
    m_outline = nullptr;
    fz_drop_outline(m_ctx, m_generated_outline);
    m_generated_outline = nullptr;
    fz_drop_document(m_ctx, m_doc);
    m_doc     = nullptr;
    m_pdf_doc = nullptr;

    // m_layout_w/h/em are per-document layout state tracked purely to
    // skip a redundant fz_layout_document call in relayoutForViewport();
    // must reset on document swap or a freshly opened document could
    // spuriously match the previous document's last-applied layout size
    // and skip being laid out at all.
    m_layout_w = m_layout_h = m_layout_em = 0.0f;

    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        m_page_lru_cache.clear();
        m_text_cache.clear();
        m_has_text_layer = -1;
        m_stext_page_cache.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        m_page_dim_cache.reset(0);
        m_default_page_dim = {};
    }
    {
        std::lock_guard<std::mutex> lock(m_content_bbox_mutex);
        m_content_bbox_cache.clear();
    }

    fz_empty_store(m_ctx);
}

void
Model::cleanup_image() noexcept
{
    m_is_image    = false;
    m_is_animated = false;
    if (m_movie)
    {
        // QMovie is a QObject with frameChanged/updated signals wired to
        // view code; an immediate delete during a signal chain crashes.
        // Disconnect first, then queue the delete for after the current
        // event returns.
        m_movie->stop();
        m_movie->disconnect();
        m_movie->deleteLater();
        m_movie = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        m_image_cache = QImage();
        m_page_dim_cache.reset(0);
        m_default_page_dim = {};
    }
    {
        std::lock_guard<std::mutex> lock(m_content_bbox_mutex);
        m_content_bbox_cache.clear();
    }
}

void
Model::cleanup_djvu() noexcept
{
    // NOTE on the render barrier: callers on the open/reload paths call
    // waitForPendingRenders() themselves and then reset m_render_cancelled
    // to false. cleanup_djvu therefore does NOT call waitForPendingRenders
    // itself — doing so would leave m_render_cancelled=true across the
    // caller's reset and every subsequent render would bail on the flag.
    // Any direct caller (e.g. Model::close) must run the barrier itself.
    auto &djvu = DjVuLib::get();
    if (djvu.ok)
    {
        if (m_ddjvu_doc)
            djvu.job_release(m_ddjvu_doc);
        if (m_ddjvu_ctx)
            djvu.ctx_release(m_ddjvu_ctx);
    }
    m_ddjvu_doc = nullptr;
    m_ddjvu_ctx = nullptr;

    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        m_page_lru_cache.clear();
        m_text_cache.clear();
        m_has_text_layer = -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        m_page_dim_cache.reset(0);
        m_default_page_dim = {};
    }
    {
        std::lock_guard<std::mutex> lock(m_content_bbox_mutex);
        m_content_bbox_cache.clear();
    }
}

// Open file asynchronously to avoid blocking the UI, especially for large
// documents or slow storage. The actual opening and page counting happens in a
// background thread, and results are posted back to the main thread when done.
// NOTE: no checking is done on the file path here — if it's invalid, the
// background thread will catch the error and emit openFileFailed. File
// existence checking should be done outside, before calling this function.
QFuture<void>
Model::openAsync(const QString &filePath) noexcept
{
    // canonicalFilePath() returns empty for a nonexistent/unreadable path —
    // fall back to the originally-requested path so the failure path below
    // still has something meaningful to show (e.g. a tab title) for what
    // couldn't be opened, instead of an empty filename.
    const QString canon = QFileInfo(filePath).canonicalFilePath();
    m_filepath              = canon.isEmpty() ? filePath : canon;
    const QString canonPath = m_filepath;
    m_success               = false;

    // Detect file type before launching the background task, so we can fail
    // fast for unsupported types without incurring the overhead of starting a
    // thread and cloning the context.
    m_filetype = getFileType(canonPath);
    m_is_image = isImageFormat(m_filetype);
    m_filesize = computeFileSize();

    if (m_filetype == FileType::NONE)
    {
        QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                  Qt::QueuedConnection);
        return QtConcurrent::run([] {});
    }

    if (m_filetype == FileType::DJVU)
        return openAsync_djvu(canonPath);

    if (m_is_image)
        return openAsync_image(canonPath);

    return openAsync_mupdf(canonPath);
}

QFuture<void>
Model::openAsync_image(const QString &canonPath) noexcept
{
    return QtConcurrent::run([this, canonPath]
    {
        if (m_filetype == FileType::SVG)
        {
            QImage img;
            int iw = 0, ih = 0;
            bool svg_rendered = false;
            auto &rsvg        = RsvgLib::get();
            if (rsvg.ok)
            {
                GErr *gerr = nullptr;
                void *handle
                    = rsvg.new_from_file(canonPath.toUtf8().constData(), &gerr);
                if (!handle)
                {
                    if (gerr)
                        rsvg.g_errfree(gerr);
                    QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                              Qt::QueuedConnection);
                    return;
                }
                double w_d = 0, h_d = 0;
                if (!rsvg.get_size(handle, &w_d, &h_d) || w_d <= 0 || h_d <= 0)
                    w_d = 800, h_d = 600;
                iw          = static_cast<int>(w_d);
                ih          = static_cast<int>(h_d);
                void *surf  = rsvg.surf_new(0 /*CAIRO_FORMAT_ARGB32*/, iw, ih);
                void *cr    = rsvg.cr_new(surf);
                RsvgRect vp = {0.0, 0.0, w_d, h_d};
                gerr        = nullptr;
                rsvg.render(handle, cr, &vp, &gerr);
                rsvg.cr_destroy(cr);
                rsvg.g_unref(handle);
                if (gerr)
                {
                    rsvg.g_errfree(gerr);
                    rsvg.surf_destroy(surf);
                    QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                              Qt::QueuedConnection);
                    return;
                }
                rsvg.surf_flush(surf);
                img = QImage(rsvg.surf_data(surf), iw, ih,
                             rsvg.surf_stride(surf),
                             QImage::Format_ARGB32_Premultiplied)
                          .copy();
                rsvg.surf_destroy(surf);
                svg_rendered = true;
            }
            if (!svg_rendered)
            {
                QSvgRenderer renderer(canonPath);
                if (!renderer.isValid())
                {
                    QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                              Qt::QueuedConnection);
                    return;
                }
                QSize sz = renderer.defaultSize();
                if (sz.isEmpty())
                    sz = QSize(800, 600);
                iw  = sz.width();
                ih  = sz.height();
                img = QImage(sz, QImage::Format_ARGB32);
                img.fill(Qt::transparent);
                QPainter p(&img);
                renderer.render(&p);
            }
            const float fw = static_cast<float>(iw);
            const float fh = static_cast<float>(ih);
            QMetaObject::invokeMethod(
                this, [this, img = std::move(img), fw, fh]() mutable
            {
                // Prevent a previous doc's render worker from touching
                // state we are about to swap out.
                waitForPendingRenders();
                cleanup_image();
                m_is_image         = true;
                m_is_animated      = false;
                m_success          = true;
                m_page_count       = 1;
                m_default_page_dim = {fw * 72.0f / m_dpi, fh * 72.0f / m_dpi};
                m_page_dim_cache.dimensions.assign(1, m_default_page_dim);
                m_page_dim_cache.known.assign(1, true);
                m_image_cache = std::move(img);
                emit openFileFinished();
            }, Qt::QueuedConnection);
            return;
        }

        QImageReader reader(canonPath);
        reader.setAutoTransform(true);

        if (!reader.canRead())
        {
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }

        const int frameCount = qMax(1, reader.imageCount());
        // Qt reports one frame for an APNG, so the file is looked at too.
        const bool animated
            = frameCount > 1 || ImageAnimation::isApng(canonPath);

        QImage first = reader.read();
        if (first.isNull())
        {
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }

        const float w = static_cast<float>(first.width());
        const float h = static_cast<float>(first.height());

        if (!animated)
        {
            QMetaObject::invokeMethod(
                this, [this, first = std::move(first), w, h]() mutable
            {
                waitForPendingRenders();
                cleanup_image();
                m_is_image         = true;
                m_is_animated      = false;
                m_success          = true;
                m_page_count       = 1;
                m_default_page_dim = {w * 72.0f / m_dpi, h * 72.0f / m_dpi};
                m_page_dim_cache.dimensions.assign(1, m_default_page_dim);
                m_page_dim_cache.known.assign(1, true);
                m_image_cache = std::move(first);
                emit openFileFinished();
            }, Qt::QueuedConnection);
            return;
        }

        // Animated: hand off to an ImageAnimation (QMovie for GIF and WebP,
        // our own decoder for APNG) — it decodes one frame at a time, keeping
        // memory at O(1 frame) instead of O(all frames).
        QMetaObject::invokeMethod(this, [this, canonPath, first = std::move(first), w, h]() mutable
        {
            waitForPendingRenders();
            cleanup_image();
            m_is_image         = true;
            m_success          = true;
            m_page_count       = 1;
            m_default_page_dim = {w * 72.0f / m_dpi, h * 72.0f / m_dpi};
            m_page_dim_cache.dimensions.assign(1, m_default_page_dim);
            m_page_dim_cache.known.assign(1, true);
            m_movie       = ImageAnimation::open(canonPath, this);
            m_is_animated = m_movie != nullptr;
            // Could not be played after all: show its first frame instead.
            if (!m_movie)
                m_image_cache = std::move(first);
            emit openFileFinished();
        }, Qt::QueuedConnection);
    });
}

QFuture<void>
Model::openAsync_djvu(const QString &canonPath) noexcept
{
    auto &djvu = DjVuLib::get();
    if (!djvu.ok)
    {
        QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                  Qt::QueuedConnection);
        return QtConcurrent::run([] {});
    }

    return QtConcurrent::run([this, canonPath]
    {
        auto &djvu = DjVuLib::get();
        void *ctx  = djvu.ctx_create("LEKTRA");
        // Match the encoding to the loaded doc_create variant: the _utf8
        // entry point takes UTF-8, the plain one takes the OS locale
        // encoding (ANSI on Windows, UTF-8 on modern Linux). Getting this
        // wrong breaks non-ASCII paths on Windows.
        const QByteArray pathBytes = djvu.doc_create_is_utf8
                                         ? canonPath.toUtf8()
                                         : canonPath.toLocal8Bit();
        const std::string pathStr(pathBytes.constData(), pathBytes.size());
        void *doc = djvu.doc_create(ctx, pathStr.c_str(), true);
        if (!doc)
        {
            djvu.ctx_release(ctx);
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }

        // Pump until the decode job terminates (status >= OK). Any status
        // above OK (FAILED, STOPPED) is a decode failure, not success.
        while (djvu.job_status(djvu.doc_job(doc)) < DJVU_JOB_OK)
        {
            DjVuMsg *msg = djvu.msg_wait(ctx);
            if (!msg || msg->m_any.tag == DJVU_MSG_ERROR)
            {
                djvu.job_release(doc);
                djvu.ctx_release(ctx);
                QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                          Qt::QueuedConnection);
                return;
            }
            djvu.msg_pop(ctx);
        }

        if (djvu.job_status(djvu.doc_job(doc)) != DJVU_JOB_OK)
        {
            djvu.job_release(doc);
            djvu.ctx_release(ctx);
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }

        const int page_count = djvu.doc_pagenum(doc);

        DjVuPageInfo info{};
        if (djvu.doc_pageinfo(doc, 0, &info) != DJVU_JOB_OK
            || info.dpi <= 0 || info.width <= 0 || info.height <= 0)
        {
            djvu.job_release(doc);
            djvu.ctx_release(ctx);
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }
        const float w = static_cast<float>(info.width) / info.dpi * 72.0f;
        const float h = static_cast<float>(info.height) / info.dpi * 72.0f;

        QMetaObject::invokeMethod(this, [this, ctx, doc, page_count, w, h]()
        {
            waitForPendingRenders();
            m_render_cancelled.store(false, std::memory_order_release);
            cleanup_mupdf(); // drops MuPDF state
            cleanup_djvu();  // drops any previous DjVu state
            cleanup_image();

            m_ddjvu_ctx  = ctx;
            m_ddjvu_doc  = doc;
            m_filetype   = FileType::DJVU;
            m_page_count = page_count;
            m_success    = true;
            m_text_cache.setCapacity(std::min(page_count, 1024));

            {
                std::lock_guard<std::mutex> lk(m_page_dim_mutex);
                m_default_page_dim = {w, h};
                m_page_dim_cache.dimensions.assign(page_count,
                                                   m_default_page_dim);
                m_page_dim_cache.known.assign(page_count, 0);
                if (page_count > 0)
                    m_page_dim_cache.known[0] = true;
            }

            emit openFileFinished();
        }, Qt::QueuedConnection);
    });
}

QFuture<void>
Model::openAsync_mupdf(const QString &canonPath) noexcept
{
    fz_context *bg_ctx = cloneContext();
    if (!bg_ctx)
    {
        QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                  Qt::QueuedConnection);
        return QtConcurrent::run([] {});
    }

    return QtConcurrent::run([this, canonPath, bg_ctx]
    {
        struct Guard
        {
            fz_context *ctx;
            fz_document *doc = nullptr;
            bool committed   = false;
            ~Guard()
            {
                if (!committed)
                {
                    if (doc)
                        fz_drop_document(ctx, doc);
                    fz_drop_context(ctx);
                }
            }
        } g{bg_ctx};

        cleanup_djvu();
        cleanup_mupdf();

        fz_document *doc           = nullptr;
        const QByteArray pathBytes = canonPath.toUtf8();
        const std::string pathStr(pathBytes.constData(), pathBytes.size());
        fz_try(bg_ctx)
        {
            doc = open_document_any(bg_ctx, pathStr.c_str());
            if (!doc)
            {
                fz_warn(bg_ctx, "Failed to open document: Unknown error");
                QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                          Qt::QueuedConnection);
                return;
            }
            g.doc = doc;

            // --- encrypted? park and stop ---
            if (m_filetype == FileType::PDF && fz_needs_password(bg_ctx, doc))
            {
                g.committed = true;
                QMetaObject::invokeMethod(this, [this, bg_ctx, doc]
                {
                    clearPending();
                    m_pending = {bg_ctx, doc};
                    emit passwordRequired();
                }, Qt::QueuedConnection);
                return;
            }

            // --- normal path ---
            g.committed = true;
            _continueOpen(bg_ctx, doc);
        }
        fz_catch(bg_ctx)
        {
            fz_warn(bg_ctx, "Failed to open document: %s",
                    fz_caught_message(bg_ctx));
            return;
        }
    });
}

void
Model::clearPending() noexcept
{
    if (m_pending.doc)
    {
        fz_drop_document(m_pending.ctx, m_pending.doc);
    }

    if (m_pending.ctx)
    {
        fz_drop_context(m_pending.ctx);
    }

    m_pending.clear();
}

QFuture<void>
Model::submitPassword(const QString &password) noexcept
{
    auto ctx = m_pending.ctx;
    auto doc = m_pending.doc;
    m_pending.clear();

    if (!ctx || !doc)
        return QtConcurrent::run([] {});

    return QtConcurrent::run([this, password, ctx, doc]
    {
        const std::string passwordStr = password.toStdString();
        if (!fz_authenticate_password(ctx, doc, passwordStr.c_str()))
        {
            // Wrong password — put it back so the user can retry
            QMetaObject::invokeMethod(this, [this, ctx, doc]
            {
                clearPending();
                m_pending = {ctx, doc};
                emit wrongPassword();
            }, Qt::QueuedConnection);
            return;
        }

        if (m_config.behavior.cache_password)
            QMetaObject::invokeMethod(this, [this, password]
            { m_cached_password = password; }, Qt::QueuedConnection);
        else
            QMetaObject::invokeMethod(this, [this]
            { m_cached_password.clear(); }, Qt::QueuedConnection);

        _continueOpen(ctx, doc);
    });
}

void
Model::_continueOpen(fz_context *ctx, fz_document *doc) noexcept
{
    int page_count = 0;
    float w = 0, h = 0;

    fz_try(ctx)
    {
        page_count = fz_count_pages(ctx, doc);
        if (page_count > 0)
        {
            fz_page *p = fz_load_page(ctx, doc, 0);
            fz_rect r  = fz_bound_page(ctx, p);
            fz_drop_page(ctx, p);
            w = r.x1 - r.x0;
            h = r.y1 - r.y0;
        }
    }
    fz_catch(ctx)
    {
        QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                  Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(this, [this, ctx, doc, page_count, w, h]
    {
        waitForPendingRenders();
        m_render_cancelled.store(false, std::memory_order_release);
        cleanup_mupdf();
        cleanup_djvu();
        cleanup_image();
        fz_drop_context(m_ctx);

        m_ctx        = ctx;
        m_doc        = doc;
        m_pdf_doc    = pdf_specifics(m_ctx, m_doc);
        m_page_count = page_count;
        m_success    = true;
        m_text_cache.setCapacity(std::min(page_count, 1024));

        {
            std::lock_guard<std::mutex> lk(m_page_dim_mutex);
            m_default_page_dim = {w, h};
            m_page_dim_cache.dimensions.assign(page_count, m_default_page_dim);
            m_page_dim_cache.known.assign(page_count, 0);
            if (page_count > 0)
                m_page_dim_cache.known[0] = true;
        }

        emit openFileFinished();
    }, Qt::QueuedConnection);
}

void
Model::close() noexcept
{
    m_filepath.clear();

    // Barrier: cancel and drain in-flight renders before we free the
    // backing document. Reset the cancelled flag after so a future open
    // on the same Model isn't stuck in cancelled state.
    waitForPendingRenders();
    m_render_cancelled.store(false, std::memory_order_release);

    if (m_filetype == FileType::DJVU)
    {
        cleanup_djvu();
        return;
    }
    if (m_is_image)
    {
        cleanup_image();
        return;
    }
    cleanup_mupdf();
}

bool
Model::decrypt() noexcept
{
    if (!m_ctx || !m_doc || !m_pdf_doc)
        return false;

    fz_try(m_ctx)
    {
        pdf_write_options opts = m_pdf_write_options;
        opts.do_encrypt        = PDF_ENCRYPT_NONE;

        if (m_pdf_doc)
        {
            const QByteArray filePathBytes = m_filepath.toUtf8();
            const std::string filePathStr(filePathBytes.constData(),
                                          filePathBytes.size());
            pdf_save_document(m_ctx, m_pdf_doc, filePathStr.c_str(), &opts);
        }
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Cannot decrypt file: " << fz_caught_message(m_ctx);
        return false;
    }
    return true;
}

bool
Model::encrypt(const EncryptInfo &info) noexcept
{
    if (!m_ctx || !m_doc || !m_pdf_doc)
        return false;

    fz_try(m_ctx)
    {

        pdf_write_options opts = m_pdf_write_options;
        opts.do_encrypt        = PDF_ENCRYPT_AES_256;

        QByteArray userPwdBytes = info.user_password.toUtf8();
        strncpy(opts.upwd_utf8, userPwdBytes.constData(),
                sizeof(opts.upwd_utf8) - 1);

        // Set owner password (required for full access/editing)
        // QByteArray ownerPwdBytes = password.toUtf8();
        strncpy(opts.opwd_utf8, userPwdBytes.constData(),
                sizeof(opts.opwd_utf8) - 1);

        opts.permissions = PDF_PERM_PRINT | PDF_PERM_COPY | PDF_PERM_ANNOTATE
                           | PDF_PERM_FORM | PDF_PERM_MODIFY | PDF_PERM_ASSEMBLE
                           | PDF_PERM_PRINT_HQ;

        m_pdf_write_options = opts;
        SaveChanges();
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Encryption failed:" << fz_caught_message(m_ctx);
        return false;
    }

    return true;
}

bool
Model::reloadDocument() noexcept
{
    if (m_filepath.isEmpty())
        return false;

    // Open the fresh document BEFORE cleanup() drops the old one.
    // cleanup() calls fz_drop_document(m_ctx, m_doc) and nulls m_doc,
    // so we must have the new handle ready before that happens.
    fz_document *new_doc = nullptr;
    fz_try(m_ctx)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        const QByteArray filePathBytes = m_filepath.toUtf8();
        const std::string filePathStr(filePathBytes.constData(),
                                      filePathBytes.size());
        new_doc = open_document_any(m_ctx, filePathStr.c_str());
        if (!new_doc)
            return false;

        if (fz_needs_password(m_ctx, new_doc))
        {
            const bool canAuth = m_config.behavior.cache_password
                                 && !m_cached_password.isEmpty();
            const bool authed
                = canAuth
                  && fz_authenticate_password(
                      m_ctx, new_doc, m_cached_password.toStdString().c_str());
            if (!authed)
            {
                fz_drop_document(m_ctx, new_doc);
                emit reloadPasswordRequired();
                return false;
            }
        }
    }
    fz_catch(m_ctx)
    {
        qWarning() << "reloadDocument: failed to open:"
                   << fz_caught_message(m_ctx);
        return false;
    }

    int page_count = 0;
    float w = 0, h = 0;
    fz_page *page = nullptr;
    fz_try(m_ctx)
    {
        page_count = fz_count_pages(m_ctx, new_doc);
        if (page_count > 0)
        {
            page      = fz_load_page(m_ctx, new_doc, 0);
            fz_rect r = fz_bound_page(m_ctx, page);
            w         = r.x1 - r.x0;
            h         = r.y1 - r.y0;
        }
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        fz_drop_document(m_ctx, new_doc);
        return false;
    }

    waitForPendingRenders();
    m_render_cancelled.store(false, std::memory_order_release);
    cleanup_mupdf();

    // Flush the MuPDF store so cloned contexts won't serve stale entries
    // from the old document's object graph to buildPageCache.
    fz_empty_store(m_ctx);

    m_doc        = new_doc;
    m_pdf_doc    = pdf_specifics(m_ctx, m_doc);
    m_page_count = page_count;
    m_success    = true;
    m_text_cache.setCapacity(std::min(page_count, 1024));

    {
        std::lock_guard<std::mutex> lk(m_page_dim_mutex);
        m_default_page_dim = {w, h};
        m_page_dim_cache.dimensions.assign(page_count, m_default_page_dim);
        m_page_dim_cache.known.assign(page_count, 0);
        if (page_count > 0)
            m_page_dim_cache.known[0] = true;
    }

    return true;
}

bool
Model::SaveChanges() noexcept
{
    if (m_filetype == FileType::DJVU)
        return false;

    fz_try(m_ctx)
    {
        const QByteArray pathBytes = m_filepath.toUtf8();
        const std::string pathStr(pathBytes.constData(), pathBytes.size());
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        pdf_write_options opts = m_pdf_write_options;
        opts.do_incremental    = 1;
        pdf_save_document(m_ctx, m_pdf_doc, pathStr.c_str(), &opts);

        m_undo_stack->setClean();

        return true;
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Save failed: " << fz_caught_message(m_ctx);
    }

    return false;
}

bool
Model::SaveAs(const QString &newFilePath) noexcept
{
    if (!m_doc || !m_pdf_doc)
        return false;

    const QByteArray pathBytes = newFilePath.toUtf8();
    const std::string pathStr(pathBytes.constData(), pathBytes.size());
    fz_try(m_ctx)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        pdf_write_options opts = m_pdf_write_options;

        pdf_save_document(m_ctx, m_pdf_doc, pathStr.c_str(), &opts);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Save As failed: " << fz_caught_message(m_ctx);
        return false;
    }
    return true;
}

Model::Properties
Model::properties() noexcept
{
    Properties props;
    props.push_back(qMakePair("Path", m_filepath));
    props.push_back(qMakePair("Type", fileTypeToString()));
    props.push_back(qMakePair("Size", fileSizeToString()));

    if (m_filetype == FileType::DJVU)
    {
        auto &djvu = DjVuLib::get();
        if (!djvu.ok || !m_ddjvu_ctx || !m_ddjvu_doc)
            return props;

        /* Fetch document-wide annotations.
           compat=1 also searches the shared annotation chunk
           so metadata is found in older files too.
           This runs on the UI thread (File Properties dialog), so cap the
           spin: a silently-erroring annotation job used to freeze the UI
           forever waiting for the dummy sentinel to clear. */
        djvu_miniexp_t anno;
        constexpr int MAX_ANNO_WAIT_ITERS = 500;
        int anno_wait_iters               = 0;
        while ((anno = djvu.doc_anno(m_ddjvu_doc, 1)) == djvu.dummy())
        {
            handle_djvu_messages(m_ddjvu_ctx, 1);
            if (++anno_wait_iters >= MAX_ANNO_WAIT_ITERS)
            {
                qWarning() << "populateDjVuProperties(): giving up on "
                              "annotation fetch after"
                           << anno_wait_iters << "iterations";
                return props;
            }
        }

        if (anno == DJVU_MINIEXP_NIL || anno == djvu.mexp_symbol("failed")
            || anno == djvu.mexp_symbol("stopped"))
        {
            return props;
        }

        /* Key/value metadata pairs */
        djvu_miniexp_t *keys = djvu.anno_keys(anno);
        if (keys)
        {
            for (int i = 0; keys[i]; i++)
            {
                const char *key = djvu.mexp_to_name(keys[i]);
                const char *val = djvu.anno_meta(anno, keys[i]);
                if (key && val)
                    props.emplace_back(key, val);
            }
            free(keys);
        }

        /* XMP metadata blob (if present) */
        const char *xmp = djvu.anno_xmp(anno);
        if (xmp)
            props.emplace_back("XMP", xmp);

        djvu.mexp_release(m_ddjvu_doc, anno);
    }

    else if (m_is_image)
    {
        QImageReader reader(m_filepath);
        props.emplace_back("Width", QString::number(reader.size().width()));
        props.emplace_back("Height", QString::number(reader.size().height()));
        props.emplace_back("Format", reader.format().constData());
        props.emplace_back("Animated",
                           reader.supportsAnimation() ? "Yes" : "No");

        // m_image_cache is already the fully-decoded image (loaded when the
        // file was opened), so this reuses it instead of decoding again just
        // for its DPI metadata. Not populated for animated images (those are
        // driven by QMovie instead), so DPI is skipped for those.
        if (!m_image_cache.isNull() && m_image_cache.dotsPerMeterX() > 0
            && m_image_cache.dotsPerMeterY() > 0)
        {
            const double dpiX = m_image_cache.dotsPerMeterX() * 0.0254;
            const double dpiY = m_image_cache.dotsPerMeterY() * 0.0254;
            if (qFuzzyCompare(dpiX, dpiY))
                props.emplace_back("DPI", QString::number(qRound(dpiX)));
            else
                props.emplace_back(
                    "DPI",
                    QString("%1 x %2").arg(qRound(dpiX)).arg(qRound(dpiY)));
        }

        populateExifProperties(m_filepath, props);
    }

    else
    {
        if (!m_ctx || !m_doc)
            return props;

        props.push_back(qMakePair(
            "Encrypted", fz_needs_password(m_ctx, m_doc) ? "Yes" : "No"));
        props.push_back(qMakePair("Page Count", QString::number(m_page_count)));

        if (m_pdf_doc)
        {
            populatePDFProperties(props);
            populateSignatureProperties(props);
        }
        else if (m_filetype == FileType::CBZ)
            populateCBZProperties(props);
    }

    return props;
}

void
Model::populatePDFProperties(
    std::vector<std::pair<QString, QString>> &props) noexcept
{
    // ========== Info Dictionary ==========
    pdf_obj *info
        = pdf_dict_get(m_ctx, pdf_trailer(m_ctx, m_pdf_doc), PDF_NAME(Info));
    if (info && pdf_is_dict(m_ctx, info))
    {
        int len = pdf_dict_len(m_ctx, info);
        for (int i = 0; i < len; ++i)
        {
            pdf_obj *keyObj = pdf_dict_get_key(m_ctx, info, i);
            pdf_obj *valObj = pdf_dict_get_val(m_ctx, info, i);

            if (!pdf_is_name(m_ctx, keyObj))
                continue;

            QString key = QString::fromLatin1(pdf_to_name(m_ctx, keyObj));
            QString val;

            if (pdf_is_string(m_ctx, valObj))
            {
                const char *s = pdf_to_str_buf(m_ctx, valObj);
                int slen      = pdf_to_str_len(m_ctx, valObj);

                if (slen >= 2 && (quint8)s[0] == 0xFE && (quint8)s[1] == 0xFF)
                {
                    QStringDecoder decoder(QStringDecoder::Utf16BE);
                    val = decoder(QByteArray(s + 2, slen - 2));
                }
                else
                {
                    val = QString::fromUtf8(s, slen);
                }
            }
            else if (pdf_is_int(m_ctx, valObj))
                val = QString::number(pdf_to_int(m_ctx, valObj));
            else if (pdf_is_bool(m_ctx, valObj))
                val = pdf_to_bool(m_ctx, valObj) ? "true" : "false";
            else if (pdf_is_name(m_ctx, valObj))
                val = QString::fromLatin1(pdf_to_name(m_ctx, valObj));
            else
                val = QStringLiteral("[Non-string value]");

            props.push_back({key, val});
        }
    }

    props.push_back(
        qMakePair("PDF Version", QString("%1.%2")
                                     .arg(m_pdf_doc->version / 10)
                                     .arg(m_pdf_doc->version % 10)));
}

// Digital signature info (signer/date/reason/location, no cryptographic
// verification — that would need a pdf_pkcs7_verifier backed by OpenSSL,
// which isn't linked in this build). Signature widgets can live on any
// page, so every page is scanned for PDF_WIDGET_TYPE_SIGNATURE annots.
void
Model::populateSignatureProperties(Properties &props) noexcept
{
    const int sigCount = pdf_count_signatures(m_ctx, m_pdf_doc);
    if (sigCount <= 0)
        return;

    props.emplace_back("Digital Signatures", QString::number(sigCount));

    int index = 1;
    std::lock_guard<std::mutex> lock(m_doc_mutex);
    for (int pageno = 0; pageno < m_page_count; ++pageno)
    {
        pdf_page *page = nullptr;
        fz_try(m_ctx)
        {
            page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

            for (pdf_annot *widget = pdf_first_widget(m_ctx, page); widget;
                 widget             = pdf_next_widget(m_ctx, widget))
            {
                if (pdf_widget_type(m_ctx, widget)
                    != PDF_WIDGET_TYPE_SIGNATURE)
                    continue;

                pdf_obj *field = pdf_annot_obj(m_ctx, widget);
                const bool isSigned
                    = pdf_signature_is_signed(m_ctx, m_pdf_doc, field);

                const QString prefix
                    = QString("Signature %1").arg(index++);
                props.emplace_back(prefix + " Status",
                                   isSigned ? "Signed" : "Unsigned");

                if (!isSigned)
                    continue;

                pdf_obj *v = pdf_dict_get(m_ctx, field, PDF_NAME(V));
                if (!v)
                    continue;

                const QString name = pdfStringValue(m_ctx, v, PDF_NAME(Name));
                const QString date = pdfStringValue(m_ctx, v, PDF_NAME(M));
                const QString reason
                    = pdfStringValue(m_ctx, v, PDF_NAME(Reason));
                const QString location
                    = pdfStringValue(m_ctx, v, PDF_NAME(Location));

                if (!name.isEmpty())
                    props.emplace_back(prefix + " Signer", name);
                if (!date.isEmpty())
                    props.emplace_back(prefix + " Date", date);
                if (!reason.isEmpty())
                    props.emplace_back(prefix + " Reason", reason);
                if (!location.isEmpty())
                    props.emplace_back(prefix + " Location", location);
            }
        }
        fz_always(m_ctx)
        {
            pdf_drop_page(m_ctx, page);
        }
        fz_catch(m_ctx)
        {
            qWarning() << "populateSignatureProperties(): failed on page"
                       << pageno << ":" << fz_caught_message(m_ctx);
        }
    }
}

// Lists the image files inside a CBZ/CBT archive. fz_open_archive() reopens
// the file independently of the fz_document (which only exposes it as a
// page sequence, not the raw archive entries) and auto-detects zip vs tar,
// so this covers both CBZ and CBT with the same code.
void
Model::populateCBZProperties(Properties &props) noexcept
{
    fz_archive *arch = nullptr;
    fz_try(m_ctx)
    {
        arch = fz_open_archive(m_ctx, m_filepath.toUtf8().constData());
    }
    fz_catch(m_ctx)
    {
        return;
    }
    if (!arch)
        return;

    static const QSet<QString> imageExts
        = {"jpg", "jpeg", "png", "gif", "bmp", "webp", "tif", "tiff"};

    QStringList images;
    const int n = fz_count_archive_entries(m_ctx, arch);
    for (int i = 0; i < n; ++i)
    {
        const char *name = fz_list_archive_entry(m_ctx, arch, i);
        if (!name)
            continue;
        const QString qname = QString::fromUtf8(name);
        if (imageExts.contains(QFileInfo(qname).suffix().toLower()))
            images.push_back(qname);
    }
    fz_drop_archive(m_ctx, arch);

    props.emplace_back("Image Count", QString::number(images.size()));
    props.emplace_back("Images", images.join("\n"));
}

void
Model::cancelOpen() noexcept
{
    if (m_pending.ctx)
    {
        fz_drop_document(m_pending.ctx, m_pending.doc);
        fz_drop_context(m_pending.ctx);
        m_pending.clear();
    }

    cleanup_mupdf();

    emit openFileFailed();
}
