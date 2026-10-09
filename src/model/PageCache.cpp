#include "BrowseLinkItem.hpp"
#include "Commands/TextHighlightAnnotationCommand.hpp"
#include "Config.hpp"
#include "Model.hpp"
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

// Build scale→rotate→[flip]→translate-to-origin matrix (manual pattern sites).
static fz_matrix
buildPageToDevMatrix(fz_rect bounds, float scale, float rotation, bool flip_h,
                     bool flip_v) noexcept
{
    fz_matrix m = fz_scale(scale, scale);
    m           = fz_pre_rotate(m, rotation);
    if (flip_h)
        m = fz_concat(m, fz_scale(-1.0f, 1.0f));
    if (flip_v)
        m = fz_concat(m, fz_scale(1.0f, -1.0f));
    const fz_rect dev_bounds = fz_transform_rect(bounds, m);
    return fz_concat(m, fz_translate(-dev_bounds.x0, -dev_bounds.y0));
}

// Same but starting from fz_transform_page (render path sites).
static fz_matrix
buildRenderTransform(fz_rect bounds, float zoom, float rotation, bool flip_h,
                     bool flip_v) noexcept
{
    fz_matrix m = fz_transform_page(bounds, zoom, rotation);
    if (flip_h || flip_v)
    {
        if (flip_h)
            m = fz_concat(m, fz_scale(-1.0f, 1.0f));
        if (flip_v)
            m = fz_concat(m, fz_scale(1.0f, -1.0f));
        const fz_rect dev_bounds = fz_transform_rect(bounds, m);
        m = fz_concat(m, fz_translate(-dev_bounds.x0, -dev_bounds.y0));
    }
    return m;
}

// Build a 256-entry lookup table for the high-contrast tone stretch.
// Pixels ≤ black become 0, pixels ≥ white become 255, midtones linearly
// stretched. Applied to every sample-byte (each colour channel is treated
// the same way) — for scanned / grayish pages this cleans up the paper
// background and sharpens text; on already-black-on-white PDFs it is a
// near no-op. Caller checks the identity case (black=0, white=255) and
// skips the whole thing.
static void
buildHighContrastLUT(unsigned char lut[256], int black, int white) noexcept
{
    if (black < 0)
        black = 0;
    if (white > 255)
        white = 255;
    if (white <= black)
    {
        // Degenerate config — fall back to a hard threshold at the midpoint
        // between the two bounds so the setting is not silently a no-op.
        const int cut = (black + white) / 2;
        for (int i = 0; i < 256; ++i)
            lut[i] = (i <= cut) ? 0 : 255;
        return;
    }
    const int span = white - black;
    for (int i = 0; i < 256; ++i)
    {
        int v;
        if (i <= black)
            v = 0;
        else if (i >= white)
            v = 255;
        else
            v = ((i - black) * 255 + span / 2) / span;
        lut[i] = static_cast<unsigned char>(v);
    }
}

// Same helper for the DjVu / QImage render path.
static void
applyHighContrastQImage(QImage &img, int black, int white) noexcept
{
    if (black == 0 && white == 255)
        return;
    unsigned char lut[256];
    buildHighContrastLUT(lut, black, white);
    const int h = img.height();
    const int w = img.width();
    for (int y = 0; y < h; ++y)
    {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const QRgb px = row[x];
            row[x] = qRgb(lut[qRed(px)], lut[qGreen(px)], lut[qBlue(px)]);
        }
    }
}

