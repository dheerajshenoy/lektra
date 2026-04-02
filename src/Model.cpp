#include "Model.hpp"

#include "BrowseLinkItem.hpp"
#include "Commands/TextHighlightAnnotationCommand.hpp"
#include "Config.hpp"
#include "utils.hpp"

#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <array>
#include <pthread.h>
#include <qbytearrayview.h>
#include <qregularexpression.h>
#include <qstyle.h>
#include <qtextformat.h>
#include <unordered_set>

static std::array<std::mutex, FZ_LOCK_MAX> mupdf_mutexes;

// Helper: compute signed distance along a direction from origin to point
static float
linedist_sel(const fz_point &origin, const fz_point &dir, const fz_point &q)
{
    return dir.x * (q.x - origin.x) + dir.y * (q.y - origin.y);
}

// Helper: count characters in a line
static int
line_length(fz_stext_line *line)
{
    int n = 0;
    for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
        ++n;
    return n;
}

// Helper: get largest character size in a line
static float
largest_size_in_line(fz_stext_line *line)
{
    float size = 0;
    for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
        if (ch->size > size)
            size = ch->size;
    return size;
}

#ifdef HAS_DJVU
static void
handle_messages(ddjvu_context_t *ctx, int wait)
{
    const ddjvu_message_t *msg;
    if (wait)
        ddjvu_message_wait(ctx);
    while ((msg = ddjvu_message_peek(ctx)))
    {
        if (msg->m_any.tag == DDJVU_ERROR)
            fprintf(stderr, "ddjvu error: %s\n", msg->m_error.message);
        ddjvu_message_pop(ctx);
    }
}
#endif

// Helper: find the closest character index within a line to point q
static int
find_closest_in_line(fz_stext_line *line, int idx, fz_point q)
{
    float closest_dist = 1e30f;
    int closest_idx    = idx;

    const float hsize = largest_size_in_line(line) / 2;
    const fz_point vdir{-line->dir.y, line->dir.x};
    const fz_point hdir = line->dir;

    // Compute mid-line from quads
    const fz_point p1{
        (line->first_char->quad.ll.x + line->first_char->quad.ul.x) / 2,
        (line->first_char->quad.ll.y + line->first_char->quad.ul.y) / 2};

    // Signed distance perpendicular mid-line (positive is below)
    const float vd = linedist_sel(p1, vdir, q);
    if (vd < -hsize)
        return idx;
    if (vd > hsize)
        return idx + line_length(line);

    for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
    {
        float d1, d2;
        if (ch->bidi & 1)
        {
            d1 = std::abs(linedist_sel(ch->quad.lr, hdir, q));
            d2 = std::abs(linedist_sel(ch->quad.ll, hdir, q));
        }
        else
        {
            d1 = std::abs(linedist_sel(ch->quad.ll, hdir, q));
            d2 = std::abs(linedist_sel(ch->quad.lr, hdir, q));
        }

        if (d1 < closest_dist)
        {
            closest_dist = d1;
            closest_idx  = idx;
        }

        if (d2 < closest_dist)
        {
            closest_dist = d2;
            closest_idx  = idx + 1;
        }

        ++idx;
    }

    return closest_idx;
}

// Helper: find the closest character index in the page to point q
// This is column-aware because it considers both horizontal and vertical
// distance to find the geometrically closest line
static int
find_closest_in_page(fz_stext_page *page, fz_point q)
{
    fz_stext_line *closest_line = nullptr;
    int closest_idx             = 0;
    float closest_dist          = 1e30f;
    int idx                     = 0;

    for (fz_stext_block *block = page->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line *line = block->u.t.first_line; line;
             line                = line->next)
        {
            if (!line->first_char)
            {
                idx += line_length(line);
                continue;
            }

            const float hsize   = largest_size_in_line(line) / 2;
            const fz_point hdir = line->dir;
            const fz_point vdir{-line->dir.y, line->dir.x};

            // Compute mid-line from quads
            const fz_point p1{
                (line->first_char->quad.ll.x + line->first_char->quad.ul.x) / 2,
                (line->first_char->quad.ll.y + line->first_char->quad.ul.y)
                    / 2};
            const fz_point p2{
                (line->last_char->quad.lr.x + line->last_char->quad.ur.x) / 2,
                (line->last_char->quad.lr.y + line->last_char->quad.ur.y) / 2};

            // Signed distance perpendicular to mid-line (positive is below)
            const float vdist = linedist_sel(p1, vdir, q);

            // Signed distance tangent to mid-line from end points
            const float hdist1 = linedist_sel(p1, hdir, q);
            const float hdist2 = linedist_sel(p2, hdir, q);

            // Within the line itself (horizontally between endpoints)
            if (vdist >= -hsize && vdist <= hsize
                && (hdist1 > 0) != (hdist2 > 0))
            {
                // Perfect match - point is directly on this line
                closest_dist = 0;
                closest_line = line;
                closest_idx  = idx;
            }
            else
            {
                // Vertical distance from mid-line
                const float avdist = std::abs(vdist);

                // Horizontal distance from closest end-point (0 if within line)
                float ahdist = 0;
                if ((hdist1 > 0) == (hdist2 > 0))
                {
                    // Point is outside the horizontal extent of the line
                    ahdist = std::min(std::abs(hdist1), std::abs(hdist2));
                }

                // Compute combined distance metric
                // Use Euclidean-like distance but weight vertical distance
                // less when we're within the vertical band of the line.
                // This ensures that when cursor is at the same Y-level as
                // lines in different columns, we strongly prefer the
                // horizontally closer column.
                float dist;
                if (avdist < hsize)
                {
                    // Within vertical band - horizontal distance dominates
                    // Small vertical component prevents jumping between
                    // adjacent lines in same column
                    dist = ahdist + avdist * 0.1f;
                }
                else
                {
                    // Outside vertical band - use weighted Euclidean distance
                    // Weight horizontal distance more to prefer staying in
                    // the same column
                    dist = std::sqrt(avdist * avdist + ahdist * ahdist * 4.0f);
                }

                if (dist < closest_dist)
                {
                    closest_dist = dist;
                    closest_line = line;
                    closest_idx  = idx;
                }
            }

            idx += line_length(line);
        }
    }

    if (closest_line)
        return find_closest_in_line(closest_line, closest_idx, q);

    return 0;
}

// Check if two points are approximately the same
static bool
same_point(const fz_point &a, const fz_point &b)
{
    return std::abs(a.x - b.x) < 0.1f && std::abs(a.y - b.y) < 0.1f;
}

// Check if a point is near another within the given fuzz values
static bool
is_near(float hfuzz, float vfuzz, const fz_point &hdir, const fz_point &end,
        const fz_point &p1, const fz_point &p2)
{
    const fz_point vdir{-hdir.y, hdir.x};
    const float v  = std::abs(linedist_sel(end, vdir, p1));
    const float d1 = std::abs(linedist_sel(end, hdir, p1));
    const float d2 = std::abs(linedist_sel(end, hdir, p2));
    return (v < vfuzz && d1 < hfuzz && d1 < d2);
}

// Main selection function - uses character index-based selection
// which properly handles multi-column layouts
static int
highlight_selection(fz_stext_page *stext_page, fz_point a, fz_point b,
                    fz_quad *quads, int max_quads)
{
    // Find character indices closest to points a and b
    int start = find_closest_in_page(stext_page, a);
    int end   = find_closest_in_page(stext_page, b);

    // Swap if needed to ensure start <= end
    if (start > end)
        std::swap(start, end);

    if (start == end)
        return 0;

    // Enumerate characters between start and end, collecting quads
    int count   = 0;
    int idx     = 0;
    bool inside = false;

    // Fuzz values for merging adjacent quads
    constexpr float hfuzz = 0.5f;
    constexpr float vfuzz = 0.1f;

    for (fz_stext_block *block = stext_page->first_block; block;
         block                 = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line *line = block->u.t.first_line; line;
             line                = line->next)
        {
            for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
            {
                if (!inside && idx == start)
                    inside = true;

                if (inside)
                {
                    // Skip zero-extent quads
                    if (!same_point(ch->quad.ll, ch->quad.lr))
                    {
                        const float char_vfuzz = vfuzz * ch->size;
                        const float char_hfuzz = hfuzz * ch->size;

                        // Try to merge with the previous quad
                        if (count > 0)
                        {
                            fz_quad &prev = quads[count - 1];
                            if (is_near(char_hfuzz, char_vfuzz, line->dir,
                                        prev.lr, ch->quad.ll, ch->quad.lr)
                                && is_near(char_hfuzz, char_vfuzz, line->dir,
                                           prev.ur, ch->quad.ul, ch->quad.ur))
                            {
                                // Merge by extending the previous quad
                                prev.ur = ch->quad.ur;
                                prev.lr = ch->quad.lr;
                                ++idx;
                                if (idx == end)
                                    return count;
                                continue;
                            }
                        }

                        // Add new quad if we have space
                        if (count < max_quads)
                            quads[count++] = ch->quad;
                    }
                }

                ++idx;
                if (idx == end)
                    return count;
            }
        }
    }

    return count;
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

