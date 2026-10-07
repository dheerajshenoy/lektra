#include "BrowseLinkItem.hpp"
#include "Commands/TextHighlightAnnotationCommand.hpp"
#include "Config.hpp"
#include "ImageAnimation.hpp"
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

// Apply the high-contrast tone stretch to a raw sample buffer. Works for
// any component layout (grayscale, RGB, RGBA) — alpha channels are
// stretched too, but since alpha is either 0 or 255 for opaque pages, the
// stretch is a no-op on them.
static void
applyHighContrastSamples(unsigned char *samples, size_t n_bytes,
                         const unsigned char lut[256]) noexcept
{
    for (size_t i = 0; i < n_bytes; ++i)
        samples[i] = lut[samples[i]];
}

// ============================================================================
// Image Tracking Device for Selective Inversion
// ============================================================================
// This device wraps a draw device and records bounding boxes of all images
// rendered to the page. After inversion, these regions can be restored.

struct ImageRect
{
    fz_irect bbox;   // Pixel bounding box of the image
    fz_image *image; // Reference to the image (for re-rendering)
    fz_matrix ctm;   // Transform matrix used for this image
    float alpha;     // Alpha value
    fz_color_params color_params;
};

struct fz_image_tracker_device
{
    fz_device super;          // Must be first - base device
    fz_device *target;        // The actual draw device to forward calls to
    fz_matrix page_transform; // Page transform to convert to pixel coords
    ImageRect *rects;         // Dynamic array of image rectangles
    int rect_count;
    int rect_cap;
};

static void
image_tracker_fill_image(fz_context *ctx, fz_device *dev_, fz_image *image,
                         fz_matrix ctm, float alpha,
                         fz_color_params color_params)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);

    // Calculate the bounding box of this image in pixel coordinates
    // Images are rendered into unit rect [0,0,1,1] transformed by ctm
    fz_rect img_rect = fz_transform_rect(fz_unit_rect, ctm);
    // fz_rect transformed = fz_transform_rect(img_rect, dev->page_transform);
    fz_irect bbox    = fz_round_rect(img_rect);

    // Grow array if needed
    if (dev->rect_count >= dev->rect_cap)
    {
        int new_cap     = dev->rect_cap == 0 ? 16 : dev->rect_cap * 2;
        auto *new_rects = static_cast<ImageRect *>(
            fz_realloc(ctx, dev->rects, new_cap * sizeof(ImageRect)));
        dev->rects    = new_rects;
        dev->rect_cap = new_cap;
    }

    // Store image info for later re-rendering
    ImageRect &ir   = dev->rects[dev->rect_count++];
    ir.bbox         = bbox;
    ir.image        = fz_keep_image(ctx, image); // Keep reference
    ir.ctm          = ctm;
    ir.alpha        = alpha;
    ir.color_params = color_params;

    // Forward to target device
    if (dev->target)
        fz_fill_image(ctx, dev->target, image, ctm, alpha, color_params);
}

// Forward all other calls to target device
static void
image_tracker_fill_path(fz_context *ctx, fz_device *dev_, const fz_path *path,
                        int even_odd, fz_matrix ctm, fz_colorspace *colorspace,
                        const float *color, float alpha, fz_color_params cp)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_fill_path(ctx, dev->target, path, even_odd, ctm, colorspace, color,
                     alpha, cp);
}

static void
image_tracker_stroke_path(fz_context *ctx, fz_device *dev_, const fz_path *path,
                          const fz_stroke_state *stroke, fz_matrix ctm,
                          fz_colorspace *colorspace, const float *color,
                          float alpha, fz_color_params cp)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_stroke_path(ctx, dev->target, path, stroke, ctm, colorspace, color,
                       alpha, cp);
}

static void
image_tracker_clip_path(fz_context *ctx, fz_device *dev_, const fz_path *path,
                        int even_odd, fz_matrix ctm, fz_rect scissor)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_clip_path(ctx, dev->target, path, even_odd, ctm, scissor);
}

static void
image_tracker_clip_stroke_path(fz_context *ctx, fz_device *dev_,
                               const fz_path *path,
                               const fz_stroke_state *stroke, fz_matrix ctm,
                               fz_rect scissor)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_clip_stroke_path(ctx, dev->target, path, stroke, ctm, scissor);
}

static void
image_tracker_fill_text(fz_context *ctx, fz_device *dev_, const fz_text *text,
                        fz_matrix ctm, fz_colorspace *colorspace,
                        const float *color, float alpha, fz_color_params cp)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_fill_text(ctx, dev->target, text, ctm, colorspace, color, alpha, cp);
}

static void
image_tracker_stroke_text(fz_context *ctx, fz_device *dev_, const fz_text *text,
                          const fz_stroke_state *stroke, fz_matrix ctm,
                          fz_colorspace *colorspace, const float *color,
                          float alpha, fz_color_params cp)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_stroke_text(ctx, dev->target, text, stroke, ctm, colorspace, color,
                       alpha, cp);
}

static void
image_tracker_clip_text(fz_context *ctx, fz_device *dev_, const fz_text *text,
                        fz_matrix ctm, fz_rect scissor)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_clip_text(ctx, dev->target, text, ctm, scissor);
}

static void
image_tracker_clip_stroke_text(fz_context *ctx, fz_device *dev_,
                               const fz_text *text,
                               const fz_stroke_state *stroke, fz_matrix ctm,
                               fz_rect scissor)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_clip_stroke_text(ctx, dev->target, text, stroke, ctm, scissor);
}

static void
image_tracker_ignore_text(fz_context *ctx, fz_device *dev_, const fz_text *text,
                          fz_matrix ctm)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_ignore_text(ctx, dev->target, text, ctm);
}