// Match fz_tint_pixmap's linear remap so DjVu-rendered pages honour the
// same page.bg / page.fg colours the MuPDF path already applies. Each
// channel maps 0 → fg, 255 → bg, with linear interpolation in between.
// The identity case (fg=black, bg=white) is short-circuited so default
// colours don't pay any per-pixel cost.
static void
tintQImageRGB(QImage &img, uint32_t fg_rgb, uint32_t bg_rgb) noexcept
{
    if (fg_rgb == 0x000000 && bg_rgb == 0xFFFFFF)
        return; // identity — no change

    const int fg_r = (fg_rgb >> 16) & 0xFF;
    const int fg_g = (fg_rgb >> 8) & 0xFF;
    const int fg_b = fg_rgb & 0xFF;
    const int dr   = static_cast<int>((bg_rgb >> 16) & 0xFF) - fg_r;
    const int dg   = static_cast<int>((bg_rgb >> 8) & 0xFF) - fg_g;
    const int db   = static_cast<int>(bg_rgb & 0xFF) - fg_b;

    const int h = img.height();
    const int w = img.width();
    for (int y = 0; y < h; ++y)
    {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const QRgb px = row[x];
            const int r   = (qRed(px) * dr) / 255 + fg_r;
            const int g   = (qGreen(px) * dg) / 255 + fg_g;
            const int b   = (qBlue(px) * db) / 255 + fg_b;
            row[x]        = qRgb(r, g, b);
        }
    }
}
namespace
{

// ---- DjVu dynamic-loader support ----------------------------------------
// Minimal type definitions matching libdjvulibre ABI (stable since 3.5.x)

// miniexp_t is struct miniexp_s* in libdjvulibre; use void* for opaque handle
using djvu_miniexp_t = void *;
static const djvu_miniexp_t DJVU_MINIEXP_DUMMY
    = reinterpret_cast<void *>(static_cast<uintptr_t>(2));
static constexpr int DJVU_MSG_ERROR     = 0; // DDJVU_ERROR
static constexpr int DJVU_ROTATE_0      = 0;
static constexpr int DJVU_ROTATE_90     = 1;
static constexpr int DJVU_ROTATE_180    = 2;
static constexpr int DJVU_ROTATE_270    = 3;
static constexpr int DJVU_FMT_RGBMASK32 = 3; // DDJVU_FORMAT_RGBMASK32
static constexpr int DJVU_RENDER_COLOR  = 0;
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
} // namespace

void
Model::clearPageCache() noexcept
{
    std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    // for (auto &[_, entry] : m_page_cache)
    //     fz_drop_display_list(m_ctx, entry.display_list);

    m_page_lru_cache.clear();
}

void
Model::ensurePageCached(int pageno) noexcept
{
#ifndef NDEBUG
    qDebug() << "Model::ensurePageCached(): Ensuring page" << pageno
             << "is cached";
#endif
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
        if (m_page_lru_cache.has(pageno))
            return;
    }

    // Not cached, build it
    // Build outside the lock — expensive, but safe
    buildPageCache(pageno);
}