// Helper to restore image regions after inversion
static void
restore_image_regions(fz_context *ctx, fz_pixmap *pix,
                      fz_image_tracker_device *tracker,
                      fz_colorspace *colorspace)
{
    if (tracker->rect_count == 0)
        return;

    // For each tracked image, render it directly to the pixmap region
    for (int i = 0; i < tracker->rect_count; ++i)
    {

        ImageRect &ir = tracker->rects[i];

        // Clip bbox to pixmap bounds
        fz_irect clipped
            = fz_intersect_irect(ir.bbox, fz_pixmap_bbox(ctx, pix));
        if (fz_is_empty_irect(clipped))
            continue;

        // Create a sub-pixmap for just this region
        fz_pixmap *sub{nullptr};
        fz_device *draw_dev{nullptr};

        fz_try(ctx)
        {
            // Create a temporary pixmap for this region
            sub = fz_new_pixmap_with_bbox(ctx, colorspace, clipped, nullptr, 1);
            fz_clear_pixmap_with_value(ctx, sub, 255);

            // The draw device needs a translation to map from device coords
            // to sub-pixmap coords (which start at 0,0)
            draw_dev = fz_new_draw_device(ctx, fz_identity, sub);

            // ir.ctm is already the full transformation that was used during
            // the original render (page coords -> device coords), so use it
            // directly
            fz_fill_image(ctx, draw_dev, ir.image, ir.ctm, ir.alpha,
                          ir.color_params);

            fz_close_device(ctx, draw_dev);

            // Copy pixels from sub back to main pixmap
            int n          = fz_pixmap_components(ctx, pix);
            int sub_stride = fz_pixmap_stride(ctx, sub);
            int pix_stride = fz_pixmap_stride(ctx, pix);

            unsigned char *sub_samples = fz_pixmap_samples(ctx, sub);
            unsigned char *pix_samples = fz_pixmap_samples(ctx, pix);

            // Calculate offset in main pixmap
            int pix_x0 = fz_pixmap_x(ctx, pix);
            int pix_y0 = fz_pixmap_y(ctx, pix);

            for (int y = clipped.y0; y < clipped.y1; ++y)
            {
                int sub_row = y - clipped.y0;
                int pix_row = y - pix_y0;

                unsigned char *src = sub_samples + sub_row * sub_stride;
                unsigned char *dst = pix_samples + pix_row * pix_stride
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
            // Log error but continue with other images
            fz_warn(ctx, "Failed to restore image region: %s",
                    fz_caught_message(ctx));
        }
    }
}

// ============================================================================

static void
mupdf_lock_mutex(void *user, int lock)
{
    auto *m = static_cast<std::mutex *>(user);
    m[lock].lock();
}

static void
mupdf_unlock_mutex(void *user, int lock)
{
    auto *m = static_cast<std::mutex *>(user);
    m[lock].unlock();
}

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

        if (m_text_cache.has(pageno))
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

#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
        cleanup_djvu();
    else
#endif
    {
        cleanup_mupdf();
    }

    if (m_ctx)
        fz_drop_context(m_ctx);
}

void
Model::initMuPDF() noexcept
{
    // initialize each mutex
    m_fz_locks.user   = mupdf_mutexes.data();
    m_fz_locks.lock   = mupdf_lock_mutex;
    m_fz_locks.unlock = mupdf_unlock_mutex;
    m_ctx             = fz_new_context(nullptr, &m_fz_locks, FZ_STORE_DEFAULT);
    fz_register_document_handlers(m_ctx);
    m_colorspace = fz_device_rgb(m_ctx);
}

void
Model::cleanup_mupdf() noexcept
{
    fz_drop_outline(m_ctx, m_outline);
    m_outline = nullptr;
    fz_drop_document(m_ctx, m_doc);
    m_doc     = nullptr;
    m_pdf_doc = nullptr;

    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        m_page_lru_cache.clear();
        m_text_cache.clear();
        m_stext_page_cache.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        m_page_dim_cache.reset(0);
        m_default_page_dim = {};
    }

    fz_empty_store(m_ctx);
}

#ifdef HAS_DJVU
void
Model::cleanup_djvu() noexcept
{
    ddjvu_document_release(m_ddjvu_doc);
    ddjvu_context_release(m_ddjvu_ctx);

    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        m_page_lru_cache.clear();
        m_text_cache.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        m_page_dim_cache.reset(0);
        m_default_page_dim = {};
    }
}
#endif

// Open file asynchronously to avoid blocking the UI, especially for large
// documents or slow storage. The actual opening and page counting happens in a
// background thread, and results are posted back to the main thread when done.
// NOTE: no checking is done on the file path here — if it's invalid, the
// background thread will catch the error and emit openFileFailed. File
// existence checking should be done outside, before calling this function.
QFuture<void>
Model::openAsync(const QString &filePath) noexcept
{
    m_filepath              = QFileInfo(filePath).canonicalFilePath();
    const QString canonPath = m_filepath;

    // Detect file type before launching the background task, so we can fail
    // fast for unsupported types without incurring the overhead of starting a
    // thread and cloning the context.
    m_filetype = getFileType(canonPath);

    if (m_filetype == FileType::NONE)
    {
        QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                  Qt::QueuedConnection);
        return QtConcurrent::run([] {});
    }

#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
        return openAsync_djvu(canonPath);
    else
#endif
        return openAsync_mupdf(canonPath);
}

#ifdef HAS_DJVU
QFuture<void>
Model::openAsync_djvu(const QString &canonPath) noexcept
{
    return QtConcurrent::run([this, canonPath]
    {
        ddjvu_context_t *ctx  = ddjvu_context_create("LEKTRA");
        ddjvu_document_t *doc = ddjvu_document_create_by_filename(
            ctx, canonPath.toUtf8().constData(), true);
        if (!doc)
        {
            ddjvu_context_release(ctx);
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }

        // Pump until decoded
        while (!ddjvu_document_decoding_done(doc))
        {
            ddjvu_message_t *msg = ddjvu_message_wait(ctx);
            if (msg->m_any.tag == DDJVU_ERROR)
            {
                ddjvu_document_release(doc);
                ddjvu_context_release(ctx);
                QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                          Qt::QueuedConnection);
                return;
            }
            ddjvu_message_pop(ctx);
        }

        const int page_count = ddjvu_document_get_pagenum(doc);

        ddjvu_pageinfo_t info{};
        ddjvu_document_get_pageinfo(doc, 0, &info);
        const float w = static_cast<float>(info.width) / info.dpi * 72.0f;
        const float h = static_cast<float>(info.height) / info.dpi * 72.0f;

        QMetaObject::invokeMethod(this, [this, ctx, doc, page_count, w, h]()
        {
            waitForPendingRenders();
            m_render_cancelled.store(false, std::memory_order_release);
            cleanup_mupdf(); // drops MuPDF state
            cleanup_djvu();  // drops any previous DjVu state

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
#endif

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
            fz_document *doc{nullptr};
            bool committed{false};
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

#ifdef HAS_DJVU
        cleanup_djvu();
#endif
        cleanup_mupdf();

        fz_document *doc          = nullptr;
        const std::string pathStr = canonPath.toStdString();
        fz_try(bg_ctx)
        {
            doc = fz_open_document(bg_ctx, pathStr.c_str());
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
                m_pending = {ctx, doc};
                emit wrongPassword();
            }, Qt::QueuedConnection);
            return;
        }

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
    cleanup_mupdf();
}

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

#ifdef HAS_DJVU
void
Model::buildPageCache_djvu(int pageno) noexcept
{
    if (!m_ddjvu_doc || m_ddjvu_ctx == nullptr)
        return;

    // DjVuLibre is NOT thread-safe for the same context — serialize
    std::lock_guard<std::mutex> lock(m_doc_mutex);

    ddjvu_page_t *page = ddjvu_page_create_by_pageno(m_ddjvu_doc, pageno);
    if (!page)
        return;

    // Pump until page is ready
    ddjvu_message_t *msg;
    while (!ddjvu_page_decoding_done(page))
    {
        msg = ddjvu_message_wait(m_ddjvu_ctx);
        if (msg->m_any.tag == DDJVU_ERROR)
        {
            ddjvu_page_release(page);
            return;
        }
        ddjvu_message_pop(m_ddjvu_ctx);
    }

    const ddjvu_page_rotation_t djvu_rot = [&]() -> ddjvu_page_rotation_t
    {
        switch (((static_cast<int>(m_rotation) % 360) + 360) % 360)
        {
            case 90:
                return DDJVU_ROTATE_90;
            case 180:
                return DDJVU_ROTATE_180;
            case 270:
                return DDJVU_ROTATE_270;
            default:
                return DDJVU_ROTATE_0;
        }
    }();
    ddjvu_page_set_rotation(page, djvu_rot);

    // DjVu page native DPI and dimensions
    const int native_dpi = ddjvu_page_get_resolution(page);
    const int pw_px      = ddjvu_page_get_width(page); // at native DPI
    const int ph_px      = ddjvu_page_get_height(page);

    // Store dimensions in pts (1/72 inch) for the rest of the pipeline
    const float w_pts = static_cast<float>(pw_px) / native_dpi * 72.0f;
    const float h_pts = static_cast<float>(ph_px) / native_dpi * 72.0f;

    {
        std::lock_guard<std::mutex> dimlock(m_page_dim_mutex);
        m_page_dim_cache.set(pageno, w_pts, h_pts);
    }

    // Render at m_zoom * m_dpi — same scale logic as the MuPDF path
    const float render_dpi = m_zoom * m_dpi * m_dpr;
    const float scale      = render_dpi / native_dpi;
    const int rw           = static_cast<int>(pw_px * scale);
    const int rh           = static_cast<int>(ph_px * scale);

    ddjvu_rect_t prect{0, 0, static_cast<unsigned>(rw),
                       static_cast<unsigned>(rh)};
    ddjvu_rect_t rrect = prect;

    // BGRA format maps cleanly to QImage::Format_RGB32
    const int stride = rw * 4;
    QByteArray buf(stride * rh, 0);

    ddjvu_format_t *fmt{nullptr};
    // DjVuLibre RGBMASK32: specify R/G/B masks and white background
    const unsigned int masks[3] = {0x00FF0000, 0x0000FF00, 0x000000FF};
    fmt = ddjvu_format_create(DDJVU_FORMAT_RGBMASK32, 3,
                              const_cast<unsigned int *>(masks));
    ddjvu_format_set_row_order(fmt, 1); // top-to-bottom

    const int ok = ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &rrect,
                                     fmt, stride, buf.data());

    ddjvu_format_release(fmt);
    ddjvu_page_release(page);

    if (!ok)
        return;

    QImage image(reinterpret_cast<const uchar *>(buf.constData()), rw, rh,
                 stride, QImage::Format_RGB32);
    image = image.copy(); // detach from buf's lifetime
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
    entry.display_list = nullptr; // signals "DjVu pre-rendered" path
    entry.cached_image = image;   // add this field to PageCacheEntry

    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        if (!m_page_lru_cache.has(pageno))
            m_page_lru_cache.put(pageno, std::move(entry));
    }
}
#endif