static void
image_tracker_fill_shade(fz_context *ctx, fz_device *dev_, fz_shade *shade,
                         fz_matrix ctm, float alpha, fz_color_params cp)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_fill_shade(ctx, dev->target, shade, ctm, alpha, cp);
}

static void
image_tracker_fill_image_mask(fz_context *ctx, fz_device *dev_, fz_image *image,
                              fz_matrix ctm, fz_colorspace *colorspace,
                              const float *color, float alpha,
                              fz_color_params cp)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_fill_image_mask(ctx, dev->target, image, ctm, colorspace, color,
                           alpha, cp);
}

static void
image_tracker_clip_image_mask(fz_context *ctx, fz_device *dev_, fz_image *image,
                              fz_matrix ctm, fz_rect scissor)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_clip_image_mask(ctx, dev->target, image, ctm, scissor);
}

static void
image_tracker_pop_clip(fz_context *ctx, fz_device *dev_)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_pop_clip(ctx, dev->target);
}

static void
image_tracker_begin_mask(fz_context *ctx, fz_device *dev_, fz_rect area,
                         int luminosity, fz_colorspace *colorspace,
                         const float *bc, fz_color_params cp)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_begin_mask(ctx, dev->target, area, luminosity, colorspace, bc, cp);
}

static void
image_tracker_end_mask(fz_context *ctx, fz_device *dev_, fz_function *fn)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_end_mask_tr(ctx, dev->target, fn);
}

static void
image_tracker_begin_group(fz_context *ctx, fz_device *dev_, fz_rect area,
                          fz_colorspace *cs, int isolated, int knockout,
                          int blendmode, float alpha)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_begin_group(ctx, dev->target, area, cs, isolated, knockout,
                       blendmode, alpha);
}

static void
image_tracker_end_group(fz_context *ctx, fz_device *dev_)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_end_group(ctx, dev->target);
}

static int
image_tracker_begin_tile(fz_context *ctx, fz_device *dev_, fz_rect area,
                         fz_rect view, float xstep, float ystep, fz_matrix ctm,
                         int id, int doc_id)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        return fz_begin_tile_tid(ctx, dev->target, area, view, xstep, ystep,
                                 ctm, id, doc_id);
    return 0;
}

static void
image_tracker_end_tile(fz_context *ctx, fz_device *dev_)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_end_tile(ctx, dev->target);
}

static void
image_tracker_close_device(fz_context *ctx, fz_device *dev_)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);
    if (dev->target)
        fz_close_device(ctx, dev->target);
}

static void
image_tracker_drop_device(fz_context *ctx, fz_device *dev_)
{
    auto *dev = reinterpret_cast<fz_image_tracker_device *>(dev_);

    // Drop all kept image references
    for (int i = 0; i < dev->rect_count; ++i)
        fz_drop_image(ctx, dev->rects[i].image);

    fz_free(ctx, dev->rects);
    // Note: target device is dropped separately by caller
}

static fz_device *
new_image_tracker_device(fz_context *ctx, fz_device *target,
                         fz_matrix page_transform)
{
    auto *dev = fz_new_derived_device(ctx, fz_image_tracker_device);

    dev->super.close_device = image_tracker_close_device;
    dev->super.drop_device  = image_tracker_drop_device;

    dev->super.fill_path        = image_tracker_fill_path;
    dev->super.stroke_path      = image_tracker_stroke_path;
    dev->super.clip_path        = image_tracker_clip_path;
    dev->super.clip_stroke_path = image_tracker_clip_stroke_path;

    dev->super.fill_text        = image_tracker_fill_text;
    dev->super.stroke_text      = image_tracker_stroke_text;
    dev->super.clip_text        = image_tracker_clip_text;
    dev->super.clip_stroke_text = image_tracker_clip_stroke_text;
    dev->super.ignore_text      = image_tracker_ignore_text;

    dev->super.fill_shade      = image_tracker_fill_shade;
    dev->super.fill_image      = image_tracker_fill_image;
    dev->super.fill_image_mask = image_tracker_fill_image_mask;
    dev->super.clip_image_mask = image_tracker_clip_image_mask;

    dev->super.pop_clip    = image_tracker_pop_clip;
    dev->super.begin_mask  = image_tracker_begin_mask;
    dev->super.end_mask    = image_tracker_end_mask;
    dev->super.begin_group = image_tracker_begin_group;
    dev->super.end_group   = image_tracker_end_group;
    dev->super.begin_tile  = image_tracker_begin_tile;
    dev->super.end_tile    = image_tracker_end_tile;

    dev->target         = target;
    dev->page_transform = page_transform;
    dev->rects          = nullptr;
    dev->rect_count     = 0;
    dev->rect_cap       = 0;

    return reinterpret_cast<fz_device *>(dev);
}

