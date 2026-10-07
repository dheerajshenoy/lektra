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

// ---- DjVu dynamic-loader support ----------------------------------------
// Minimal type definitions matching libdjvulibre ABI (stable since 3.5.x)

// miniexp_t is struct miniexp_s* in libdjvulibre; use void* for opaque handle
using djvu_miniexp_t = void *;
static const djvu_miniexp_t DJVU_MINIEXP_DUMMY
    = reinterpret_cast<void *>(static_cast<uintptr_t>(2));

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
} // namespace

Model::Model(const Config &config, QObject *parent) noexcept
    : QObject(parent), m_config(config)
{
    initMuPDF();
    m_undo_stack = new QUndoStack(this);
    setUrlLinkRegex(m_config.links.url_regex);

    // Eviction for LRU Cache
    m_text_cache.setCapacity(512); // TODO: make this configurable
    m_page_lru_cache.setCapacity(m_config.behavior.cache_pages);
    m_page_lru_cache.setCallback([this](PageCacheEntry &entry)
    {
        const int pageno = entry.pageno;
        if (entry.display_list)
        {
            fz_drop_display_list(m_ctx, entry.display_list);
            entry.display_list = nullptr;
            // fz_drop_context(ctx);
        }

        m_text_cache.remove(pageno);
    });

    m_stext_page_cache.setCapacity(10); // TODO: make this configurable
    m_stext_page_cache.setCallback([this](fz_stext_page *stext_page)
    {
        if (!stext_page)
            return;

        if (m_ctx)
            fz_drop_stext_page(m_ctx, stext_page);
    });

    connect(m_undo_stack, &QUndoStack::cleanChanged, this,
            [this](bool isClean) { emit undoStackCleanChanged(isClean); });
}

Model::~Model() noexcept
{
#ifndef NDEBUG
    PPRINT("Model destructor called");
#endif
    m_search_cancelled.store(true);
    m_search_future.cancel();
    m_search_future.waitForFinished();

    m_render_cancelled.store(true);
    waitForPendingRenders();

    if (m_filetype == FileType::DJVU)
    {
        cleanup_djvu();
    }
    else if (m_is_image)
    {
        cleanup_image();
    }
    else
    {
        cleanup_mupdf();
    }

    if (m_ctx)
        fz_drop_context(m_ctx);
}

Model::FileType
Model::getFileType(const QString &path) noexcept
{
    // HTML/Markdown/plain text/CBZ have no reliable content "magic" (unlike
    // PDF/EPUB/DjVu), so MatchContent sniffing below can't identify them —
    // go by extension instead, mirroring the extension lists MuPDF's own
    // html/md/txt document handlers use internally. CBZ in particular is
    // just a plain ZIP of images with no internal marker distinguishing it
    // from a generic zip, so content-only sniffing always reports it as
    // "application/zip", never "application/vnd.comicbook+zip" — the mime
    // type below is glob-registered only, matching *.cbz by name.
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "html" || suffix == "htm" || suffix == "xhtml")
        return FileType::HTML;
    if (suffix == "md")
        return FileType::MD;
    if (suffix == "txt" || suffix == "text" || suffix == "log")
        return FileType::TXT;
    if (suffix == "docx")
        return FileType::DOCX;
    if (suffix == "xlsx")
        return FileType::XLSX;
    if (suffix == "pptx")
        return FileType::PPTX;
    // Comic archives: MuPDF reads zip (cbz), tar (cbt), and rar/7z (cbr, cb7)
    // via libarchive. Archives have no content marker that says "comic", so
    // go by extension.
    if (suffix == "cbz" || suffix == "cbr" || suffix == "cb7"
        || suffix == "cbt")
        return FileType::CBZ;

    const QMimeType mime
        = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent);
    const QString name = mime.name();

    // Documents
    if (name == "application/pdf")
        return FileType::PDF;
    if (name == "application/epub+zip")
        return FileType::EPUB;
    if (name == "application/vnd.ms-xpsdocument" || name == "application/oxps")
        return FileType::XPS;
    if (name == "application/x-mobipocket-ebook")
        return FileType::MOBI;
    if (name == "application/vnd.comicbook+zip" || name == "application/x-cbz")
        return FileType::CBZ;
    if (name == "application/x-tar")
        return FileType::CBZ; // cbt
    if (name == "application/x-fictionbook+xml"
        || name == "application/x-fictionbook")
        return FileType::FB2;
    if (DjVuLib::get().ok
        && (name == "image/vnd.djvu" || name == "image/vnd.djvu+multipage"
            || name == "image/x-djvu"))
        return FileType::DJVU;

    // Images
    if (name == "image/jpeg")
        return FileType::JPG;
    if (name == "image/png" || name == "image/apng")
        return FileType::PNG; // APNG shares PNG mime on most systems
    if (name == "image/tiff")
        return FileType::TIFF;
    if (name == "image/svg+xml" || name == "image/svg")
        return FileType::SVG;

    if (name == "image/bmp" || name == "image/x-bmp")
        return FileType::BMP;
    if (name == "image/gif")
        return FileType::GIF;
    if (name == "image/webp")
        return FileType::WEBP;
    if (name == "image/x-tga" || name == "image/x-targa")
        return FileType::TGA;
    if (name == "image/vnd.microsoft.icon" || name == "image/x-ico")
        return FileType::ICO;
    if (name == "image/x-portable-pixmap")
        return FileType::PPM;
    if (name == "image/x-portable-graymap")
        return FileType::PGM;
    if (name == "image/x-portable-bitmap")
        return FileType::PBM;

    return FileType::NONE;
}