void
Model::buildPageCache(int pageno) noexcept
{
    if (m_page_lru_cache.has(pageno))
        return;

#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
    {
        buildPageCache_djvu(pageno);
        return;
    }
#endif

    PageCacheEntry entry;

    fz_context *ctx = cloneContext();
    if (!ctx)
    {
        qWarning() << "Failed to clone context for page cache";
        return;
    }

    fz_page *page{nullptr};
    fz_display_list *dlist{nullptr};
    fz_device *list_dev{nullptr};
    fz_link *head{nullptr};
    fz_rect bounds{};
    bool success{false};

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
        if (m_config.links.enabled)
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

                entry.links.push_back(std::move(cl));
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
                    case PDF_ANNOT_HIGHLIGHT:
                    {
                        pdf_annot_color(ctx, annot, &n, color);
                        ca.color = QColor::fromRgbF(color[0], color[1],
                                                    color[2], ca.opacity);
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

                entry.annotations.push_back(std::move(ca));
            }
        }

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
        fz_drop_context(ctx);
    }
    fz_catch(ctx)
    {
        qWarning() << "Failed to build page cache for page" << pageno << ":"
                   << fz_caught_message(ctx);
    }

    if (!success)
        return;

    // Cache the display list and links
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
        if (!m_page_lru_cache.has(pageno))
        {
            m_page_lru_cache.put(pageno, std::move(entry));
        }
        else
            fz_drop_display_list(ctx, dlist);
    }
}

void
Model::setPopupColor(const QColor &color) noexcept
{
    m_popup_color[0] = color.redF();
    m_popup_color[1] = color.greenF();
    m_popup_color[2] = color.blueF();
    m_popup_color[3] = color.alphaF();
}

void
Model::setHighlightColor(const QColor &color) noexcept
{
    m_highlight_color[0] = color.redF();
    m_highlight_color[1] = color.greenF();
    m_highlight_color[2] = color.blueF();
    m_highlight_color[3] = color.alphaF();
}

void
Model::setSelectionColor(const QColor &color) noexcept
{
    m_selection_color[0] = color.redF();
    m_selection_color[1] = color.greenF();
    m_selection_color[2] = color.blueF();
    m_selection_color[3] = color.alphaF();
}

void
Model::setAnnotRectColor(const QColor &color) noexcept
{
    m_annot_rect_color[0] = color.redF();
    m_annot_rect_color[1] = color.greenF();
    m_annot_rect_color[2] = color.blueF();
    m_annot_rect_color[3] = color.alphaF();
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
            const std::string filePathStr = m_filepath.toStdString();
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
        const std::string filePathStr = m_filepath.toStdString();
        new_doc = fz_open_document(m_ctx, filePathStr.c_str());
        if (!new_doc)
            return false;
    }
    fz_catch(m_ctx)
    {
        qWarning() << "reloadDocument: failed to open:"
                   << fz_caught_message(m_ctx);
        return false;
    }

    int page_count = 0;
    float w = 0, h = 0;
    fz_page *page{nullptr};
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
#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
        return false;
#endif

    fz_try(m_ctx)
    {
        const std::string pathStr = m_filepath.toStdString();
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        pdf_write_options opts = m_pdf_write_options;
        // opts.do_incremental    = 1;
        pdf_save_document(m_ctx, m_pdf_doc, pathStr.c_str(), &opts);

        if (m_undo_stack)
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

    const std::string pathStr = newFilePath.toStdString();
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

fz_outline *
Model::getOutline() noexcept
{
    if (!m_doc)
        return nullptr;

    if (!m_outline)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        m_outline = fz_load_outline(m_ctx, m_doc);
    }

    return m_outline;
}

std::vector<QPolygonF>
Model::computeTextSelectionQuad(int pageno, QPointF devStart,
                                QPointF devEnd) noexcept
{
#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
        return {};
#endif

    std::vector<QPolygonF> out;
    constexpr int MAX_HITS{1024};
    thread_local std::array<fz_quad, MAX_HITS> hits;
    const float scale = logicalScale();

    fz_page *page{nullptr};
    fz_rect page_bounds;
    fz_matrix page_to_dev{};
    int count{0};

    fz_try(m_ctx)
    {
        const auto [w, h] = getPageDimensions(pageno);
        if (w < 0 || h < 0)
        {
            page        = fz_load_page(m_ctx, m_doc, pageno);
            page_bounds = fz_bound_page(m_ctx, page);
        }
        else
        {
            page_bounds = {0, 0, w, h};
        }

        page_to_dev = fz_scale(scale, scale);
        page_to_dev = fz_pre_rotate(page_to_dev, m_rotation);

        const fz_rect dev_bounds = fz_transform_rect(page_bounds, page_to_dev);
        page_to_dev = fz_concat(page_to_dev,
                                fz_translate(-dev_bounds.x0, -dev_bounds.y0));

        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

        fz_point a = {float(devStart.x()), float(devStart.y())};
        fz_point b = {float(devEnd.x()), float(devEnd.y())};

        a = fz_transform_point(a, dev_to_page);
        b = fz_transform_point(b, dev_to_page);

        if (a.y > b.y || (qFuzzyCompare(a.y, b.y) && a.x > b.x))
        {
            std::swap(a, b);
        }

        fz_stext_page *stext_page = get_or_build_stext_page(m_ctx, pageno);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

        count = highlight_selection(stext_page, a, b, hits.data(), MAX_HITS);
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Selection failed:" << fz_caught_message(m_ctx);
        return out;
    }

    out.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const fz_quad &q = hits[i];

        auto toDev = [&](const fz_point &p0) -> QPointF
        {
            const fz_point p = fz_transform_point(p0, page_to_dev);
            return {p.x, p.y};
        };

        QPolygonF poly;
        poly.reserve(4);
        poly << toDev(q.ll) << toDev(q.lr) << toDev(q.ur) << toDev(q.ul);
        out.push_back(std::move(poly));
    }

    return out;
}

QString
Model::get_selected_text(int pageno, QPointF start, QPointF end,
                         bool formatted) noexcept
{
    std::string result;
    fz_page *page{nullptr};
    char *selection_text{nullptr};
    const float scale = logicalScale();
    fz_rect bounds;

    fz_try(m_ctx)
    {
        auto [w, h] = getPageDimensions(pageno);
        if (w < 0 || h < 0)
        {
            std::lock_guard<std::mutex> lock(m_doc_mutex);
            page   = fz_load_page(m_ctx, m_doc, pageno);
            bounds = fz_bound_page(m_ctx, page);
        }
        else
        {
            bounds = {0, 0, w, h};
        }

        auto page_to_dev = fz_scale(scale, scale);
        page_to_dev      = fz_pre_rotate(page_to_dev, m_rotation);

        const fz_rect dev_bounds = fz_transform_rect(bounds, page_to_dev);
        page_to_dev = fz_concat(page_to_dev,
                                fz_translate(-dev_bounds.x0, -dev_bounds.y0));

        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

        fz_point a = {float(start.x()), float(start.y())};
        fz_point b = {float(end.x()), float(end.y())};
        a          = fz_transform_point(a, dev_to_page);
        b          = fz_transform_point(b, dev_to_page);

        fz_stext_page *stext_page = get_or_build_stext_page(m_ctx, pageno);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

        selection_text = fz_copy_selection(m_ctx, stext_page, a, b, 0);
    }
    fz_always(m_ctx)
    {
        if (selection_text)
        {
            result = std::string(selection_text);
            fz_free(m_ctx, selection_text);
            if (!formatted)
                clean_pdf_text(result);
        }
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Failed to copy selection text";
    }

    return QString::fromStdString(result);
}