// Restores the original pixels of each tracked image rect. The image is not
// re-drawn on its own: that would skip the page's graphics state (clip paths,
// blend modes, transparency groups) and visibly corrupt images that depend on
// it. Instead the page's display list is replayed into a pixmap covering just
// that rect, so every pixel comes out exactly as the plain render produced it.
static void
restore_image_regions(fz_context *ctx, fz_pixmap *pix,
                      fz_image_tracker_device *tracker, fz_display_list *dlist,
                      fz_matrix transform, fz_colorspace *colorspace)
{
    if (tracker->rect_count == 0)
        return;

    const int n = fz_pixmap_components(ctx, pix);

    for (int i = 0; i < tracker->rect_count; ++i)
    {
        const ImageRect &ir = tracker->rects[i];

        const fz_irect clipped
            = fz_intersect_irect(ir.bbox, fz_pixmap_bbox(ctx, pix));
        if (fz_is_empty_irect(clipped))
            continue;

        fz_pixmap *sub      = nullptr;
        fz_device *draw_dev = nullptr;

        fz_try(ctx)
        {
            sub = fz_new_pixmap_with_bbox(ctx, colorspace, clipped, nullptr, 0);
            fz_clear_pixmap_with_value(ctx, sub, 255);

            draw_dev = fz_new_draw_device(ctx, fz_identity, sub);
            fz_run_display_list(ctx, dlist, draw_dev, transform,
                                fz_rect_from_irect(clipped), nullptr);
            fz_close_device(ctx, draw_dev);

            const int sub_stride       = fz_pixmap_stride(ctx, sub);
            const int pix_stride       = fz_pixmap_stride(ctx, pix);
            unsigned char *sub_samples = fz_pixmap_samples(ctx, sub);
            unsigned char *pix_samples = fz_pixmap_samples(ctx, pix);
            const int pix_x0           = fz_pixmap_x(ctx, pix);
            const int pix_y0           = fz_pixmap_y(ctx, pix);

            for (int y = clipped.y0; y < clipped.y1; ++y)
            {
                unsigned char *src
                    = sub_samples + (y - clipped.y0) * sub_stride;
                unsigned char *dst = pix_samples + (y - pix_y0) * pix_stride
                                     + (clipped.x0 - pix_x0) * n;
                std::memcpy(dst, src, (clipped.x1 - clipped.x0) * n);
            }
        }
        fz_always(ctx)
        {
            fz_drop_device(ctx, draw_dev);
            fz_drop_pixmap(ctx, sub);
        }
        fz_catch(ctx)
        {
            fz_warn(ctx, "Failed to restore image region: %s",
                    fz_caught_message(ctx));
        }
    }
}

Model::RenderJob
Model::createRenderJob(int pageno) const noexcept
{
    RenderJob job;
    job.filepath = m_filepath;
    job.pageno   = pageno;
    job.dpr      = m_dpr;
    job.dpi      = m_dpi;
    job.zoom = m_zoom * m_dpr * m_dpi; // DPI resolution for fz_transform_page
                                       // (divides by 72 internally)
    job.rotation     = m_rotation;
    job.invert_color = m_invert_color;
    job.flip_h       = m_flip_h;
    job.flip_v       = m_flip_v;
    job.colorspace   = m_colorspace;
    return job;
}

QImage
Model::requestImageRender(bool highQuality) noexcept
{
    if (!m_is_image)
        return {};

    // 1. Handle Animated Images
    if (m_is_animated)
    {
        if (!m_movie)
            return {};
        QImage frame = m_movie->currentImage();
        if (frame.isNull())
            return {};
        if (m_rotation != 0 || m_flip_h || m_flip_v)
        {
            QTransform trans;
            trans.rotate(m_rotation);
            if (m_flip_h)
                trans.scale(-1.0, 1.0);
            if (m_flip_v)
                trans.scale(1.0, -1.0);
            frame = frame.transformed(trans, Qt::SmoothTransformation);
        }
        frame.setDevicePixelRatio(m_dpr);
        if (m_invert_color)
            frame.invertPixels();
        return frame;
    }

    if (m_image_cache.isNull())
        return {};

    // 2. Determine base dimensions after rotation
    // Use QTransform to see how dimensions swap at 90/270 degrees
    QTransform rotationTransform;
    rotationTransform.rotate(m_rotation);

    // Calculate the size the image would be if it were scaled at 100% zoom
    // but rotated
    QRectF rotatedRect
        = rotationTransform.mapRect(QRectF(m_image_cache.rect()));
    const double rotatedWidth  = rotatedRect.width();
    const double rotatedHeight = rotatedRect.height();

    // 3. Calculate target dimensions with zoom and DPR
    int rw = std::max(1, (int)(rotatedWidth * m_zoom * m_dpr));
    int rh = std::max(1, (int)(rotatedHeight * m_zoom * m_dpr));

    // 4. Capping Logic (Remains largely the same, but uses new rw/rh)
    constexpr int MAX_RENDER_EDGE      = 16384;
    constexpr double MAX_RENDER_PIXELS = 64.0 * 1024.0 * 1024.0;

    const double pixel_count
        = static_cast<double>(rw) * static_cast<double>(rh);
    double cap_scale = 1.0;

    if (rw > MAX_RENDER_EDGE || rh > MAX_RENDER_EDGE)
    {
        cap_scale = std::min((double)MAX_RENDER_EDGE / rw,
                             (double)MAX_RENDER_EDGE / rh);
    }

    if (pixel_count > MAX_RENDER_PIXELS)
    {
        cap_scale
            = std::min(cap_scale, std::sqrt(MAX_RENDER_PIXELS / pixel_count));
    }

    if (cap_scale < 1.0)
    {
        rw = std::max(1, static_cast<int>(rw * cap_scale));
        rh = std::max(1, static_cast<int>(rh * cap_scale));
    }

    // 5. Perform Transformation
    const Qt::TransformationMode mode
        = highQuality ? Qt::SmoothTransformation : Qt::FastTransformation;

    // To prevent double-processing, we combine scale and rotation into one
    // transform This is more efficient than calling .scaled() then
    // .transformed()
    QTransform finalTransform;
    finalTransform.rotate(m_rotation);
    if (m_flip_h)
        finalTransform.scale(-1.0, 1.0);
    if (m_flip_v)
        finalTransform.scale(1.0, -1.0);

    // Calculate the actual scale needed to reach our capped rw/rh from the
    // original source We scale the original cache pixels to fit the
    // calculated bounding box
    QImage result = m_image_cache.transformed(finalTransform, mode)
                        .scaled(rw, rh, Qt::IgnoreAspectRatio, mode);

    result.setDevicePixelRatio(m_dpr);

    if (m_invert_color)
        result.invertPixels();

    return result;
}