void
Model::buildPageCache_djvu(int pageno) noexcept
{
    auto &djvu = DjVuLib::get();
    if (!djvu.ok || !m_ddjvu_doc || m_ddjvu_ctx == nullptr)
        return;

    // DjVuLibre is NOT thread-safe for the same context — serialize
    std::lock_guard<std::mutex> lock(m_doc_mutex);

    void *page = djvu.page_create(m_ddjvu_doc, pageno);
    if (!page)
        return;

    // Pump until page is ready (DDJVU_JOB_OK = 2)
    DjVuMsg *msg;
    while (djvu.job_status(djvu.page_job(page)) < DJVU_JOB_OK)
    {
        msg = djvu.msg_wait(m_ddjvu_ctx);
        if (!msg || msg->m_any.tag == DJVU_MSG_ERROR)
        {
            djvu.job_release(page);
            return;
        }
        djvu.msg_pop(m_ddjvu_ctx);
    }

    if (djvu.job_status(djvu.page_job(page)) != DJVU_JOB_OK)
    {
        djvu.job_release(page);
        return;
    }

    const int djvu_rot = [&]() -> int
    {
        switch (((static_cast<int>(m_rotation) % 360) + 360) % 360)
        {
            case 90:
                return DJVU_ROTATE_90;
            case 180:
                return DJVU_ROTATE_180;
            case 270:
                return DJVU_ROTATE_270;
            default:
                return DJVU_ROTATE_0;
        }
    }();
    // Read pre-rotation dimensions first so the cache stores them consistently
    // with the MuPDF path (pre-rotation). Post-rotation values are used only
    // for the render buffer below.
    const int native_dpi = djvu.page_dpi(page);
    const int orig_pw_px = djvu.page_width(page);
    const int orig_ph_px = djvu.page_height(page);

    if (native_dpi <= 0 || orig_pw_px <= 0 || orig_ph_px <= 0)
    {
        djvu.job_release(page);
        return;
    }

    const float w_pts = static_cast<float>(orig_pw_px) / native_dpi * 72.0f;
    const float h_pts = static_cast<float>(orig_ph_px) / native_dpi * 72.0f;

    {
        std::lock_guard<std::mutex> dimlock(m_page_dim_mutex);
        m_page_dim_cache.set(pageno, w_pts, h_pts);
    }

    djvu.page_setrot(page, djvu_rot);

    // Post-rotation pixel dimensions drive the render buffer size.
    const int pw_px = djvu.page_width(page);
    const int ph_px = djvu.page_height(page);

    if (pw_px <= 0 || ph_px <= 0)
    {
        djvu.job_release(page);
        return;
    }

    // Render at m_zoom * m_dpi — same scale logic as the MuPDF path
    const float render_dpi = m_zoom * m_dpi * m_dpr;
    const float scale      = render_dpi / native_dpi;

    // Clamp pixel dimensions to keep the render buffer bounded. 32k×32k×4B
    // is ~4 GiB — well past any sane viewport; a bogus DPI or scale here
    // could otherwise overflow int and produce an undersized or negative
    // buffer that page_render happily writes past.
    static constexpr int MAX_RENDER_PX = 32768;
    const int rw_raw                   = static_cast<int>(pw_px * scale);
    const int rh_raw                   = static_cast<int>(ph_px * scale);
    if (rw_raw <= 0 || rh_raw <= 0 || rw_raw > MAX_RENDER_PX
        || rh_raw > MAX_RENDER_PX)
    {
        djvu.job_release(page);
        return;
    }
    const int rw = rw_raw;
    const int rh = rh_raw;

    DjVuRect prect{0, 0, static_cast<unsigned>(rw), static_cast<unsigned>(rh)};
    DjVuRect rrect = prect;

    // BGRA format maps cleanly to QImage::Format_RGB32.
    // Use 64-bit arithmetic for the buffer size so a large page cannot
    // overflow int (e.g. 25000 * 4 * 25000 = 2.5e9 wraps int).
    const int stride         = rw * 4;
    const qint64 buf_bytes64 = static_cast<qint64>(stride) * rh;
    if (buf_bytes64 <= 0
        || buf_bytes64 > static_cast<qint64>(std::numeric_limits<int>::max()))
    {
        djvu.job_release(page);
        return;
    }
    QByteArray buf(static_cast<int>(buf_bytes64), 0);

    void *fmt                   = nullptr;
    // DjVuLibre RGBMASK32: specify R/G/B masks and white background
    const unsigned int masks[3] = {0x00FF0000, 0x0000FF00, 0x000000FF};
    fmt = djvu.fmt_create(DJVU_FMT_RGBMASK32, 3,
                          const_cast<unsigned int *>(masks));
    djvu.fmt_roworder(fmt, 1); // top-to-bottom

    const int render_ok = djvu.page_render(page, DJVU_RENDER_COLOR, &prect,
                                           &rrect, fmt, stride, buf.data());

    djvu.fmt_release(fmt);
    djvu.job_release(page);

    if (!render_ok)
        return;

    QImage image(reinterpret_cast<const uchar *>(buf.constData()), rw, rh,
                 stride, QImage::Format_RGB32);
    image = image.copy(); // detach from buf's lifetime

    // Apply the same page.bg / page.fg tint the MuPDF path applies via
    // fz_tint_pixmap so DjVu pages honour the config colours too. Drop
    // the alpha byte to match the MuPDF path's `>> 8` convention, so
    // identity colours (0x000000FF / 0xFFFFFFFF) short-circuit inside
    // tintQImageRGB.
    tintQImageRGB(image, (m_fg_color >> 8) & 0xFFFFFF,
                  (m_bg_color >> 8) & 0xFFFFFF);

    // DjVu is overwhelmingly used for scanned documents, where the
    // high-contrast stretch does its best work — apply it here too.
    if (m_config.behavior.high_contrast)
    {
        applyHighContrastQImage(image,
                                m_config.behavior.high_contrast_black_point,
                                m_config.behavior.high_contrast_white_point);
    }

    image.setDotsPerMeterX(static_cast<int>(render_dpi * 1000.0 / 25.4));
    image.setDotsPerMeterY(static_cast<int>(render_dpi * 1000.0 / 25.4));
    image.setDevicePixelRatio(m_dpr);

    // DjVu has no PDF links or annotations — build a minimal cache entry
    // with the pre-rendered image stored as a display-list substitute.
    // We abuse PageCacheEntry by storing the image directly and handling
    // it in renderPageWithExtrasAsync.
    PageCacheEntry entry;
    entry.pageno       = pageno;
    entry.bounds       = {0, 0, w_pts, h_pts};
    entry.display_list = nullptr;
    entry.cached_image = image;

    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        if (!m_page_lru_cache.has(pageno))
            m_page_lru_cache.put(pageno, std::move(entry));
    }
}