Model::Properties
Model::properties() noexcept
{
#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
    {
        if (!m_ddjvu_ctx || !m_ddjvu_doc)
            return {};

        Properties djvu_props;
        djvu_props.reserve(5); // typical number of metadata entries

        /* Fetch document-wide annotations.
           compat=1 also searches the shared annotation chunk
           so metadata is found in older files too. */
        miniexp_t anno;
        while ((anno = ddjvu_document_get_anno(m_ddjvu_doc, 1))
               == miniexp_dummy)
            handle_messages(m_ddjvu_ctx, 1);

        if (anno == miniexp_nil || anno == miniexp_symbol("failed")
            || anno == miniexp_symbol("stopped"))
        {
            return djvu_props;
        }

        /* Key/value metadata pairs */
        miniexp_t *keys = ddjvu_anno_get_metadata_keys(anno);
        if (keys)
        {
            for (int i = 0; keys[i]; i++)
            {
                const char *key = miniexp_to_name(keys[i]);
                const char *val = ddjvu_anno_get_metadata(anno, keys[i]);
                if (key && val)
                    djvu_props.emplace_back(key, val);
            }
            free(keys);
        }

        /* XMP metadata blob (if present) */
        const char *xmp = ddjvu_anno_get_xmp(anno);
        if (xmp)
            djvu_props.emplace_back("XMP", xmp);

        ddjvu_miniexp_release(m_ddjvu_doc, anno);
        return djvu_props;
    }
#endif

    if (!m_ctx || !m_doc)
        return {};

    Properties props;
    props.reserve(16); // Typical number of PDF properties

    props.push_back(qMakePair("File Path", m_filepath));
    props.push_back(
        qMakePair("Encrypted", fz_needs_password(m_ctx, m_doc) ? "Yes" : "No"));
    props.push_back(qMakePair("Page Count", QString::number(m_page_count)));

    if (m_pdf_doc)
        populatePDFProperties(props);

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

    // ========== Add Derived Properties ==========
    props.push_back(
        qMakePair("PDF Version", QString("%1.%2")
                                     .arg(m_pdf_doc->version / 10)
                                     .arg(m_pdf_doc->version % 10)));
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
    const float scale   = m_zoom * m_dpr * m_dpi;
    fz_matrix transform = fz_transform_page(bounds, scale, m_rotation);

    // Get the bbox (to find the origin shift)
    fz_rect transformed = fz_transform_rect(bounds, transform);
    fz_irect bbox       = fz_round_rect(transformed);

    // Adjust for Qt's Device Pixel Ratio and add bbox origin
    float physicalX = pixelPos.x() * m_dpr;
    float physicalY = pixelPos.y() * m_dpr;

    p.x = physicalX + bbox.x0;
    p.y = physicalY + bbox.y0;

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
    const float scale   = m_zoom * m_dpr * m_dpi;
    fz_matrix transform = fz_transform_page(bounds, scale, m_rotation);

    // Get the bbox (this is the key!)
    fz_rect transformed = fz_transform_rect(bounds, transform);
    fz_irect bbox       = fz_round_rect(transformed);

    // Transform point to device space and subtract bbox origin
    fz_point device_point = fz_transform_point(p, transform);
    float localX          = device_point.x - bbox.x0;
    float localY          = device_point.y - bbox.y0;

    // Adjust for Qt's Device Pixel Ratio
    return QPointF(localX / m_dpr, localY / m_dpr);
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
    job.colorspace   = m_colorspace;
    return job;
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
        // TODO: This is a hack, this shouldn't actually happen, check why it
        // happens, but for now, just guard against invalid futures.
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

    // auto future
    //     = QtConcurrent::run([this, job /*, callback */]() -> PageRenderResult
    // {
    //     if (m_render_cancelled.load())
    //         return {};
    //
    //     ensurePageCached(job.pageno);
    //
    //     if (m_render_cancelled.load())
    //         return {};
    //
    //     return renderPageWithExtrasAsync(job);
    // });
    //
    // watcher->setFuture(future);

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

#ifdef HAS_DJVU
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

        image.setDotsPerMeterX(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDotsPerMeterY(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDevicePixelRatio(job.dpr);

        result.image = std::move(image);
        // DjVu has no links or annotations — result.links/annotations stay
        // empty
        return result;
    }
#endif

    fz_context *ctx = cloneContext();
    if (!ctx)
        return result;

    fz_display_list *dlist{nullptr};
    fz_rect bounds{};
    std::vector<CachedLink> links;
    std::vector<CachedAnnotation> annotations;

    fz_try(ctx)
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);

        if (!m_page_lru_cache.has(job.pageno))
        {
            qWarning() << "Model::PageRenderResult() Page not cached:"
                       << job.pageno;
            return result;
        }

        const PageCacheEntry *entry = m_page_lru_cache.get(job.pageno);
        if (!entry->display_list)
        {
            qWarning() << "Model::PageRenderResult() Missing display list for:"
                       << job.pageno;
            return result;
        }

        // Increment reference count so the display list stays valid
        dlist  = fz_keep_display_list(ctx, entry->display_list);
        bounds = entry->bounds;

        links.reserve(entry->links.size());
        links = entry->links;
        annotations.reserve(entry->annotations.size());
        annotations = entry->annotations;
    }
    fz_always(ctx)
    {
        // We will drop the context at the end of this function, which will
        // also drop the display list reference we just kept. If we failed to
        // keep the display list, dropping a null pointer is safe.
    }
    fz_catch(ctx)
    {
        qWarning() << "Failed to retrieve page cache for rendering:"
                   << job.pageno << ":" << fz_caught_message(ctx);
        fz_drop_context(ctx);
        return result;
    }

    fz_link *head{nullptr};
    fz_pixmap *pix{nullptr};
    fz_device *dev{nullptr};
    fz_device *tracker{nullptr};

    fz_try(ctx)
    {
        fz_matrix transform = fz_transform_page(bounds, job.zoom, job.rotation);
        fz_rect transformed = fz_transform_rect(bounds, transform);
        fz_irect bbox       = fz_round_rect(transformed);

        // // --- Render page to QImage ---
        pix = fz_new_pixmap_with_bbox(ctx, job.colorspace, bbox, nullptr, 1);
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
        {
            fz_invert_pixmap(ctx, pix);

            if (m_config.behavior.dont_invert_images && supports_image_blocks()
                && tracker)
            {
                restore_image_regions(
                    ctx, pix,
                    reinterpret_cast<fz_image_tracker_device *>(tracker),
                    m_colorspace);
            }
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

        QImage image(width, height, fmt);

        std::memcpy(image.bits(), samples, stride * height);

        image.setDotsPerMeterX(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDotsPerMeterY(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDevicePixelRatio(job.dpr);
        result.image = image;

        // --- Extract links ---
        for (const auto &link : links)
        {
            if (link.uri.isEmpty())
                continue;
            fz_rect r         = fz_transform_rect(link.rect, transform);
            const float scale = m_inv_dpr;
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

        // fz_stext_page *stext_page{nullptr};
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

        for (const auto &annot : annotations)
        {
            RenderAnnotation renderAnnot;

            fz_rect r         = fz_transform_rect(annot.rect, transform);
            const float scale = m_inv_dpr;
            QRectF qtRect(r.x0 * scale, r.y0 * scale, (r.x1 - r.x0) * scale,
                          (r.y1 - r.y0) * scale);
            renderAnnot.rect  = qtRect;
            renderAnnot.type  = annot.type;
            renderAnnot.index = annot.index;
            renderAnnot.color = annot.color;
            renderAnnot.text  = annot.text;
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

void
Model::highlight_text_selection(int pageno, QPointF start, QPointF end) noexcept
{
    constexpr int MAX_HITS{1000};
    fz_quad hits[MAX_HITS];
    int count{0};
    fz_page *page{nullptr};
    const float scale = logicalScale();
    fz_rect bounds;

    fz_try(m_ctx)
    {
        auto [w, h] = getPageDimensions(pageno);
        if (w < 0 || h < 0)
        {
            std::lock_guard<std::mutex> lock(m_doc_mutex);
            page   = fz_load_page(m_ctx, m_doc, pageno);
            bounds = fz_bound_page(m_ctx, page);
        }
        else
        {
            bounds = {0, 0, w, h};
        }

        auto page_to_dev = fz_scale(scale, scale);
        page_to_dev      = fz_pre_rotate(page_to_dev, m_rotation);

        const fz_rect dev_bounds = fz_transform_rect(bounds, page_to_dev);
        page_to_dev = fz_concat(page_to_dev,
                                fz_translate(-dev_bounds.x0, -dev_bounds.y0));

        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

        fz_point a = {float(start.x()), float(start.y())};
        fz_point b = {float(end.x()), float(end.y())};

        a                         = fz_transform_point(a, dev_to_page);
        b                         = fz_transform_point(b, dev_to_page);
        fz_stext_page *stext_page = get_or_build_stext_page(m_ctx, pageno);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "failed to load stext page");

        count = highlight_selection(stext_page, a, b, hits, MAX_HITS);
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Failed to copy selection text";
    }

    // Collect quads for the command
    std::vector<fz_quad> quads;
    quads.reserve(count);
    for (int i = 0; i < count; ++i)
        quads.push_back(hits[i]);

    // // Create and push the command onto the undo stack for undo/redo
    // support
    m_undo_stack->push(
        new TextHighlightAnnotationCommand(this, pageno, std::move(quads)));
}

int
Model::addHighlightAnnotation(const int pageno,
                              const std::vector<fz_quad> &quads,
                              const QString &content) noexcept
{
    int objNum{-1};

#ifndef NDEBUG
    qDebug() << "Model::addHighlightAnnotation(); Adding highlight for page = "
             << pageno;
#endif

    if (quads.empty())
        return objNum;

    pdf_annot *annot{nullptr};
    pdf_page *page{nullptr};

    fz_try(m_ctx)
    {
        // Load the specific page for this annotation
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        // Create a separate highlight annotation for each quad
        // This looks better visually for multi-line selections
        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_HIGHLIGHT);
        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_set_annot_quad_points(m_ctx, annot, quads.size(), &quads[0]);
        pdf_set_annot_color(m_ctx, annot, 3, m_highlight_color);
        pdf_set_annot_opacity(m_ctx, annot, m_highlight_color[3]);
        pdf_set_annot_contents(m_ctx, annot, content.toUtf8().constData());

        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        // Store the object number for later undo
        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (obj)
            objNum = pdf_to_num(m_ctx, obj);
    }
    fz_always(m_ctx)
    {
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Redo failed:" << fz_caught_message(m_ctx);
        return objNum;
    }

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }

#ifndef NDEBUG
    qDebug() << "Adding highlight annotation on page" << pageno
             << " Quad count:" << quads.size() << " ObjNum:" << objNum;
#endif
    return objNum;
}

int
Model::addRectAnnotation(const int pageno, const fz_rect &rect,
                         const QString &content) noexcept
{
    int objNum{-1};
    pdf_annot *annot{nullptr};
    pdf_page *page{nullptr};

    fz_try(m_ctx)
    {
        // Load the specific page for this annotation
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_SQUARE);

        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_set_annot_rect(m_ctx, annot, rect);
        pdf_set_annot_interior_color(m_ctx, annot, 3, m_annot_rect_color);
        pdf_set_annot_color(m_ctx, annot, 3, m_annot_rect_color);
        pdf_set_annot_opacity(m_ctx, annot, m_annot_rect_color[3]);

        if (!content.isEmpty())
            pdf_set_annot_contents(m_ctx, annot, content.toUtf8().constData());

        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        // Store the object number for later undo
        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (!obj)
            fz_throw(m_ctx, FZ_ERROR_GENERIC,
                     "Failed to get annotation object");

        objNum = pdf_to_num(m_ctx, obj);
        // TODO: pdf_drop_obj(m_ctx, obj); (CHECK)
    }
    fz_always(m_ctx)
    {
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Redo failed:" << fz_caught_message(m_ctx);
        return objNum;
    }

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }

#ifndef NDEBUG
    qDebug() << "Adding rect annotation on page" << pageno
             << " ObjNum:" << objNum;
#endif

    return objNum;
}

int
Model::addTextAnnotation(const int pageno, const fz_rect &rect,
                         const QString &text) noexcept
{
    int objNum{-1};

    if (text.isEmpty())
        return objNum;

    pdf_annot *annot{nullptr};
    pdf_page *page{nullptr};

    fz_try(m_ctx)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        // Load the specific page for this annotation
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        // Create a text (sticky note) annotation
        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_TEXT);

        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_set_annot_rect(m_ctx, annot, rect);
        pdf_set_annot_color(m_ctx, annot, 3, m_popup_color);
        pdf_set_annot_opacity(m_ctx, annot, m_popup_color[3]);

        // Set the annotation contents (the text that appears in the popup)
        pdf_set_annot_contents(m_ctx, annot, text.toUtf8().constData());

        // Set the annotation to be open by default (optional)
        // pdf_set_annot_is_open(m_ctx, annot, 0);

        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        // Store the object number for later undo
        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (obj)
            objNum = pdf_to_num(m_ctx, obj);
    }
    fz_always(m_ctx)
    {
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {

        qWarning() << "addTextAnnotation failed:" << fz_caught_message(m_ctx);
    }

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }

#ifndef NDEBUG
    qDebug() << "Adding text annotation on page" << pageno
             << " ObjNum:" << objNum;
#endif
    return objNum;
}

QString
Model::getAnnotComment(const int pageno, const int objNum) noexcept
{
    QString comment;
    pdf_page *page{nullptr};
    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != objNum)
                continue;

            comment = QString::fromUtf8(pdf_annot_contents(m_ctx, annot));
            break;
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getAnnotComment failed:" << fz_caught_message(m_ctx);
    }

    return comment;
}

void
Model::addAnnotComment(const int pageno, const int objNum,
                       const QString &text) noexcept
{
    bool changed{false};
    pdf_page *page{nullptr};

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != objNum)
                continue;

            const QByteArray utf8 = text.toUtf8();
            pdf_set_annot_contents(m_ctx, annot, utf8.constData());
            pdf_update_annot(m_ctx, annot);
            pdf_update_page(m_ctx, page);
            changed = true;
            break;
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "addAnnotComment failed:" << fz_caught_message(m_ctx);
        return;
    }

    if (changed)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
}

void
Model::setUrlLinkRegex(const QString &pattern) noexcept
{
    const QString defaultPattern
        = QString::fromUtf8(R"((https?://|www\.)[^\s<>()\"']+)");
    const QString effectivePattern
        = pattern.isEmpty() ? defaultPattern : pattern;
    QRegularExpression re(effectivePattern);
    re.optimize();

    if (!re.isValid())
    {
        qWarning() << "Invalid url_regex:" << re.errorString();
        re = QRegularExpression(defaultPattern);
    }
    m_url_link_re = re;
}

void
Model::removeAnnotations(int pageno, const std::vector<int> &objNums) noexcept
{
    if (objNums.empty())
        return;

    // Build fast lookup set
    std::unordered_set<int> to_delete;
    to_delete.reserve(objNums.size());
    for (int n : objNums)
        to_delete.insert(n);

    pdf_page *page{nullptr};

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        bool changed = false;

        // Safe iteration pattern: grab next before deleting current
        for (pdf_annot *a = pdf_first_annot(m_ctx, page); a;)
        {
            pdf_annot *next = pdf_next_annot(m_ctx, a);

            pdf_obj *obj  = pdf_annot_obj(m_ctx, a);
            const int num = obj ? pdf_to_num(m_ctx, obj) : 0;

            if (num != 0 && to_delete.find(num) != to_delete.end())
            {
                pdf_delete_annot(m_ctx, page, a);
                changed = true;
            }

            a = next;
        }

        if (changed)
        {
#ifndef NDEBUG
            qDebug() << "Removed annotations on page" << pageno
                     << " Count:" << objNums.size();
#endif
            // Update once
            pdf_update_page(m_ctx, page);

            invalidatePageCache(pageno);
            emit reloadRequested(pageno);
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "removeAnnotations failed:" << fz_caught_message(m_ctx);
    }
}

void
Model::invalidatePageCache(int pageno) noexcept
{
    std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    if (m_page_lru_cache.has(pageno))
    {
        // m_page_cache.erase(pageno);
        m_page_lru_cache.remove(pageno);
    }

    // Also clear text cache for this page to save memory
    if (m_text_cache.has(pageno))
        m_text_cache.remove(pageno);
}

void
Model::invalidatePageCaches() noexcept
{
    std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    m_page_lru_cache.clear();
    m_text_cache.clear();
    m_stext_page_cache.clear();

    std::lock_guard<std::mutex> lk(m_page_dim_mutex);
    m_page_dim_cache.reset(m_page_count);
}

std::vector<QPolygonF>
Model::selectAtHelper(int pageno, fz_point pt, int snapMode) noexcept
{
    std::vector<QPolygonF> out;
    constexpr int MAX_HITS{1024};
    thread_local std::array<fz_quad, MAX_HITS> hits;
    const float scale = logicalScale();
    fz_rect bounds;

    auto [w, h] = getPageDimensions(pageno);
    if (w < 0 || h < 0)
    {
        fz_page *page = fz_load_page(m_ctx, m_doc, pageno);
        bounds        = fz_bound_page(m_ctx, page);
        fz_drop_page(m_ctx, page);
    }
    else
    {
        bounds = {0, 0, w, h};
    }

    fz_matrix page_to_dev    = fz_scale(scale, scale);
    page_to_dev              = fz_pre_rotate(page_to_dev, m_rotation);
    const fz_rect dev_bounds = fz_transform_rect(bounds, page_to_dev);
    page_to_dev
        = fz_concat(page_to_dev, fz_translate(-dev_bounds.x0, -dev_bounds.y0));
    const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

    fz_point a = fz_transform_point(pt, dev_to_page);
    fz_point b = a;

    int count = 0;
    fz_try(m_ctx)
    {
        fz_stext_page *stext_page = get_or_build_stext_page(m_ctx, pageno);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load stext page");

        fz_snap_selection(m_ctx, stext_page, &a, &b, snapMode);
        count = highlight_selection(stext_page, a, b, hits.data(), MAX_HITS);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Selection failed:" << fz_caught_message(m_ctx);
        return out;
    }

    out.reserve(count);
    auto toDev = [&](const fz_point &p0) -> QPointF
    {
        const fz_point p = fz_transform_point(p0, page_to_dev);
        return QPointF(p.x, p.y);
    };

    for (int i = 0; i < count; ++i)
    {
        const fz_quad &q = hits[i];
        QPolygonF poly;
        poly.reserve(4);
        poly << toDev(q.ll) << toDev(q.lr) << toDev(q.ur) << toDev(q.ul);
        out.push_back(std::move(poly));
    }

    return out;
}

std::vector<QPolygonF>
Model::selectWordAt(int pageno, fz_point pt) noexcept
{
    return selectAtHelper(pageno, pt, FZ_SELECT_WORDS);
}

std::vector<QPolygonF>
Model::selectLineAt(int pageno, fz_point pt) noexcept
{
    return selectAtHelper(pageno, pt, FZ_SELECT_LINES);
}

std::vector<QPolygonF>
Model::selectParagraphAt(int pageno, fz_point pt) noexcept
{
    std::vector<QPolygonF> out;
    constexpr int MAX_HITS = 1024;
    thread_local std::array<fz_quad, MAX_HITS> hits;
    const float scale = logicalScale();
    fz_rect bounds;

    fz_try(m_ctx)
    {
        auto [w, h] = getPageDimensions(pageno);
        if (w < 0 || h < 0)
        {
            // std::lock_guard<std::mutex> lock(m_doc_mutex);
            fz_page *page = fz_load_page(m_ctx, m_doc, pageno);
            bounds        = fz_bound_page(m_ctx, page);
            fz_drop_page(m_ctx, page);
        }
        else
        {
            bounds = {0, 0, w, h};
        }

        fz_matrix page_to_dev    = fz_scale(scale, scale);
        page_to_dev              = fz_pre_rotate(page_to_dev, m_rotation);
        const fz_rect dev_bounds = fz_transform_rect(bounds, page_to_dev);
        page_to_dev = fz_concat(page_to_dev,
                                fz_translate(-dev_bounds.x0, -dev_bounds.y0));
        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);
        fz_point page_pt            = fz_transform_point(pt, dev_to_page);
        fz_stext_page *stext_page   = get_or_build_stext_page(m_ctx, pageno);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "failed to load stext page");

        for (fz_stext_block *block = stext_page->first_block; block;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;

            if (page_pt.x >= block->bbox.x0 && page_pt.x <= block->bbox.x1
                && page_pt.y >= block->bbox.y0 && page_pt.y <= block->bbox.y1)
            {
                fz_point blockStart = {block->bbox.x0, block->bbox.y0};
                fz_point blockEnd   = {block->bbox.x1, block->bbox.y1};

                int count = highlight_selection(
                    stext_page, blockStart, blockEnd, hits.data(), MAX_HITS);

                auto toDev = [&](const fz_point &p0) -> QPointF
                {
                    const fz_point p = fz_transform_point(p0, page_to_dev);
                    return QPointF(p.x, p.y);
                };

                out.reserve(count);
                for (int i = 0; i < count; ++i)
                {
                    const fz_quad &q = hits[i];
                    QPolygonF poly;
                    poly.reserve(4);
                    poly << toDev(q.ll) << toDev(q.lr) << toDev(q.ur)
                         << toDev(q.ul);
                    out.push_back(std::move(poly));
                }

                // m_selection_start = blockStart;
                // m_selection_end   = blockEnd;
                break;
            }
        }
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Quadruple-click paragraph selection failed";
    }

    return out;
}

// Returns {page_to_dev, dev_to_page}, or {identity, identity} on failure
std::pair<fz_matrix, fz_matrix>
Model::buildPageTransforms(int pageno) const noexcept
{
    const fz_matrix identity{fz_identity};
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

    const float scale     = logicalScale();
    fz_matrix page_to_dev = fz_scale(scale, scale);
    page_to_dev           = fz_pre_rotate(page_to_dev, m_rotation);
    const fz_rect dbox    = fz_transform_rect(bounds, page_to_dev);
    page_to_dev = fz_concat(page_to_dev, fz_translate(-dbox.x0, -dbox.y0));
    return {page_to_dev, fz_invert_matrix(page_to_dev)};
}