void
Model::requestPageRender(
    const RenderJob &job,
    const std::function<void(PageRenderResult)> &callback) noexcept
{
#ifndef NDEBUG
    qDebug() << "Model::requestPageRender(): Requesting render for page"
             << job.pageno;
#endif

    auto watcher = new QFutureWatcher<PageRenderResult>(this);
    connect(watcher, &QFutureWatcher<PageRenderResult>::finished, this,
            [this, watcher, callback, job]()
    {
        // TODO: This is a hack, this shouldn't actually happen, check why
        // it happens, but for now, just guard against invalid futures.
        if (!watcher->future().isValid())
        {
            watcher->deleteLater();
            return;
        }

        PageRenderResult result = watcher->result();
        watcher->deleteLater();

        if (m_render_cancelled.load())
            return;

        if (callback)
            callback(result);

        if (supports_links() && m_detect_url_links)
        {
            const int pageno = job.pageno;
            QFuture<void> _  = QtConcurrent::run([this, job, pageno]()
            {
                auto urlLinks = detectUrlLinksForPage(job);
                if (!urlLinks.empty())
                    emit urlLinksReady(pageno, std::move(urlLinks));
            });
        }
    });

    // In requestPageRender - worker lambda:
    auto future = QtConcurrent::run([this, job]() -> PageRenderResult
    {
        m_active_renders.fetch_add(1, std::memory_order_relaxed);

        struct Guard
        {
            Model *m;
            ~Guard()
            {
                if (m->m_active_renders.fetch_sub(1, std::memory_order_acq_rel)
                    == 1)
                    m->m_renders_cv.notify_all();
            }
        } guard{this};

        if (m_render_cancelled.load(std::memory_order_acquire))
            return {};
        ensurePageCached(job.pageno);
        if (m_render_cancelled.load(std::memory_order_acquire))
            return {};
        return renderPageWithExtrasAsync(job);
    });

    watcher->setFuture(future); // no synchronizer, just the watcher
}

