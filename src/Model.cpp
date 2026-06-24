#include "Model.hpp"

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
#include <limits>
#include <qbytearrayview.h>
#include <qregularexpression.h>
#include <qstyle.h>
#include <qtextformat.h>
#include <unordered_set>

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
static constexpr int DJVU_ROTATE_0      = 0;
static constexpr int DJVU_ROTATE_90     = 1;
static constexpr int DJVU_ROTATE_180    = 2;
static constexpr int DJVU_ROTATE_270    = 3;
static constexpr int DJVU_FMT_RGBMASK32 = 3; // DDJVU_FORMAT_RGBMASK32
static constexpr int DJVU_RENDER_COLOR  = 0;

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
        DJLOADSYM(doc_create, "ddjvu_document_create_by_filename")
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
                continue;

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

// Returns true if point p is inside the (possibly rotated) quad q.
// Uses a 2-D cross-product test on the four edges.
static bool
point_in_quad(fz_point p, fz_quad q)
{
    auto side = [](fz_point a, fz_point b, fz_point pt)
    {
        return (b.x - a.x) * (pt.y - a.y) - (b.y - a.y) * (pt.x - a.x);
    };
    return side(q.ul, q.ur, p) >= 0 && side(q.ur, q.lr, p) >= 0
           && side(q.lr, q.ll, p) >= 0 && side(q.ll, q.ul, p) >= 0;
}