void
Model::searchCancel() noexcept
{
    if (m_search_future.isRunning())
    {
        m_search_cancelled.store(true);
        m_search_future.cancel();
        m_search_future.waitForFinished();
    }
}

void
Model::search(const QString &term, bool caseSensitive, int pageFrom,
              bool use_regex) noexcept
{
    if (m_search_future.isRunning())
    {
        m_search_cancelled.store(true);
        m_search_future.cancel();
        m_search_future.waitForFinished();
    }
    m_search_cancelled.store(false);

    // Copy everything the lambda needs — no 'this' access in the thread

    m_search_future
        = QtConcurrent::run([this, pageFrom, page_count = m_page_count,
                             progressive = m_config.search.progressive,
                             use_regex, term, caseSensitive]() mutable
    {
        if (m_search_cancelled.load())
            return;

        if (term.isEmpty())
        {
            if (!m_search_cancelled.load())
                emit searchResultsReady({});
            return;
        }

        QRegularExpression re;
        if (use_regex)
        {
            QRegularExpression::PatternOptions opts
                = QRegularExpression::UseUnicodePropertiesOption;
            if (!caseSensitive)
                opts |= QRegularExpression::CaseInsensitiveOption;
            re = QRegularExpression(term, opts);
            if (!re.isValid())
                return;
            re.optimize();
        }

        constexpr int BATCH = 64;
        QMap<int, std::vector<SearchHit>> results;
        int total = 0;

        if (pageFrom == -1)
            pageFrom = 0;
        for (int batch_start = pageFrom;
             batch_start < page_count && !m_search_cancelled.load();
             batch_start += BATCH)
        {
            const int batch_end = std::min(batch_start + BATCH, page_count);

            std::set<int> batchPages;
            for (int p = batch_start; p < batch_end; ++p)
                batchPages.insert(p);

            buildTextCacheForPages(batchPages);

            if (m_search_cancelled.load())
                return;

            QList<int> batchList(batchPages.begin(), batchPages.end());
            auto future
                = QtConcurrent::mapped(batchList, [this, use_regex, re, term,
                                                   caseSensitive](int pageno)
            {
                if (m_search_cancelled.load())
                    return std::vector<SearchHit>{};
                return use_regex ? searchHelperRegex(pageno, re)
                                 : searchHelper(pageno, term, caseSensitive);
            });

            while (!future.isFinished())
            {
                if (m_search_cancelled.load())
                {
                    future.cancel();
                    future.waitForFinished();
                    return;
                }
                QThread::msleep(5);
            }

            if (m_search_cancelled.load())
                return;

            QMap<int, std::vector<SearchHit>> batchResults;
            for (int i = 0; i < batchList.size(); ++i)
            {
                auto hits = future.resultAt(i);
                if (!hits.empty())
                {
                    total += static_cast<int>(hits.size());
                    batchResults.insert(batchList[i], std::move(hits));
                }
            }

            for (auto it = batchResults.cbegin(); it != batchResults.cend();
                 ++it)
                results.insert(it.key(), it.value());

            if (m_search_cancelled.load())
                return;

            if (progressive && !batchResults.isEmpty())
            {
                emit searchPartialResultsReady(batchResults);
            }
        }

        if (m_search_cancelled.load())
            return;

        m_search_match_count = total;
        emit searchResultsReady(results);
    });
}

void
Model::searchInPage(const int pageno, const QString &term,
                    bool caseSensitive) noexcept
{
    QFuture<void> result
        = QtConcurrent::run([this, pageno, term, caseSensitive]()
    {
        QMap<int, std::vector<Model::SearchHit>> results;
        m_search_match_count = 0;

        if (term.isEmpty() || pageno < 0 || pageno >= m_page_count)
        {
            emit searchResultsReady(results);
            return;
        }

        auto hits = searchHelper(pageno, term, caseSensitive);
        if (!hits.empty())
        {
            m_search_match_count += hits.size();
            results.insert(pageno, std::move(hits));
        }
        emit searchResultsReady(results);
    });
}

std::vector<Model::SearchHit>
Model::searchHelper(int pageno, const QString &term,
                    bool caseSensitive) noexcept
{
    std::vector<SearchHit> results;
    if (term.isEmpty())
        return results;

    std::vector<CachedTextChar> text;
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        if (!m_text_cache.has(pageno))
            return results;
        text = m_text_cache.get(pageno)->chars;
    }
    const int n = text.size();
    const int m = term.size();

    if (n < m)
        return results;

    // Convert search term once
    std::vector<uint32_t> pattern;
    pattern.reserve(m);
    for (QChar c : term)
        pattern.push_back(c.unicode());

    for (int i = 0; i <= n - m; ++i)
    {
        bool match = true;

        for (int j = 0; j < m; ++j)
        {
            if (!charEqual(text[i + j].rune, pattern[j], caseSensitive))
            {
                match = false;
                break;
            }
        }

        if (!match)
            continue;

        // Compute **single quad for entire match**
        fz_rect bbox = fz_empty_rect;
        for (int j = 0; j < m; ++j)
        {
            if (!fz_is_empty_quad(text[i + j].quad))
            {
                bbox = fz_union_rect(bbox, fz_rect_from_quad(text[i + j].quad));
            }
        }

        if (!fz_is_empty_rect(bbox))
        {
            results.push_back({pageno, fz_quad_from_rect(bbox), i});
        }
    }

    return results;
}

std::vector<Model::SearchHit>
Model::searchHelperRegex(int pageno, const QRegularExpression &re) noexcept
{
    std::vector<SearchHit> results;

    std::vector<CachedTextChar> text;
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        if (!m_text_cache.has(pageno))
            return results;
        text = m_text_cache.get(pageno)->chars;
    }

    const int n = static_cast<int>(text.size());

    // Build flat string + position map
    QString flat;
    flat.reserve(n);
    std::vector<int> strToChar;
    strToChar.reserve(n);

    for (int i = 0; i < n; ++i)
    {
        const char32_t r = text[i].rune;
        const QString ch
            = (r == '\n') ? QStringLiteral("\n") : QString::fromUcs4(&r);
        for (QChar qc : ch)
        {
            flat.append(qc);
            strToChar.push_back(i);
        }
    }

    // Match and map back to quads
    auto it = re.globalMatch(flat);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int strStart                  = match.capturedStart();
        const int strEnd                    = match.capturedEnd() - 1;

        if (strStart < 0 || strEnd >= static_cast<int>(strToChar.size()))
            continue;

        const int charStart = strToChar[strStart];
        const int charEnd   = strToChar[strEnd];

        fz_rect bbox = fz_empty_rect;
        for (int k = charStart; k <= charEnd; ++k)
            if (!fz_is_empty_quad(text[k].quad))
                bbox = fz_union_rect(bbox, fz_rect_from_quad(text[k].quad));

        if (!fz_is_empty_rect(bbox))
            results.push_back({pageno, fz_quad_from_rect(bbox), charStart});
    }

    return results;
}

std::vector<Model::AnnotCommentInfo>
Model::collect_annot_comments() noexcept
{
    std::vector<AnnotCommentInfo> results;

    if (!m_ctx || !m_doc || !m_pdf_doc)
        return results;

    for (int pageno = 0; pageno < m_page_count; ++pageno)
    {
        pdf_page *pdfPage{nullptr};
        fz_try(m_ctx)
        {
            pdfPage = pdf_load_page(m_ctx, m_pdf_doc, pageno);
            if (!pdfPage)
                fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

            for (pdf_annot *annot = pdf_first_annot(m_ctx, pdfPage); annot;
                 annot            = pdf_next_annot(m_ctx, annot))
            {
                if (auto *content = pdf_annot_contents(m_ctx, annot);
                    content && content[0] != '\0')
                {
                    results.push_back({pageno, QString(content),
                                       pdf_annot_rect(m_ctx, annot)});
                }
            }
        }
        fz_always(m_ctx)
        {
            pdf_drop_page(m_ctx, pdfPage);
        }
        fz_catch(m_ctx)
        {
            qWarning() << "Failed to collect comments for annotations on page"
                       << pageno;
        }
    }

    return results;
}

std::vector<Model::HighlightText>
Model::collectHighlightTexts(bool groupByLine) noexcept
{
    std::vector<HighlightText> results;

    if (!m_ctx || !m_doc || !m_pdf_doc)
        return results;

    for (int pageno = 0; pageno < m_page_count; ++pageno)
    {
        pdf_page *pdfPage{nullptr};
        fz_stext_page *stext_page{nullptr};

        fz_try(m_ctx)
        {
            pdfPage = pdf_load_page(m_ctx, m_pdf_doc, pageno);
            if (!pdfPage)
                fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

            stext_page = fz_new_stext_page_from_page(m_ctx, (fz_page *)pdfPage,
                                                     nullptr);

            if (!stext_page)
                continue;

            for (pdf_annot *annot = pdf_first_annot(m_ctx, pdfPage); annot;
                 annot            = pdf_next_annot(m_ctx, annot))
            {
                if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_HIGHLIGHT)
                    continue;

                const int quad_count = pdf_annot_quad_point_count(m_ctx, annot);
                if (quad_count <= 0)
                    continue;

                std::vector<fz_quad> quads;
                quads.reserve(quad_count);
                for (int i = 0; i < quad_count; ++i)
                    quads.push_back(pdf_annot_quad_point(m_ctx, annot, i));

                std::vector<fz_quad> line_quads;
                if (groupByLine)
                    line_quads = merge_quads_by_line(quads);
                else
                    line_quads = merged_quads_from_quads(quads);

                for (const fz_quad &q : line_quads)
                {
                    fz_rect rect = fz_rect_from_quad(q);
                    if (fz_is_infinite_rect(rect) || fz_is_empty_rect(rect))
                        continue;

                    const fz_point a{rect.x0, rect.y0};
                    const fz_point b{rect.x1, rect.y1};
                    char *selection_text
                        = fz_copy_selection(m_ctx, stext_page, a, b, 0);
                    if (!selection_text)
                        continue;

                    QString text = QString::fromUtf8(selection_text).trimmed();
                    fz_free(m_ctx, selection_text);

                    if (text.isEmpty())
                        continue;

                    results.push_back({pageno, text, q});
                }
            }
        }
        fz_always(m_ctx)
        {
            pdf_drop_page(m_ctx, pdfPage);
            fz_drop_stext_page(m_ctx, stext_page);
        }
        fz_catch(m_ctx)
        {
            qWarning() << "Failed to collect highlight text on page" << pageno;
        }
    }

    return results;
}