Model::PageRenderResult
Model::renderPageWithExtrasAsync(const RenderJob &job) noexcept
{
    PageRenderResult result;

    if (m_filetype == FileType::DJVU)
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
        const PageCacheEntry *entry = m_page_lru_cache.get(job.pageno);
        if (!entry || entry->cached_image.isNull())
        {
            qWarning() << "DjVu page not cached:" << job.pageno;
            return result;
        }

        QImage image = entry->cached_image;

        if (job.invert_color)
            image.invertPixels();

        if (job.flip_h || job.flip_v)
        {
            Qt::Orientations o;
            if (job.flip_h)
                o |= Qt::Horizontal;
            if (job.flip_v)
                o |= Qt::Vertical;

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            image = image.flipped(o);
#else
            image = image.mirrored(o == Qt::Horizontal, o == Qt::Vertical);
#endif
        }

        image.setDotsPerMeterX(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDotsPerMeterY(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDevicePixelRatio(job.dpr);

        result.image = std::move(image);
        // DjVu has no links or annotations — result.links/annotations stay
        // empty
        return result;
    }

    fz_context *ctx = cloneContext();
    if (!ctx)
        return result;

    fz_display_list *dlist = nullptr;
    fz_rect bounds;
    std::vector<CachedLink> links;
    std::vector<CachedAnnotation> annotations;

    fz_try(ctx)
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);

        const PageCacheEntry *entry = m_page_lru_cache.find(job.pageno);
        if (!entry)
        {
            qWarning() << "Model::PageRenderResult() Page not cached:"
                       << job.pageno;
            return result;
        }

        if (!entry->display_list)
        {
            qWarning() << "Model::PageRenderResult() Missing display list for:"
                       << job.pageno;
            return result;
        }

        // Increment reference count so the display list stays valid
        dlist  = fz_keep_display_list(ctx, entry->display_list);
        bounds = entry->bounds;

        links       = entry->links;
        annotations = entry->annotations;
    }
    fz_always(ctx)
    {
        // We will drop the context at the end of this function, which will
        // also drop the display list reference we just kept. If we failed
        // to keep the display list, dropping a null pointer is safe.
    }
    fz_catch(ctx)
    {
        qWarning() << "Failed to retrieve page cache for rendering:"
                   << job.pageno << ":" << fz_caught_message(ctx);
        fz_drop_context(ctx);
        return result;
    }

    fz_link *head      = nullptr;
    fz_pixmap *pix     = nullptr;
    fz_device *dev     = nullptr;
    fz_device *tracker = nullptr;

    fz_try(ctx)
    {
        fz_matrix transform = buildRenderTransform(
            bounds, job.zoom, job.rotation, job.flip_h, job.flip_v);
        fz_rect transformed    = fz_transform_rect(bounds, transform);
        const fz_irect fullBox = fz_round_rect(transformed);
        fz_irect bbox          = fullBox;

        // A page that would be an enormous bitmap (deep zoom) is rendered
        // only where it is being looked at. Memory and time then depend on
        // the window size rather than on the zoom level.
        constexpr double MAX_FULL_PAGE_PIXELS = 16.0 * 1024.0 * 1024.0;
        constexpr int REGION_ALIGN            = 64;
        const int fullW                       = fullBox.x1 - fullBox.x0;
        const int fullH                       = fullBox.y1 - fullBox.y0;
        bool partial                          = false;
        if (job.has_clip && fullW > 0 && fullH > 0
            && static_cast<double>(fullW) * fullH > MAX_FULL_PAGE_PIXELS)
        {
            auto alignDown = [](int v)
            {
                return (v / REGION_ALIGN) * REGION_ALIGN;
            };
            auto alignUp = [](int v)
            {
                return ((v + REGION_ALIGN - 1) / REGION_ALIGN) * REGION_ALIGN;
            };
            fz_irect r;
            r.x0 = fullBox.x0
                   + alignDown(static_cast<int>(
                       std::floor(job.clip_frac.left() * fullW)));
            r.y0 = fullBox.y0
                   + alignDown(static_cast<int>(
                       std::floor(job.clip_frac.top() * fullH)));
            r.x1 = fullBox.x0
                   + alignUp(static_cast<int>(
                       std::ceil(job.clip_frac.right() * fullW)));
            r.y1 = fullBox.y0
                   + alignUp(static_cast<int>(
                       std::ceil(job.clip_frac.bottom() * fullH)));
            r    = fz_intersect_irect(r, fullBox);
            if (!fz_is_empty_irect(r))
            {
                bbox    = r;
                partial = true;
            }
        }

        // // --- Render page to QImage ---
        pix = fz_new_pixmap_with_bbox(ctx, job.colorspace, bbox, nullptr, 0);
        fz_clear_pixmap_with_value(ctx, pix, 255);

        dev = fz_new_draw_device(ctx, fz_identity, pix);

        if (m_config.behavior.dont_invert_images && supports_image_blocks())
        {
            tracker = new_image_tracker_device(ctx, dev, transform);

            fz_run_display_list(ctx, dlist, tracker, transform,
                                fz_rect_from_irect(bbox), nullptr);
        }
        else
        {
            fz_run_display_list(ctx, dlist, dev, transform,
                                fz_rect_from_irect(bbox), nullptr);
        }

        const int fg = (m_fg_color >> 8) & 0xFFFFFF;
        const int bg = (m_bg_color >> 8) & 0xFFFFFF;

        if (fg != 0 || bg != 0)
            fz_tint_pixmap(ctx, pix, fg, bg);

        if (job.invert_color)
            fz_invert_pixmap(ctx, pix);

        // High-contrast tone stretch — applied after invert so "dark mode
        // + high contrast" is a legitimate combination. LUT-based, single
        // pass over the sample buffer, ~1ms per page. Independent of
        // invert: users can turn on high contrast without dark mode when
        // reading grayish scans.
        const bool high_contrast_active = m_config.behavior.high_contrast;
        if (high_contrast_active)
        {
            unsigned char lut[256];
            buildHighContrastLUT(lut,
                                 m_config.behavior.high_contrast_black_point,
                                 m_config.behavior.high_contrast_white_point);
            const size_t nbytes
                = static_cast<size_t>(fz_pixmap_stride(ctx, pix))
                  * static_cast<size_t>(fz_pixmap_height(ctx, pix));
            applyHighContrastSamples(fz_pixmap_samples(ctx, pix), nbytes, lut);
        }

        // Image protection covers both invert and high contrast: if the
        // user has asked for images to be preserved, restore their
        // original pixels after any post-processing pass has touched them.
        if ((job.invert_color || high_contrast_active)
            && m_config.behavior.dont_invert_images && supports_image_blocks()
            && tracker)
        {
            restore_image_regions(
                ctx, pix, reinterpret_cast<fz_image_tracker_device *>(tracker),
                dlist, transform, m_colorspace);
        }

        // fz_gamma_pixmap(ctx, pix, 1.0f);

        const int width  = fz_pixmap_width(ctx, pix);
        const int height = fz_pixmap_height(ctx, pix);
        const int n      = fz_pixmap_components(ctx, pix);
        const int stride = fz_pixmap_stride(ctx, pix);

        unsigned char *samples = fz_pixmap_samples(ctx, pix);
        if (!samples)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "No pixmap samples");
        }

        QImage::Format fmt;
        switch (n)
        {
            case 1:
                fmt = QImage::Format_Grayscale8;
                break;
            case 3:
                fmt = QImage::Format_RGB888;
                break;
            case 4:
                fmt = QImage::Format_RGBA8888;
                break;

            default:
            {
                fz_throw(ctx, FZ_ERROR_GENERIC,
                         "Unsupported pixmap component count");
            }
        }

        // Construct with the MuPDF stride so Qt copies each scanline
        // correctly regardless of its own alignment padding.
        QImage image(samples, width, height, stride, fmt);
        image
            = image.copy(); // detach from MuPDF's buffer before fz_drop_pixmap

        image.setDotsPerMeterX(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDotsPerMeterY(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDevicePixelRatio(job.dpr);
        result.image = image;
        if (partial)
        {
            result.partial   = true;
            result.full_size = QSize(fullW, fullH);
            result.region    = QRect(bbox.x0 - fullBox.x0, bbox.y0 - fullBox.y0,
                                     width, height);
        }

        // --- Extract links ---
        const float scale = m_inv_dpr;
        result.links.reserve(links.size());
        for (const auto &link : links)
        {
            if (link.uri.isEmpty())
                continue;
            fz_rect r = fz_transform_rect(link.rect, transform);
            QRectF qtRect(r.x0 * scale, r.y0 * scale, (r.x1 - r.x0) * scale,
                          (r.y1 - r.y0) * scale);

            RenderLink renderLink;
            renderLink.rect       = qtRect;
            renderLink.uri        = link.uri;
            renderLink.type       = link.type;
            renderLink.boundary   = m_link_show_boundary;
            renderLink.source_loc = BrowseLinkItem::PageLocation{
                link.source_loc.x, link.source_loc.y, 0.0f};

            if (link.type == BrowseLinkItem::LinkType::Page)
            {
                renderLink.target_page = link.target_page;
            }

            if (link.type == BrowseLinkItem::LinkType::Location)
            {
                renderLink.target_page = link.target_page;
                renderLink.target_loc  = BrowseLinkItem::PageLocation{
                    link.target_loc.x, link.target_loc.y, link.zoom};
            }

            result.links.push_back(std::move(renderLink));
        }

        // fz_stext_page *stext_page = nullptr;
        // if (m_detect_url_links)
        // {
        //     text_page = fz_load_page(ctx, m_doc, job.pageno);
        //     if (text_page)
        //         stext_page
        //             = fz_new_stext_page_from_page(ctx, text_page,
        //             nullptr);
        //
        //     if (stext_page)
        //     {
        //         const QRegularExpression &urlRe = m_url_link_re;
        //
        //         auto hasIntersectingLink = [&](const fz_rect &r) -> bool
        //         {
        //             for (const auto &link : links)
        //             {
        //                 const fz_rect lr = link.rect;
        //                 if (r.x1 < lr.x0 || r.x0 > lr.x1 || r.y1 < lr.y0
        //                     || r.y0 > lr.y1)
        //                     continue;
        //                 return true;
        //             }
        //             return false;
        //         };
        //
        //         for (fz_stext_block *b = stext_page->first_block; b;
        //              b                 = b->next)
        //         {
        //             if (b->type != FZ_STEXT_BLOCK_TEXT)
        //                 continue;
        //
        //             for (fz_stext_line *line = b->u.t.first_line; line;
        //                  line                = line->next)
        //             {
        //                 QString lineText;
        //                 lineText.reserve(256);
        //                 for (fz_stext_char *ch = line->first_char; ch;
        //                      ch                = ch->next)
        //                 {
        //                     lineText.append(QChar::fromUcs4(ch->c));
        //                 }
        //
        //                 if (lineText.isEmpty())
        //                     continue;
        //
        //                 QRegularExpressionMatchIterator it
        //                     = urlRe.globalMatch(lineText);
        //                 while (it.hasNext())
        //                 {
        //                     QRegularExpressionMatch match = it.next();
        //                     int start = match.capturedStart();
        //                     int len   = match.capturedLength();
        //                     if (start < 0 || len <= 0)
        //                         continue;
        //
        //                     QString raw = match.captured();
        //                     while (
        //                         !raw.isEmpty()
        //                         &&
        //                         QString(".,;:!?)\"'").contains(raw.back()))
        //                     {
        //                         raw.chop(1);
        //                         --len;
        //                     }
        //
        //                     if (raw.isEmpty() || len <= 0)
        //                         continue;
        //
        //                     fz_quad q = getQuadForSubstring(line, start,
        //                     len); fz_rect r = fz_rect_from_quad(q); if
        //                     (fz_is_empty_rect(r))
        //                         continue;
        //
        //                     if (hasIntersectingLink(r))
        //                         continue;
        //
        //                     QString uri = raw;
        //                     if (uri.startsWith("www."))
        //                         uri.prepend("https://");
        //
        //                     fz_rect tr        = fz_transform_rect(r,
        //                     transform); const float scale = m_inv_dpr;
        //                     QRectF qtRect(tr.x0 * scale, tr.y0 * scale,
        //                                   (tr.x1 - tr.x0) * scale,
        //                                   (tr.y1 - tr.y0) * scale);
        //
        //                     RenderLink renderLink;
        //                     renderLink.rect = qtRect;
        //                     renderLink.uri  = uri;
        //                     renderLink.type
        //                         = BrowseLinkItem::LinkType::External;
        //                     renderLink.boundary = m_link_show_boundary;
        //                     result.links.push_back(std::move(renderLink));
        //                 }
        //             }
        //         }
        //     }
        // }

        result.annotations.reserve(annotations.size());
        for (const auto &annot : annotations)
        {
            RenderAnnotation renderAnnot;

            fz_rect r = fz_transform_rect(annot.rect, transform);
            QRectF qtRect(r.x0 * scale, r.y0 * scale, (r.x1 - r.x0) * scale,
                          (r.y1 - r.y0) * scale);
            renderAnnot.rect  = qtRect;
            renderAnnot.type  = annot.type;
            renderAnnot.index = annot.index;
            renderAnnot.color = annot.color;
            renderAnnot.text  = annot.text;
            for (const fz_rect &qr : annot.quad_rects)
            {
                fz_rect tr = fz_transform_rect(qr, transform);
                renderAnnot.rects.emplace_back(tr.x0 * scale, tr.y0 * scale,
                                               (tr.x1 - tr.x0) * scale,
                                               (tr.y1 - tr.y0) * scale);
            }
            result.annotations.push_back(std::move(renderAnnot));
        }
    }
    fz_always(ctx)
    {
        fz_close_device(ctx, tracker);
        fz_drop_device(ctx, tracker);

        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);

        fz_drop_link(ctx, head);
        fz_drop_pixmap(ctx, pix);
        fz_drop_display_list(ctx, dlist);

        // fz_drop_page(ctx, text_page);
        fz_drop_context(ctx);
    }
    fz_catch(ctx)
    {
        qWarning() << "MuPDF error in thread:" << fz_caught_message(ctx);
    }

    return result;
}