void
Model::buildPageCache(int pageno) noexcept
{
    if (m_page_lru_cache.has(pageno))
        return;

    if (m_filetype == FileType::DJVU)
    {
        buildPageCache_djvu(pageno);
        return;
    }

    PageCacheEntry entry;
    std::vector<CachedLink> links;
    std::vector<CachedAnnotation> annotations;

    fz_context *ctx = cloneContext();
    if (!ctx)
    {
        qWarning() << "Failed to clone context for page cache";
        return;
    }

    fz_page *page          = nullptr;
    fz_display_list *dlist = nullptr;
    fz_device *list_dev    = nullptr;
    fz_link *head          = nullptr;
    bool success           = false;
    fz_rect bounds;

    std::lock_guard<std::mutex> lock(m_doc_mutex);

    fz_try(ctx)
    {
        page = fz_load_page(ctx, m_doc, pageno);
        if (!page)
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");

        const auto [w, h] = getPageDimensions(pageno);
        bounds            = (w >= 0 && h >= 0) ? fz_rect{0, 0, w, h}
                                               : fz_bound_page(ctx, page);

        dlist    = fz_new_display_list(ctx, bounds);
        list_dev = fz_new_list_device(ctx, dlist);

        fz_run_page(ctx, page, list_dev, fz_identity, nullptr);
        fz_close_device(ctx, list_dev);

        {
            const float w = bounds.x1 - bounds.x0;
            const float h = bounds.y1 - bounds.y0;

            std::lock_guard<std::mutex> lock(m_page_dim_mutex);
            m_page_dim_cache.set(pageno, w, h);
        }

        // Extract links and cache them
        if (m_config.links.enabled && supports_links())
        {
            head = fz_load_links(ctx, page);
            for (fz_link *link = head; link; link = link->next)
            {
                if (!link->uri || !link->uri[0])
                    continue;

                CachedLink cl;
                cl.rect = link->rect;
                cl.uri  = QString::fromUtf8(link->uri);

                // Store source location for all link types (where the link is
                // located)
                cl.source_loc.x = link->rect.x0;
                cl.source_loc.y = link->rect.y0;

                if (fz_is_external_link(ctx, link->uri))
                {
                    cl.type = BrowseLinkItem::LinkType::External;
                }
                else if (cl.uri.startsWith("#page"))
                {
                    float xp, yp;
                    fz_location loc
                        = fz_resolve_link(ctx, m_doc, link->uri, &xp, &yp);
                    cl.type        = BrowseLinkItem::LinkType::Page;
                    cl.target_page = loc.page;
                }
                else
                {
                    fz_link_dest dest
                        = fz_resolve_link_dest(ctx, m_doc, link->uri);
                    cl.type         = BrowseLinkItem::LinkType::Location;
                    cl.target_page  = dest.loc.page;
                    cl.target_loc.x = dest.x;
                    cl.target_loc.y = dest.y;
                    cl.zoom         = dest.zoom;
                }

                links.push_back(std::move(cl));
            }
        }

        pdf_page *pdfPage = pdf_page_from_fz_page(ctx, page);
        if (pdfPage)
        {
            float color[3]{0.0f, 0.0f, 0.0f};
            int n = 3;

            for (pdf_annot *annot = pdf_first_annot(ctx, pdfPage); annot;
                 annot            = pdf_next_annot(ctx, annot))
            {
                CachedAnnotation ca;
                ca.rect = pdf_bound_annot(ctx, annot);
                if (fz_is_infinite_rect(ca.rect) || fz_is_empty_rect(ca.rect))
                    continue;

                ca.type = pdf_annot_type(ctx, annot);

                // Only get text for annotations that typically have it
                const char *contents = pdf_annot_contents(ctx, annot);
                if (contents)
                    ca.text = QString::fromUtf8(contents);

                ca.index   = pdf_to_num(ctx, pdf_annot_obj(ctx, annot));
                ca.opacity = pdf_annot_opacity(ctx, annot);

                switch (ca.type)
                {
                    case PDF_ANNOT_POPUP:
                    case PDF_ANNOT_TEXT:
                        pdf_annot_color(ctx, annot, &n, color);
                        ca.color = QColor::fromRgbF(color[0], color[1],
                                                    color[2], ca.opacity);
                        break;

                    case PDF_ANNOT_HIGHLIGHT:
                    case PDF_ANNOT_UNDERLINE:
                    {
                        pdf_annot_color(ctx, annot, &n, color);
                        ca.color     = QColor::fromRgbF(color[0], color[1],
                                                        color[2], ca.opacity);
                        const int qc = pdf_annot_quad_point_count(ctx, annot);
                        ca.quad_rects.reserve(qc);
                        for (int qi = 0; qi < qc; ++qi)
                            ca.quad_rects.push_back(fz_rect_from_quad(
                                pdf_annot_quad_point(ctx, annot, qi)));
                    }
                    break;

                    case PDF_ANNOT_SQUARE:
                    {
                        pdf_annot_interior_color(ctx, annot, &n, color);
                        ca.color = QColor::fromRgbF(color[0], color[1],
                                                    color[2], ca.opacity);
                    }
                    break;

                    default:
                        continue;
                }

                annotations.push_back(std::move(ca));
            }
        }

        if (!links.empty())
            entry.links = std::make_shared<const std::vector<CachedLink>>(
                std::move(links));
        if (!annotations.empty())
            entry.annotations
                = std::make_shared<const std::vector<CachedAnnotation>>(
                    std::move(annotations));

        entry.display_list = dlist;
        entry.bounds       = bounds;
        entry.pageno       = pageno;
        success            = true;
    }
    fz_always(ctx)
    {
        fz_drop_link(ctx, head);
        fz_drop_device(ctx, list_dev);
        fz_drop_page(ctx, page);
        if (!success && dlist)
            fz_drop_display_list(ctx, dlist);
    }
    fz_catch(ctx)
    {
        qWarning() << "Failed to build page cache for page" << pageno << ":"
                   << fz_caught_message(ctx);
    }

    if (!success)
    {
        fz_drop_context(ctx);
        return;
    }

    // Cache the display list and links
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
        if (!m_page_lru_cache.has(pageno))
            m_page_lru_cache.put(pageno, std::move(entry));
        else
            fz_drop_display_list(ctx, dlist);
    }

    fz_drop_context(ctx);
}