// Extract the text of all stext chars whose centres lie inside any of `quads`.
static QString
text_from_quads(fz_stext_page *stext_page, const std::vector<fz_quad> &quads)
{
    QString result;
    for (fz_stext_block *block = stext_page->first_block; block;
         block                 = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;
        for (fz_stext_line *line = block->u.t.first_line; line;
             line                = line->next)
        {
            bool line_had_match = false;
            for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
            {
                fz_point centre{(ch->quad.ul.x + ch->quad.lr.x) * 0.5f,
                                (ch->quad.ul.y + ch->quad.lr.y) * 0.5f};
                for (const fz_quad &q : quads)
                {
                    if (point_in_quad(centre, q))
                    {
                        if (!result.isEmpty() && !line_had_match)
                            result.append(' ');
                        char buf[8];
                        int len = fz_runetochar(buf, ch->c);
                        result.append(QString::fromUtf8(buf, len));
                        line_had_match = true;
                        break;
                    }
                }
            }
        }
    }
    return result.trimmed();
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
        fz_pixmap *sub      = nullptr;
        fz_device *draw_dev = nullptr;

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

void
Model::initMuPDF() noexcept
{
    // initialize each mutex
    m_fz_locks.user   = mupdf_mutexes.data();
    m_fz_locks.lock   = mupdf_lock_mutex;
    m_fz_locks.unlock = mupdf_unlock_mutex;
    const size_t storeBytes
        = static_cast<size_t>(m_config.behavior.mupdf_store_size) << 20;
    m_ctx = fz_new_context(nullptr, &m_fz_locks, storeBytes);
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

void
Model::cleanup_image() noexcept
{
    m_image_cache = QImage();
    m_is_image    = false;
    m_is_animated = false;
    if (m_movie)
    {
        m_movie->stop();
        delete m_movie;
        m_movie = nullptr;
    }
    m_page_dim_cache.reset(0);
    m_default_page_dim = {};
}

void
Model::cleanup_djvu() noexcept
{
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
    }

    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        m_page_dim_cache.reset(0);
        m_default_page_dim = {};
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
    m_filepath              = QFileInfo(filePath).canonicalFilePath();
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
        const bool animated  = frameCount > 1;

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

        // Animated: hand off to QMovie — it decodes one frame at a time,
        // keeping memory at O(1 frame) instead of O(all frames).
        QMetaObject::invokeMethod(this, [this, canonPath, w, h]()
        {
            cleanup_image();
            m_is_image         = true;
            m_is_animated      = true;
            m_success          = true;
            m_page_count       = 1;
            m_default_page_dim = {w * 72.0f / m_dpi, h * 72.0f / m_dpi};
            m_page_dim_cache.dimensions.assign(1, m_default_page_dim);
            m_page_dim_cache.known.assign(1, true);
            m_movie = new QMovie(canonPath);
            m_movie->setCacheMode(QMovie::CacheNone);
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
        auto &djvu                 = DjVuLib::get();
        void *ctx                  = djvu.ctx_create("LEKTRA");
        const QByteArray pathBytes = canonPath.toUtf8();
        const std::string pathStr(pathBytes.constData(), pathBytes.size());
        void *doc = djvu.doc_create(ctx, pathStr.c_str(), true);
        if (!doc)
        {
            djvu.ctx_release(ctx);
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }

        // Pump until decoded (DDJVU_JOB_OK = 2)
        while (djvu.job_status(djvu.doc_job(doc)) < 2)
        {
            DjVuMsg *msg = djvu.msg_wait(ctx);
            if (msg->m_any.tag == DJVU_MSG_ERROR)
            {
                djvu.job_release(doc);
                djvu.ctx_release(ctx);
                QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                          Qt::QueuedConnection);
                return;
            }
            djvu.msg_pop(ctx);
        }

        const int page_count = djvu.doc_pagenum(doc);

        DjVuPageInfo info{};
        djvu.doc_pageinfo(doc, 0, &info);
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
    while (djvu.job_status(djvu.page_job(page)) < 2)
    {
        msg = djvu.msg_wait(m_ddjvu_ctx);
        if (msg->m_any.tag == DJVU_MSG_ERROR)
        {
            djvu.job_release(page);
            return;
        }
        djvu.msg_pop(m_ddjvu_ctx);
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

    // Render at m_zoom * m_dpi — same scale logic as the MuPDF path
    const float render_dpi = m_zoom * m_dpi * m_dpr;
    const float scale      = render_dpi / native_dpi;
    const int rw           = static_cast<int>(pw_px * scale);
    const int rh           = static_cast<int>(ph_px * scale);

    DjVuRect prect{0, 0, static_cast<unsigned>(rw), static_cast<unsigned>(rh)};
    DjVuRect rrect = prect;

    // BGRA format maps cleanly to QImage::Format_RGB32
    const int stride = rw * 4;
    QByteArray buf(stride * rh, 0);

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
                        pdf_annot_color(ctx, annot, &n, color);
                        ca.color = QColor::fromRgbF(color[0], color[1],
                                                    color[2], ca.opacity);
                        break;

                    case PDF_ANNOT_HIGHLIGHT:
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
        new_doc = fz_open_document(m_ctx, filePathStr.c_str());
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
    if (m_filetype == FileType::DJVU)
        return {};

    std::vector<QPolygonF> out;
    constexpr int MAX_HITS = 1024;
    thread_local std::array<fz_quad, MAX_HITS> hits;
    const float scale = logicalScale();

    fz_page *page = nullptr;
    int count     = 0;
    fz_rect page_bounds;
    fz_matrix page_to_dev;

    fz_try(m_ctx)
    {
        fz_stext_page *stext_page = get_or_build_stext_page(m_ctx, pageno);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

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

        page_to_dev = buildPageToDevMatrix(page_bounds, scale, m_rotation,
                                           m_flip_h, m_flip_v);

        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

        fz_point a = {float(devStart.x()), float(devStart.y())};
        fz_point b = {float(devEnd.x()), float(devEnd.y())};

        a = fz_transform_point(a, dev_to_page);
        b = fz_transform_point(b, dev_to_page);

        if (a.y > b.y || (qFuzzyCompare(a.y, b.y) && a.x > b.x))
        {
            std::swap(a, b);
        }

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
    fz_page *page        = nullptr;
    char *selection_text = nullptr;
    const float scale    = logicalScale();
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

        auto page_to_dev = buildPageToDevMatrix(bounds, scale, m_rotation,
                                                m_flip_h, m_flip_v);

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
           so metadata is found in older files too. */
        djvu_miniexp_t anno;
        while ((anno = djvu.doc_anno(m_ddjvu_doc, 1)) == djvu.dummy())
            handle_djvu_messages(m_ddjvu_ctx, 1);

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
    }

    else
    {
        if (!m_ctx || !m_doc)
            return props;

        props.push_back(qMakePair(
            "Encrypted", fz_needs_password(m_ctx, m_doc) ? "Yes" : "No"));
        props.push_back(qMakePair("Page Count", QString::number(m_page_count)));

        if (m_pdf_doc)
            populatePDFProperties(props);
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
        fz_rect transformed = fz_transform_rect(bounds, transform);
        fz_irect bbox       = fz_round_rect(transformed);

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

    // For raster sources (images, DjVu) there is no display list to re-render
    // from — return a null image so the caller can fall back to upscaling.
    if (m_is_image || m_filetype == FileType::DJVU)
        return {};

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

        result = QImage(width, height, fmt);
        std::memcpy(result.bits(), fz_pixmap_samples(ctx, pix),
                    stride * height);
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
        qWarning() << "renderRegionAtDPI failed:" << fz_caught_message(ctx);
        result = {};
    }

    fz_drop_context(ctx);
    return result;
}

void
Model::highlight_text_selection(int pageno, QPointF start, QPointF end,
                                const QString &comment) noexcept
{
    constexpr int MAX_HITS = 1000;
    fz_quad hits[MAX_HITS];
    int count         = 0;
    fz_page *page     = nullptr;
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

        auto page_to_dev = buildPageToDevMatrix(bounds, scale, m_rotation,
                                                m_flip_h, m_flip_v);

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
    m_undo_stack->push(new TextHighlightAnnotationCommand(
        this, pageno, std::move(quads), comment));
}

int
Model::addHighlightAnnotation(const int pageno,
                              const std::vector<fz_quad> &quads,
                              const QColor &color,
                              const QString &content) noexcept
{
    int objNum{-1};

#ifndef NDEBUG
    qDebug() << "Model::addHighlightAnnotation(); Adding highlight for page = "
             << pageno;
#endif

    if (quads.empty())
        return objNum;

    pdf_annot *annot = nullptr;
    pdf_page *page   = nullptr;

    fz_try(m_ctx)
    {
        // Load the specific page for this annotation
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_HIGHLIGHT);
        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_set_annot_quad_points(m_ctx, annot, quads.size(), &quads[0]);

        float mucolor[4] = {};
        if (color.isValid())
        {
            mucolor[0] = color.redF();
            mucolor[1] = color.greenF();
            mucolor[2] = color.blueF();
            mucolor[3] = color.alphaF();
        }
        else
        {
            mucolor[0] = m_highlight_color[0];
            mucolor[1] = m_highlight_color[1];
            mucolor[2] = m_highlight_color[2];
            mucolor[3] = m_highlight_color[3];
        }

        pdf_set_annot_color(m_ctx, annot, 3, mucolor);
        pdf_set_annot_opacity(m_ctx, annot, mucolor[3]);
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
    int objNum       = -1;
    pdf_annot *annot = nullptr;
    pdf_page *page   = nullptr;

    fz_try(m_ctx)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
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

    pdf_annot *annot = nullptr;
    pdf_page *page   = nullptr;

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
    pdf_page *page = nullptr;
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
    bool changed   = false;
    pdf_page *page = nullptr;

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
Model::removeAnnotComment(const int pageno, const int objNum) noexcept
{
    bool changed   = false;
    pdf_page *page = nullptr;

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

            pdf_set_annot_contents(m_ctx, annot, "");
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
        qWarning() << "removeAnnotComment failed:" << fz_caught_message(m_ctx);
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

    pdf_page *page = nullptr;

    fz_try(m_ctx)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
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
    m_page_lru_cache.remove(pageno);
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
    constexpr int MAX_HITS = 1024;
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

    fz_matrix page_to_dev
        = buildPageToDevMatrix(bounds, scale, m_rotation, m_flip_h, m_flip_v);
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

        fz_matrix page_to_dev = buildPageToDevMatrix(bounds, scale, m_rotation,
                                                     m_flip_h, m_flip_v);
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

        auto chars = m_text_cache.find(pageno);
        if (!chars)
            return results;

        text = chars->chars;
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
        auto chars = m_text_cache.find(pageno);
        if (!chars)
            return results;

        text = chars->chars;
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
        pdf_page *pdfPage = nullptr;

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
        pdf_page *pdfPage         = nullptr;
        fz_stext_page *stext_page = nullptr;

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

                const char *contents = pdf_annot_contents(m_ctx, annot);
                const QString comment
                    = contents ? QString::fromUtf8(contents).trimmed()
                               : QString{};

                std::vector<fz_quad> quads;
                quads.reserve(quad_count);
                for (int i = 0; i < quad_count; ++i)
                    quads.push_back(pdf_annot_quad_point(m_ctx, annot, i));

                std::vector<fz_quad> line_quads;
                if (groupByLine)
                    line_quads = merge_quads_by_line(quads);
                else
                    line_quads = merged_quads_from_quads(quads);

                QStringList parts;
                fz_quad anchor_quad{};

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

                    if (parts.isEmpty())
                        anchor_quad = q;
                    parts.append(text);
                }

                if (!parts.isEmpty())
                    results.push_back(
                        {pageno, parts.join(' '), comment, anchor_quad});
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

bool
Model::exportTextHighlights(const QString &path) noexcept
{
    const auto highlights = collectHighlightTexts();

    QJsonArray arr;
    for (const auto &h : highlights)
    {
        QJsonObject obj;
        obj["page"] = h.page + 1;
        obj["text"] = h.text;
        if (!h.comment.isEmpty())
            obj["comment"] = h.comment;
        arr.append(obj);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
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

    bool changed   = false;
    pdf_page *page = nullptr;

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
    pdf_page *page = nullptr;

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

QString
Model::getHighlightText(const int pageno, const int objNum) noexcept
{
    QString result;
    pdf_page *page = nullptr;

    fz_try(m_ctx)
    {
        fz_stext_page *stext_page = get_or_build_stext_page(m_ctx, pageno);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to get stext page");

        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != objNum)
                continue;
            if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_HIGHLIGHT)
                break;

            const int qc = pdf_annot_quad_point_count(m_ctx, annot);
            std::vector<fz_quad> quads;
            quads.reserve(qc);
            for (int i = 0; i < qc; ++i)
                quads.push_back(pdf_annot_quad_point(m_ctx, annot, i));

            result = text_from_quads(stext_page, quads);
            break;
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getHighlightText failed:" << fz_caught_message(m_ctx);
    }

    return result;
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

    fz_stext_page *stext_page = nullptr;
    fz_page *page             = nullptr;
    char *selection_text      = nullptr;

    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, pageno);

        const fz_rect page_bounds = fz_bound_page(m_ctx, page);
        fz_matrix page_to_dev     = buildPageToDevMatrix(
            page_bounds, scale, m_rotation, m_flip_h, m_flip_v);
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
    fz_stext_page *stext_page = nullptr;
    fz_page *page             = nullptr;
    fz_point result           = {0, 0};
    bool found                = false;

    fz_try(m_ctx)
    {
        page       = fz_load_page(m_ctx, m_doc, pageno);
        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

        for (fz_stext_block *block = stext_page->first_block; block && !found;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;
            for (fz_stext_line *line = block->u.t.first_line; line && !found;
                 line                = line->next)
            {
                for (fz_stext_char *span = line->first_char; span;
                     span                = span->next)
                {
                    if (span->size > 0)
                    {
                        result = span->origin;
                        found  = true;
                        break;
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

    return result;
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

    fz_matrix transform = fz_identity;

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

        transform = buildRenderTransform(bounds, job.zoom, job.rotation,
                                         job.flip_h, job.flip_v);

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

int
Model::get_obj_num_at_rect(int pageno, fz_rect targetRect) noexcept
{
    pdf_page *page   = nullptr;
    pdf_annot *annot = nullptr;
    int foundObjNum  = -1;

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

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
    }
    fz_always(m_ctx)
    {
        // No cleanup needed here since we drop the page inside the loop
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "get_obj_num_at_rect failed:" << fz_caught_message(m_ctx);
        return -1;
    }

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

QString
Model::fileTypeToString() const noexcept
{
    switch (m_filetype)
    {
        case FileType::PDF:
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

std::string
Model::getTextInPage(const int pageno, bool formatted) noexcept
{
    std::string result;

    fz_stext_page *stext_page = nullptr;
    fz_page *page             = nullptr;

    fz_try(m_ctx)
    {
        page       = fz_load_page(m_ctx, m_doc, pageno);
        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

        char *page_text
            = fz_copy_selection(m_ctx, stext_page, {0, 0}, {1e6f, 1e6f}, 0);
        if (page_text)
        {
            result = page_text;
            fz_free(m_ctx, page_text);
        }
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
        fz_drop_stext_page(m_ctx, stext_page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getTextInPage failed:" << fz_caught_message(m_ctx);
    }

    if (formatted)
        clean_pdf_text(result);

    return result;
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