QImage
Model::renderRegionAtDPI(int pageno, QRectF logicalRect,
                         float targetDPI) noexcept
{
    // logicalRect is in item-local logical pixels (physicalPixels / DPR).
    // buildPageTransforms uses logicalScale(), which maps PDF pts ↔ logical
    // pixels, so dev_to_page correctly inverts logicalRect back to PDF
    // point-space.
    const auto [page_to_dev, dev_to_page] = buildPageTransforms(pageno);

    const fz_point tl = fz_transform_point(
        {float(logicalRect.left()), float(logicalRect.top())}, dev_to_page);
    const fz_point br = fz_transform_point(
        {float(logicalRect.right()), float(logicalRect.bottom())}, dev_to_page);
    const fz_rect page_rect_pts = {std::min(tl.x, br.x), std::min(tl.y, br.y),
                                   std::max(tl.x, br.x), std::max(tl.y, br.y)};

    return renderPtsRegion(pageno,
                           QRectF(page_rect_pts.x0, page_rect_pts.y0,
                                  page_rect_pts.x1 - page_rect_pts.x0,
                                  page_rect_pts.y1 - page_rect_pts.y0),
                           targetDPI);
}

QImage
Model::renderPtsRegion(int pageno, QRectF ptsRect, float targetDPI) noexcept
{
    const fz_rect page_rect_pts
        = {float(ptsRect.left()), float(ptsRect.top()), float(ptsRect.right()),
           float(ptsRect.bottom())};

    // For raster sources (images, DjVu) there is no display list to re-render
    // from — return a null image so the caller can fall back to upscaling.
    if (m_is_image || m_filetype == FileType::DJVU)
        return {};

    // The page may never have been shown (e.g. the target of a link).
    ensurePageCached(pageno);

    auto [w, h] = getPageDimensions(pageno);
    if (w <= 0 || h <= 0)
        return {};

    const fz_rect full_bounds = {0, 0, w, h};

    // Build a new render transform at targetDPI, preserving rotation and flip.
    // targetDPI is passed directly as the "zoom" argument; fz_transform_page
    // divides by 72 internally, yielding targetDPI/72 pixels-per-point.
    const fz_matrix target_ctm = buildRenderTransform(
        full_bounds, targetDPI, m_rotation, m_flip_h, m_flip_v);

    // Determine the pixel bbox for only the selected region.
    const fz_rect target_rect  = fz_transform_rect(page_rect_pts, target_ctm);
    const fz_irect target_bbox = fz_round_rect(target_rect);

    if (target_bbox.x0 >= target_bbox.x1 || target_bbox.y0 >= target_bbox.y1)
        return {};

    fz_context *ctx = cloneContext();
    if (!ctx)
        return {};

    fz_display_list *dlist = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        const PageCacheEntry *entry = m_page_lru_cache.get(pageno);
        if (!entry || !entry->display_list)
        {
            fz_drop_context(ctx);
            return {};
        }
        dlist = fz_keep_display_list(ctx, entry->display_list);
    }

    fz_pixmap *pix = nullptr;
    fz_device *dev = nullptr;
    QImage result;

    fz_try(ctx)
    {
        pix = fz_new_pixmap_with_bbox(ctx, m_colorspace, target_bbox, nullptr,
                                      0);
        fz_clear_pixmap_with_value(ctx, pix, 255);

        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_display_list(ctx, dlist, dev, target_ctm,
                            fz_rect_from_irect(target_bbox), nullptr);
        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);
        dev = nullptr;

        const int fg = (m_fg_color >> 8) & 0xFFFFFF;
        const int bg = (m_bg_color >> 8) & 0xFFFFFF;
        if (fg != 0 || bg != 0)
            fz_tint_pixmap(ctx, pix, fg, bg);

        if (m_invert_color)
            fz_invert_pixmap(ctx, pix);

        const int width  = fz_pixmap_width(ctx, pix);
        const int height = fz_pixmap_height(ctx, pix);
        const int n      = fz_pixmap_components(ctx, pix);
        const int stride = fz_pixmap_stride(ctx, pix);

        QImage::Format fmt;
        switch (n)
        {
            case 1:
                fmt = QImage::Format_Grayscale8;
                break;
            case 3:
                fmt = QImage::Format_RGB888;
                break;
            case 4:
                fmt = QImage::Format_RGBA8888;
                break;
            default:
                fz_throw(ctx, FZ_ERROR_GENERIC, "Unsupported component count");
        }

        result = QImage(fz_pixmap_samples(ctx, pix), width, height, stride, fmt)
                     .copy();
        result.setDotsPerMeterX(
            static_cast<int>((targetDPI * 1000.0f) / 25.4f));
        result.setDotsPerMeterY(
            static_cast<int>((targetDPI * 1000.0f) / 25.4f));
    }
    fz_always(ctx)
    {
        fz_drop_device(ctx, dev);
        fz_drop_pixmap(ctx, pix);
        fz_drop_display_list(ctx, dlist);
    }
    fz_catch(ctx)
    {
        qWarning() << "renderPtsRegion failed:" << fz_caught_message(ctx);
        result = {};
    }

    fz_drop_context(ctx);
    return result;
}