// Returns page dimensions in points (1/72 inch) if known, otherwise (-1,
// -1)
std::tuple<float, float>
Model::getPageDimensions(int pageno) const noexcept
{
    std::lock_guard<std::mutex> lock(m_page_dim_mutex);
    if (pageno < 0 || pageno >= m_page_count || m_page_dim_cache.known.empty()
        || pageno >= (int)m_page_dim_cache.known.size()
        || !m_page_dim_cache.known[pageno])
    {
        return {-1.0f, -1.0f};
    }
    return {m_page_dim_cache.dimensions[pageno].width_pts,
            m_page_dim_cache.dimensions[pageno].height_pts};
}

fz_point
Model::toPDFSpace(int pageno, QPointF pixelPos) const noexcept
{
    fz_point p{0, 0};

    const auto [width_pts, height_pts] = getPageDimensions(pageno);

    // Create bounds rect from cached dimensions
    fz_rect bounds = {0, 0, width_pts, height_pts};

    // Re-create the same transform used in rendering
    const float scale = m_zoom * m_dpr * m_dpi;
    fz_matrix transform
        = buildRenderTransform(bounds, scale, m_rotation, m_flip_h, m_flip_v);

    // Adjust for Qt's Device Pixel Ratio
    float physicalX = pixelPos.x() * m_dpr;
    float physicalY = pixelPos.y() * m_dpr;

    p.x = physicalX;
    p.y = physicalY;

    // Invert transformation to get PDF space coordinates
    fz_matrix inv_transform = fz_invert_matrix(transform);
    p                       = fz_transform_point(p, inv_transform);

    return p;
}