void
Model::buildTextCacheForPages(const std::set<int> &pagenos) noexcept
{
    if (pagenos.empty())
        return;

    // TODO: Support text selection for DJVU

    fz_context *ctx = cloneContext();
    if (!ctx)
        return;

    for (int pageno : pagenos)
    {
        if (m_text_cache.has(pageno))
            continue;

        fz_try(ctx)
        {
            fz_stext_page *stext = get_or_build_stext_page(ctx, pageno);

            CachedTextPage cache{};
            cache.chars.reserve(4096); // pre-reserve to reduce reallocations

            for (fz_stext_block *b = stext->first_block; b; b = b->next)
            {
                if (b->type != FZ_STEXT_BLOCK_TEXT)
                    continue;

                for (fz_stext_line *l = b->u.t.first_line; l; l = l->next)
                {
                    for (fz_stext_char *c = l->first_char; c; c = c->next)
                    {
                        cache.chars.push_back(
                            {static_cast<uint32_t>(c->c), c->quad});
                    }

                    // logical line break (prevents cross-line matches)
                    cache.chars.push_back({'\n', {}});
                }
            }

            std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
            m_text_cache.put(pageno, std::move(cache));
        }
        fz_catch(ctx) {}
    }

    fz_drop_context(ctx);
}

void
Model::annotChangeColor(int pageno, int index, const QColor &color) noexcept
{
    if (!m_pdf_doc)
        return;

    bool changed = false;

    pdf_page *page{nullptr};

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != index)
                continue;

            const float rgb[3] = {color.redF(), color.greenF(), color.blueF()};
            switch (pdf_annot_type(m_ctx, annot))
            {
                case PDF_ANNOT_SQUARE:
                case PDF_ANNOT_TEXT:
                    pdf_set_annot_interior_color(m_ctx, annot, 3, rgb);
                    break;
                case PDF_ANNOT_HIGHLIGHT:
                    pdf_set_annot_color(m_ctx, annot, 3, rgb);
                    break;
                default:
                    break;
            }
            pdf_set_annot_opacity(m_ctx, annot, color.alphaF());
            pdf_update_annot(m_ctx, annot);
            pdf_update_page(m_ctx, page);
            changed = true;
            break;
        }

        if (!changed)
            qWarning() << "annotChangeColor: annotation not found, index:"
                       << index;
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "annotChangeColor failed:" << fz_caught_message(m_ctx);
        return;
    }

    if (changed)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
}

QColor
Model::getAnnotColor(const int pageno, const int objNum) noexcept
{
    QColor color;
    pdf_page *page{nullptr};

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != objNum)
                continue;

            int n{3};
            float rgb[3];
            switch (pdf_annot_type(m_ctx, annot))
            {
                case PDF_ANNOT_SQUARE:
                case PDF_ANNOT_TEXT:
                    pdf_annot_interior_color(m_ctx, annot, &n, rgb);
                    break;
                case PDF_ANNOT_HIGHLIGHT:
                    pdf_annot_color(m_ctx, annot, &n, rgb);
                    break;
                default:
                    break;
            }
            float alpha = pdf_annot_opacity(m_ctx, annot);
            color.setRgbF(rgb[0], rgb[1], rgb[2], alpha);
            break;
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getAnnotColor failed:" << fz_caught_message(m_ctx);
    }

    return color;
}

std::string
Model::getTextInArea(const int pageno, QPointF start, QPointF end) noexcept
{
    std::string result;

    const QRectF deviceRect = QRectF(start, end).normalized();
    if (deviceRect.isEmpty())
        return result;

    const float scale = logicalScale(); // does not include DPR or DPI,
                                        // since selection is in PDF points

    fz_stext_page *stext_page{nullptr};
    fz_page *page{nullptr};
    char *selection_text{nullptr};

    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, pageno);

        const fz_rect page_bounds = fz_bound_page(m_ctx, page);
        fz_matrix page_to_dev     = fz_scale(scale, scale);
        page_to_dev               = fz_pre_rotate(page_to_dev, m_rotation);
        const fz_rect dev_bounds  = fz_transform_rect(page_bounds, page_to_dev);
        page_to_dev = fz_concat(page_to_dev,
                                fz_translate(-dev_bounds.x0, -dev_bounds.y0));
        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

        fz_point p1 = fz_transform_point(
            {float(deviceRect.left()), float(deviceRect.top())}, dev_to_page);
        fz_point p2 = fz_transform_point(
            {float(deviceRect.right()), float(deviceRect.top())}, dev_to_page);
        fz_point p3 = fz_transform_point(
            {float(deviceRect.right()), float(deviceRect.bottom())},
            dev_to_page);
        fz_point p4 = fz_transform_point(
            {float(deviceRect.left()), float(deviceRect.bottom())},
            dev_to_page);

        const fz_rect rect = {std::min({p1.x, p2.x, p3.x, p4.x}),
                              std::min({p1.y, p2.y, p3.y, p4.y}),
                              std::max({p1.x, p2.x, p3.x, p4.x}),
                              std::max({p1.y, p2.y, p3.y, p4.y})};

        stext_page     = fz_new_stext_page_from_page(m_ctx, page, nullptr);
        selection_text = fz_copy_rectangle(m_ctx, stext_page, rect, 0);
    }
    fz_always(m_ctx)
    {
        if (selection_text)
        {
            result = selection_text;
            fz_free(m_ctx, selection_text);
        }
        fz_drop_stext_page(m_ctx, stext_page);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getTextInArea failed:" << fz_caught_message(m_ctx);
    }

    return result;
}

// std::optional<std::wstring>
// Model::get_paper_name_at_position(const int pageno, const fz_point pos)
// noexcept
// {
//     fz_stext_page *stext_page{nullptr};
//     fz_page *page{nullptr};

//     fz_try(m_ctx)
//     {
//         page       = fz_load_page(m_ctx, m_doc, pageno);
//         stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);
//     }
//     fz_always(m_ctx)
//     {
//         fz_drop_page(m_ctx, page);
//         fz_drop_stext_page(m_ctx, stext_page);
//     }
//     fz_catch(m_ctx)
//     {
//         return {};
//     }

//     if (!stext_page)
//         return {};

//     // 2) Flatten all characters
//     std::vector<fz_stext_char *> flat_chars;
//     flat_chars.reserve(4096);

//     for (fz_stext_block *b = stext_page->first_block; b; b = b->next)
//     {
//         if (b->type != FZ_STEXT_BLOCK_TEXT)
//             continue;

//         for (fz_stext_line *ln = b->u.t.first_line; ln; ln = ln->next)
//         {
//             for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next)
//                 flat_chars.push_back(ch);

//             // Add a sentinel "line break" marker by pushing nullptr
//             (optional),
//             // but we can also just treat end-of-line later via
//             // ch->next==nullptr. (We won't push nullptr here to keep it
//             // simple.)
//         }
//     }

//     if (flat_chars.empty())
//         return {};

//     // 3) Find index of the clicked character (point-in-rect with
//     epsilon) auto contains_point_eps = [&](fz_rect r) -> bool
//     {
//         // expand rect a bit so clicks don't have to be perfect
//         const float eps = 0.75f; // page units; tweak if needed
//         r.x0 -= eps;
//         r.y0 -= eps;
//         r.x1 += eps;
//         r.y1 += eps;
//         return (pos.x >= r.x0 && pos.x <= r.x1 && pos.y >= r.y0
//                 && pos.y <= r.y1);
//     };

//     int hit = -1;
//     for (int i = 0; i < (int)flat_chars.size(); ++i)
//     {
//         fz_stext_char *ch = flat_chars[i];
//         if (!ch)
//             continue;

//         const fz_rect r = fz_rect_from_quad(ch->quad);
//         if (contains_point_eps(r))
//         {
//             hit = i;
//             break;
//         }
//     }

//     if (hit < 0)
//         return {};

//     // 4) Expand to sentence-like chunk delimited by '.' (your original
//     intent) int left  = hit; int right = hit;

//     // Move left to char after previous '.'
//     while (left > 0)
//     {
//         fz_stext_char *ch = flat_chars[left - 1];
//         if (!ch)
//         {
//             --left;
//             continue;
//         }

//         if (ch->c == L'.')
//             break;

//         --left;
//     }

//     // Move right to next '.'
//     while (right < (int)flat_chars.size())
//     {
//         fz_stext_char *ch = flat_chars[right];
//         if (!ch)
//         {
//             ++right;
//             continue;
//         }

//         if (ch->c == L'.')
//             break;

//         ++right;
//     }

//     if (right <= left)
//         return {};

//     // 5) Build the string from [left, right) (excluding the '.')
//     std::wstring out;
//     out.reserve((size_t)(right - left) + 16);

//     for (int i = left; i < right; ++i)
//     {
//         fz_stext_char *ch = flat_chars[i];
//         if (!ch)
//             continue;

//         // Skip end-of-line hyphenation: hyphen at end of a line
//         if (ch->c == L'-' && ch->next == nullptr)
//             continue;

//         // Normal character
//         out.push_back((wchar_t)ch->c);

//         // If end of line, add a space (but avoid double spaces)
//         if (ch->next == nullptr)
//         {
//             if (!out.empty() && out.back() != L' ')
//                 out.push_back(L' ');
//         }
//     }

//     fz_drop_stext_page(m_ctx, stext_page);
//     trim_ws(out);

//     if (out.empty())
//         return {};

//     return out;
// }