Model::ImageHit
Model::imageAt(int pageno, QPointF logicalPt) noexcept
{
    ImageHit result;

    // Raster sources (plain images, DjVu) have no notion of "an embedded
    // image within the page" separate from the page itself.
    if (m_is_image || m_filetype == FileType::DJVU)
        return result;

    const fz_matrix dev_to_page = buildPageTransforms(pageno).second;
    const fz_point pagePt       = fz_transform_point(
        {float(logicalPt.x()), float(logicalPt.y())}, dev_to_page);

    fz_context *ctx = cloneContext();
    if (!ctx)
        return result;

    fz_display_list *dlist = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        const PageCacheEntry *entry = m_page_lru_cache.get(pageno);
        if (!entry || !entry->display_list)
        {
            fz_drop_context(ctx);
            return result;
        }
        dlist = fz_keep_display_list(ctx, entry->display_list);
    }

    fz_device *tracker = nullptr;
    fz_pixmap *pix     = nullptr;

    fz_try(ctx)
    {
        // Identity ctm: the tracked bboxes come out directly in page-point
        // space, matching pagePt.
        tracker = new_image_tracker_device(ctx, nullptr, fz_identity);
        fz_run_display_list(ctx, dlist, tracker, fz_identity, fz_infinite_rect,
                            nullptr);
        fz_close_device(ctx, tracker);

        auto *td = reinterpret_cast<fz_image_tracker_device *>(tracker);

        // Several images can cover the point (e.g. a full-page scan plus a
        // smaller overlay). Dragging should take the one that is the page
        // content, which is the largest such image, not the topmost overlay.
        const ImageRect *best = nullptr;
        fz_rect bestRect      = fz_empty_rect;
        for (int i = 0; i < td->rect_count; ++i)
        {
            const ImageRect &ir = td->rects[i];
            const fz_rect r     = fz_rect_from_irect(ir.bbox);
            if (pagePt.x < r.x0 || pagePt.x > r.x1 || pagePt.y < r.y0
                || pagePt.y > r.y1)
                continue;
            if (!best
                || (r.x1 - r.x0) * (r.y1 - r.y0)
                       >= (bestRect.x1 - bestRect.x0)
                              * (bestRect.y1 - bestRect.y0))
            {
                best     = &ir;
                bestRect = r;
            }
        }
        if (best)
        {
            const ImageRect &ir = *best;
            const fz_rect r     = bestRect;

            // Render the page's own appearance inside this image's rect, at the
            // image's native pixel size. Decoding the image alone is wrong for
            // stencil/mask images (e.g. text on a scanned page): it yields only
            // the mask shape, not the colours the page paints with it.
            const int pw      = ir.image->w;
            const int ph      = ir.image->h;
            const float wpt   = r.x1 - r.x0;
            const float hpt   = r.y1 - r.y0;
            const fz_matrix m = fz_concat(fz_translate(-r.x0, -r.y0),
                                          fz_scale(pw / wpt, ph / hpt));
            pix               = fz_new_pixmap_with_bbox(
                ctx, m_colorspace, fz_make_irect(0, 0, pw, ph), nullptr, 0);
            fz_clear_pixmap_with_value(ctx, pix, 255);
            fz_device *rdev = fz_new_draw_device(ctx, fz_identity, pix);
            fz_run_display_list(ctx, dlist, rdev, m, r, nullptr);
            fz_close_device(ctx, rdev);
            fz_drop_device(ctx, rdev);

            // Match what the page render does to its pixels, so the dragged
            // image looks like it does on screen. With dont_invert_images the
            // page leaves images untouched, so do the same here.
            if (!(m_config.behavior.dont_invert_images
                  && supports_image_blocks()))
            {
                const int fg = (m_fg_color >> 8) & 0xFFFFFF;
                const int bg = (m_bg_color >> 8) & 0xFFFFFF;
                if (fg != 0 || bg != 0)
                    fz_tint_pixmap(ctx, pix, fg, bg);
                if (m_invert_color)
                    fz_invert_pixmap(ctx, pix);
                if (m_config.behavior.high_contrast)
                {
                    unsigned char lut[256];
                    buildHighContrastLUT(
                        lut, m_config.behavior.high_contrast_black_point,
                        m_config.behavior.high_contrast_white_point);
                    const size_t nbytes
                        = static_cast<size_t>(fz_pixmap_stride(ctx, pix))
                          * static_cast<size_t>(fz_pixmap_height(ctx, pix));
                    applyHighContrastSamples(fz_pixmap_samples(ctx, pix),
                                             nbytes, lut);
                }
            }

            const int width  = fz_pixmap_width(ctx, pix);
            const int height = fz_pixmap_height(ctx, pix);
            const int n      = fz_pixmap_components(ctx, pix);
            const int stride = fz_pixmap_stride(ctx, pix);

            QImage::Format fmt;
            switch (n)
            {
                case 1:
                    fmt = QImage::Format_Grayscale8;
                    break;
                case 3:
                    fmt = QImage::Format_RGB888;
                    break;
                case 4:
                    fmt = QImage::Format_RGBA8888;
                    break;
                default:
                    fz_throw(ctx, FZ_ERROR_GENERIC,
                             "Unsupported component count");
            }

            result.image = QImage(fz_pixmap_samples(ctx, pix), width, height,
                                  stride, fmt)
                               .copy();
            result.page_rect_pts = QRectF(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
            result.valid         = !result.image.isNull();
        }
    }
    fz_always(ctx)
    {
        if (tracker)
            fz_drop_device(ctx, tracker);
        fz_drop_pixmap(ctx, pix);
        fz_drop_display_list(ctx, dlist);
    }
    fz_catch(ctx)
    {
        qWarning() << "Model::imageAt failed:" << fz_caught_message(ctx);
        result = {};
    }

    fz_drop_context(ctx);
    return result;
}

void
Model::setZoom(float zoom) noexcept
{
    m_zoom = zoom;

    if (m_filetype == FileType::DJVU)
    {
        invalidatePageCaches();
        return;
    }

    if (m_is_image)
        invalidatePageCaches();
}

void
Model::rotateClock() noexcept
{
    m_rotation += 90;
    if (m_rotation >= 360)
        m_rotation = 0;

    if (m_filetype == FileType::DJVU)
    {
        invalidatePageCaches();
        return;
    }

    if (m_is_image)
        invalidatePageCaches();
}

void
Model::rotateAnticlock() noexcept
{
    m_rotation -= 90;
    if (m_rotation < 0)
        m_rotation = 270;

    if (m_filetype == FileType::DJVU)
    {
        invalidatePageCaches();
        return;
    }

    if (m_is_image)
        invalidatePageCaches();
}