QPointF
Model::toPixelSpace(int pageno, fz_point p) const noexcept
{
    // Get cached page dimensions instead of loading the page
    const auto [width_pts, height_pts] = getPageDimensions(pageno);

    // Create bounds rect from cached dimensions
    fz_rect bounds = {0, 0, width_pts, height_pts};

    // Re-create the same transform used in rendering
    const float scale = m_zoom * m_dpr * m_dpi;
    fz_matrix transform
        = buildRenderTransform(bounds, scale, m_rotation, m_flip_h, m_flip_v);

    // Transform point to device space
    fz_point device_point = fz_transform_point(p, transform);
    float localX          = device_point.x;
    float localY          = device_point.y;

    // Adjust for Qt's Device Pixel Ratio
    return QPointF(localX / m_dpr, localY / m_dpr);
}

void
Model::invalidatePageCache(int pageno) noexcept
{
    std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    m_page_lru_cache.remove(pageno);
    m_text_cache.remove(pageno);
}

void
Model::invalidatePageCaches() noexcept
{
    std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    m_page_lru_cache.clear();
    m_text_cache.clear();
    m_has_text_layer = -1;
    m_stext_page_cache.clear();

    std::lock_guard<std::mutex> lk(m_page_dim_mutex);
    m_page_dim_cache.reset(m_page_count);
}

// Returns {page_to_dev, dev_to_page}, or {identity, identity} on failure
std::pair<fz_matrix, fz_matrix>
Model::buildPageTransforms(int pageno) const noexcept
{
    const fz_matrix identity = fz_identity;
    fz_rect bounds;

    fz_try(m_ctx)
    {
        auto [w, h] = getPageDimensions(pageno);
        if (w < 0 || h < 0)
        {
            std::lock_guard<std::mutex> lock(m_doc_mutex);
            fz_page *page = fz_load_page(m_ctx, m_doc, pageno);
            bounds        = fz_bound_page(m_ctx, page);
            fz_drop_page(m_ctx, page);
        }
        else
        {
            bounds = {0, 0, w, h};
        }
    }
    fz_catch(m_ctx)
    {
        return {identity, identity};
    }

    const float scale = logicalScale();
    fz_matrix page_to_dev
        = buildPageToDevMatrix(bounds, scale, m_rotation, m_flip_h, m_flip_v);
    return {page_to_dev, fz_invert_matrix(page_to_dev)};
}

Model::ContentBBox
Model::contentBBox(int pageno) noexcept
{
    // Fallback for the non-PDF paths: image files and DjVu have no vector
    // "content region" concept — return the full page rect so smart-fit
    // degrades gracefully to plain fit.
    const auto dim              = page_dimension_pts(pageno);
    const ContentBBox page_rect = {0.0f, 0.0f, dim.width_pts, dim.height_pts};

    if (m_is_image || m_filetype == FileType::DJVU || !m_ctx || !m_doc
        || pageno < 0 || pageno >= m_page_count)
    {
        return page_rect;
    }

    {
        std::lock_guard<std::mutex> lock(m_content_bbox_mutex);
        auto it = m_content_bbox_cache.find(pageno);
        if (it != m_content_bbox_cache.end())
            return it->second;
    }

    // Run a MuPDF bbox device over the page to accumulate the tight bounds
    // of every draw call. Serialise against other MuPDF operations on the
    // same document since the fz_context is not shareable across threads.
    std::lock_guard<std::mutex> doc_lock(m_doc_mutex);

    fz_page *page  = nullptr;
    fz_device *dev = nullptr;
    fz_rect bbox   = fz_empty_rect;

    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, pageno);
        dev  = fz_new_bbox_device(m_ctx, &bbox);
        fz_run_page(m_ctx, page, dev, fz_identity, nullptr);
        fz_close_device(m_ctx, dev);
    }
    fz_always(m_ctx)
    {
        if (dev)
            fz_drop_device(m_ctx, dev);
        if (page)
            fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        // On error just fall back to the full page rect below.
        bbox = fz_empty_rect;
    }

    ContentBBox result = page_rect;
    if (bbox.x1 > bbox.x0 && bbox.y1 > bbox.y0)
    {
        // Clamp to the page rect so quirky content that reports coords
        // outside the mediabox doesn't produce weird fit factors.
        result.x0 = std::max(0.0f, bbox.x0);
        result.y0 = std::max(0.0f, bbox.y0);
        result.x1 = std::min(dim.width_pts, bbox.x1);
        result.y1 = std::min(dim.height_pts, bbox.y1);
        if (result.isEmpty())
            result = page_rect;
    }

    {
        std::lock_guard<std::mutex> lock(m_content_bbox_mutex);
        m_content_bbox_cache[pageno] = result;
    }
    return result;
}