void
Model::waitForPendingRenders() noexcept
{
    m_render_cancelled.store(true, std::memory_order_release);
    std::unique_lock<std::mutex> lock(m_renders_mutex);
    m_renders_cv.wait(lock, [this]
    { return m_active_renders.load(std::memory_order_acquire) == 0; });
}

QString
Model::fileTypeToString() const noexcept
{
    return fileTypeName(m_filetype, m_filepath);
}

QString
Model::fileTypeName(FileType type, const QString &filepath) noexcept
{
    switch (type)
    {
        case FileType::PDF:
            // Adobe Illustrator files are PDF-compatible (valid PDF under
            // the hood, opened via the same PDF path) and have no distinct
            // content "magic" of their own to detect by — recognized here
            // by extension purely for a more accurate label.
            if (filepath.endsWith(".ai", Qt::CaseInsensitive))
                return "AI";
            return "PDF";
        case FileType::EPUB:
            return "EPUB";
        case FileType::XPS:
            return "XPS";
        case FileType::MOBI:
            return "MOBI";
        case FileType::CBZ:
            return "CBZ/CBT";
        case FileType::FB2:
            return "FB2";
        case FileType::HTML:
            return "HTML";
        case FileType::MD:
            return "Markdown";
        case FileType::TXT:
            return "Text";
        case FileType::DOCX:
            return "DOCX";
        case FileType::XLSX:
            return "XLSX";
        case FileType::PPTX:
            return "PPTX";
        case FileType::DJVU:
            return "DJVU";
        case FileType::JPG:
            return "JPEG";
        case FileType::PNG:
            return "PNG";
        case FileType::TIFF:
            return "TIFF";
        case FileType::SVG:
            return "SVG";
        case FileType::BMP:
            return "BMP";
        case FileType::GIF:
            return "GIF";
        case FileType::WEBP:
            return "WEBP";
        case FileType::TGA:
            return "TGA";
        case FileType::ICO:
            return "ICO";
        case FileType::PPM:
            return "PPM";
        case FileType::PGM:
            return "PGM";
        case FileType::PBM:
            return "PBM";
        default:
            return "Unknown";
    }
}

Model::FileSize
Model::computeFileSize() noexcept
{
    QFileInfo fileInfo(m_filepath);
    return fileInfo.size();
}

QString
Model::fileSizeToString() const noexcept
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex              = 0;
    double displaySize         = static_cast<double>(m_filesize);

    while (displaySize >= 1024 && unitIndex < 4)
    {
        displaySize /= 1024;
        ++unitIndex;
    }

    return QString::number(displaySize, 'f', 2) + " " + units[unitIndex];
}