// Logic for Model.cpp to get the first character's position
fz_point
Model::getFirstCharPos(const int pageno) noexcept
{
    fz_stext_page *stext_page{nullptr};
    fz_page *page{nullptr};

    fz_try(m_ctx)
    {
        page       = fz_load_page(m_ctx, m_doc, pageno);
        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

        for (fz_stext_block *block = stext_page->first_block; block;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;
            for (fz_stext_line *line = block->u.t.first_line; line;
                 line                = line->next)
            {
                for (fz_stext_char *span = line->first_char; span;
                     span                = span->next)
                {
                    if (span->size > 0)
                    {
                        // Return the origin of the first character in the
                        // first span
                        fz_drop_page(m_ctx, page);
                        fz_drop_stext_page(m_ctx, stext_page);
                        return span->origin;
                    }
                }
            }
        }
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
        fz_drop_stext_page(m_ctx, stext_page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getFirstCharPos failed:" << fz_caught_message(m_ctx);
    }

    return {0, 0};
}

// Detect URL-like text and return as links, excluding areas already covered
// by PDF links
std::vector<Model::RenderLink>
Model::detectUrlLinksForPage(const RenderJob &job) noexcept
{
    fz_context *ctx = cloneContext();
    if (!ctx)
        return {};

    std::vector<RenderLink> result;

    // Grab cached links under lock to check for intersections
    std::vector<CachedLink> cachedLinks;
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        const PageCacheEntry *entry = m_page_lru_cache.get(job.pageno);
        if (entry)
            cachedLinks = entry->links;
    }

    fz_matrix transform
        = fz_transform_page(fz_empty_rect, job.zoom,
                            job.rotation); // bounds not needed for stext

    fz_try(ctx)
    {
        // Get bounds for proper transform
        fz_rect bounds;
        auto [w, h] = getPageDimensions(job.pageno);
        if (w < 0 || h < 0)
        {
            std::lock_guard<std::mutex> lock(m_doc_mutex);
            fz_page *page = fz_load_page(ctx, m_doc, job.pageno);
            bounds        = fz_bound_page(ctx, page);
            fz_drop_page(ctx, page);
        }
        else
        {
            bounds = {0, 0, w, h};
        }

        transform = fz_transform_page(bounds, job.zoom, job.rotation);

        fz_stext_page *stext_page = get_or_build_stext_page(ctx, job.pageno);
        if (!stext_page)
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load stext page");

        const QRegularExpression &urlRe = m_url_link_re;

        for (fz_stext_block *b = stext_page->first_block; b; b = b->next)
        {
            if (b->type != FZ_STEXT_BLOCK_TEXT)
                continue;

            for (fz_stext_line *line = b->u.t.first_line; line;
                 line                = line->next)
            {
                QString lineText;
                lineText.reserve(256);
                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
                    lineText.append(QChar::fromUcs4(ch->c));

                if (lineText.isEmpty())
                    continue;

                QRegularExpressionMatchIterator it
                    = urlRe.globalMatch(lineText);
                while (it.hasNext())
                {
                    QRegularExpressionMatch match = it.next();
                    int start                     = match.capturedStart();
                    int len                       = match.capturedLength();
                    if (start < 0 || len <= 0)
                        continue;

                    QString raw = match.captured();
                    while (!raw.isEmpty()
                           && QString(".,;:!?)\"'").contains(raw.back()))
                    {
                        raw.chop(1);
                        --len;
                    }
                    if (raw.isEmpty() || len <= 0)
                        continue;

                    fz_quad q = getQuadForSubstring(line, start, len);
                    fz_rect r = fz_rect_from_quad(q);
                    if (fz_is_empty_rect(r))
                        continue;

                    // Skip if already covered by a PDF link
                    bool intersects = false;
                    for (const auto &cl : cachedLinks)
                    {
                        const fz_rect lr = cl.rect;
                        if (r.x1 >= lr.x0 && r.x0 <= lr.x1 && r.y1 >= lr.y0
                            && r.y0 <= lr.y1)
                        {
                            intersects = true;
                            break;
                        }
                    }
                    if (intersects)
                        continue;

                    QString uri = raw;
                    if (uri.startsWith("www."))
                        uri.prepend("https://");

                    fz_rect tr        = fz_transform_rect(r, transform);
                    const float scale = m_inv_dpr;
                    QRectF qtRect(tr.x0 * scale, tr.y0 * scale,
                                  (tr.x1 - tr.x0) * scale,
                                  (tr.y1 - tr.y0) * scale);

                    RenderLink renderLink;
                    renderLink.rect     = qtRect;
                    renderLink.uri      = uri;
                    renderLink.type     = BrowseLinkItem::LinkType::External;
                    renderLink.boundary = m_link_show_boundary;
                    result.push_back(std::move(renderLink));
                }
            }
        }
    }
    fz_catch(ctx) {}

    fz_drop_context(ctx);
    return result;
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

// LRU Cache stext page for performance boost
fz_stext_page *
Model::get_or_build_stext_page(fz_context *ctx, int pageno) noexcept
{
    {
        std::lock_guard lock(m_page_cache_mutex);
        if (m_stext_page_cache.has(pageno))
            return *m_stext_page_cache.get(pageno);
    }

    fz_page *page        = nullptr;
    fz_stext_page *stext = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        page  = fz_load_page(ctx, m_doc, pageno);
        stext = fz_new_stext_page_from_page(ctx, page, nullptr);
        fz_drop_page(ctx, page);
    }

    {
        std::lock_guard lock(m_page_cache_mutex);
        if (m_stext_page_cache.has(pageno)) // another thread beat us
        {
            fz_drop_stext_page(ctx, stext);
            return *m_stext_page_cache.get(pageno);
        }
        m_stext_page_cache.put(pageno, stext);
    }
    return stext;
}

std::vector<Model::VisualLineInfo>
Model::get_text_lines(int pageno) noexcept
{
    std::vector<VisualLineInfo> lines;
    fz_context *ctx = cloneContext();
    if (!ctx)
        return lines;

    fz_try(ctx)
    {
        fz_stext_page *stext = get_or_build_stext_page(ctx, pageno);
        if (!stext)
            fz_throw(ctx, FZ_ERROR_GENERIC, "no stext page");

        for (fz_stext_block *block = stext->first_block; block;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;

            for (fz_stext_line *line = block->u.t.first_line; line;
                 line                = line->next)
            {
                VisualLineInfo info;
                info.bbox   = QRectF(line->bbox.x0, line->bbox.y0,
                                     line->bbox.x1 - line->bbox.x0,
                                     line->bbox.y1 - line->bbox.y0);
                info.pageno = pageno;
                lines.push_back(info);
            }
        }
    }
    fz_catch(ctx)
    {
        lines.clear();
    }

    fz_drop_context(ctx);

    return lines;
}

int
Model::visual_line_index_at_pos(
    QPointF pos, const std::vector<VisualLineInfo> &lines) noexcept
{
    if (lines.empty())
        return -1;

    int closest   = -1;
    float minDist = std::numeric_limits<float>::max();

    for (size_t i = 0; i < lines.size(); ++i)
    {
        const auto &line = lines[i];

        // Exact vertical hit
        if (pos.y() >= line.bbox.top() && pos.y() <= line.bbox.bottom())
        {
            return i;
        }

        // Closest-by-vertical-distance fallback
        float dy = (pos.y() < line.bbox.top())
                       ? static_cast<float>(line.bbox.top() - pos.y())
                       : static_cast<float>(pos.y() - line.bbox.bottom());

        if (dy < minDist)
        {
            minDist = dy;
            closest = static_cast<int>(i);
        }
    }

    return closest;
}

Model::FileType
Model::getFileType(const QString &path) noexcept
{
    const QMimeType mime
        = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent);
    const QString name = mime.name();

    if (name == "application/pdf")
        return FileType::PDF;
    if (name == "application/epub+zip")
        return FileType::EPUB;
    if (name == "image/svg+xml")
        return FileType::SVG;
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
    if (name == "image/jpeg")
        return FileType::JPG;
    if (name == "image/png")
        return FileType::PNG;
    if (name == "image/tiff")
        return FileType::TIFF;
#ifdef HAS_DJVU
    if (name == "image/vnd.djvu" || name == "image/vnd.djvu+multipage"
        || name == "image/x-djvu")
        return FileType::DJVU;
#endif

    return FileType::NONE;
}

void
Model::setZoom(float zoom) noexcept
{
    m_zoom = zoom;

#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
    {
        invalidatePageCaches();
    }
#endif
}

void
Model::rotateClock() noexcept
{
    m_rotation += 90;
    if (m_rotation >= 360)
        m_rotation = 0;

#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
    {
        invalidatePageCaches();
    }
#endif
}

void
Model::rotateAnticlock() noexcept
{
    m_rotation -= 90;
    if (m_rotation < 0)
        m_rotation = 270;

#ifdef HAS_DJVU
    if (m_filetype == FileType::DJVU)
    {
        invalidatePageCaches();
    }
#endif
}

int
Model::get_obj_num_at_rect(int pageno, fz_rect targetRect) noexcept
{
    pdf_page *page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
    pdf_annot *annot{nullptr};
    int foundObjNum{-1};

    for (annot = pdf_first_annot(m_ctx, page); annot;
         annot = pdf_next_annot(m_ctx, annot))
    {
        fz_rect currentRect = pdf_annot_rect(m_ctx, annot);
        // Compare coordinates (with a tiny epsilon for float precision)
        if (std::abs(currentRect.x0 - targetRect.x0) < 0.001f
            && std::abs(currentRect.y0 - targetRect.y0) < 0.001f)
        {
            foundObjNum = pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot));
            break;
        }
    }

    pdf_drop_page(m_ctx, page);
    return foundObjNum;
}

void
Model::waitForPendingRenders() noexcept
{
    m_render_cancelled.store(true, std::memory_order_release);
    std::unique_lock<std::mutex> lock(m_renders_mutex);
    m_renders_cv.wait(lock, [this]
    { return m_active_renders.load(std::memory_order_acquire) == 0; });
}
