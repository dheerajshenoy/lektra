#include "BrowseLinkItem.hpp"
#include "Commands/TextHighlightAnnotationCommand.hpp"
#include "Config.hpp"
#include "Model.hpp"
#include "utils.hpp"

#include <QBuffer>
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
#include <cmath>
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

// --- Outline file I/O ---

// `doc` resolves each node's chapter-aware fz_location to a single global
// page index for the JSON output — reading node->page.page directly would
// give the wrong (chapter-local) number for any chaptered format (EPUB).
// `doc` may be null (DjVu, which has no fz_document / chapter concept at
// all); in that case node->page.page is already the correct global index,
// so resolution is skipped rather than risking a null-doc MuPDF call.
static QJsonArray
outline_to_json(fz_context *ctx, fz_document *doc, fz_outline *node) noexcept
{
    QJsonArray arr;
    for (; node; node = node->next)
    {
        // EPUB's outline loader leaves node->page as the sentinel {-1,-1}
        // (and x/y unset), expecting the destination to be resolved from
        // node->uri via fz_resolve_link() — mirrors
        // Model::resolveOutlineNode().
        fz_location loc = node->page;
        float x = node->x, y = node->y;
        if (loc.chapter < 0 && doc && node->uri)
            loc = fz_resolve_link(ctx, doc, node->uri, &x, &y);
        const int pageno
            = doc ? fz_page_number_from_location(ctx, doc, loc) : loc.page;
        QJsonObject obj;
        obj["title"] = node->title ? QString::fromUtf8(node->title) : QString();
        obj["page"]  = pageno + 1; // store as 1-based
        obj["x"]     = (double)x;
        obj["y"]     = (double)y;
        obj["children"] = outline_to_json(ctx, doc, node->down);
        arr.append(obj);
    }
    return arr;
}

// `doc` converts each entry's stored GLOBAL page index back into the
// correct chapter-aware fz_location, so loaded entries behave identically
// to real embedded-outline entries (both resolve correctly via
// fz_page_number_from_location() at every consumption site).
static fz_outline *
json_to_outline(fz_context *ctx, fz_document *doc,
                const QJsonArray &arr) noexcept
{
    fz_outline *root  = nullptr;
    fz_outline **tail = &root;
    for (const QJsonValue &val : arr)
    {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        fz_outline *node      = fz_new_outline(ctx);
        const QString title   = obj["title"].toString();
        node->title           = fz_strdup(ctx, title.toUtf8().constData());
        const int storedPage  = obj["page"].toInt(1) - 1;
        node->page    = doc ? fz_location_from_page_number(ctx, doc, storedPage)
                            : fz_make_location(0, storedPage);
        node->x       = (float)obj["x"].toDouble();
        node->y       = (float)obj["y"].toDouble();
        node->is_open = 1;
        if (obj.contains("children") && obj["children"].isArray())
            node->down = json_to_outline(ctx, doc, obj["children"].toArray());
        *tail = node;
        tail  = &node->next;
    }
    return root;
}

std::vector<Model::PageLink>
Model::pageLinks(int pageno) noexcept
{
    std::vector<PageLink> out;
    if (!supports_links() || pageno < 0 || pageno >= m_page_count)
        return out;

    ensurePageCached(pageno);

    std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
    const PageCacheEntry *entry = m_page_lru_cache.find(pageno);
    if (!entry)
        return out;

    if (!entry->links)
        return out;
    const auto &entryLinks = *entry->links;
    out.reserve(entryLinks.size());
    for (const CachedLink &l : entryLinks)
    {
        PageLink p;
        p.rect             = QRectF(l.rect.x0, l.rect.y0, l.rect.x1 - l.rect.x0,
                                    l.rect.y1 - l.rect.y0);
        p.info.uri         = l.uri;
        p.info.dest        = fz_make_link_dest_none();
        p.info.type        = l.type;
        p.info.target_page = l.target_page;
        p.info.target_loc  = {l.target_loc.x, l.target_loc.y, l.zoom};
        p.info.source_loc  = {l.source_loc.x, l.source_loc.y, 0.0f};
        p.info.source_page = pageno;
        out.push_back(std::move(p));
    }
    return out;
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

fz_outline *
Model::generateOutline(float min_ratio, int max_levels) noexcept
{
    if (!m_doc || !m_ctx || m_page_count == 0)
        return nullptr;

    // Phase 1: collect font size frequencies across all pages (bucketed to
    // 0.1pt)
    std::map<int, int> size_freq; // key = round(size * 10)
    for (int pageno = 0; pageno < m_page_count; ++pageno)
    {
        fz_stext_page *stext = get_or_build_stext_page(m_ctx, pageno);
        if (!stext)
            continue;
        for (fz_stext_block *block = stext->first_block; block;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;
            for (fz_stext_line *line = block->u.t.first_line; line;
                 line                = line->next)
            {
                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
                {
                    if (ch->size > 0)
                        size_freq[(int)std::round(ch->size * 10)]++;
                }
            }
        }
    }

    if (size_freq.empty())
        return nullptr;

    // Body size = most frequently used size
    int body_key = 0;
    int max_freq = 0;
    for (const auto &[key, freq] : size_freq)
    {
        if (freq > max_freq)
        {
            max_freq = freq;
            body_key = key;
        }
    }
    const float body_size = body_key / 10.0f;
    const float threshold = body_size * min_ratio;

    // Collect distinct heading sizes >= threshold, sorted descending, capped at
    // max_levels
    std::vector<int> heading_keys;
    for (const auto &[key, freq] : size_freq)
    {
        if (key / 10.0f >= threshold)
            heading_keys.push_back(key);
    }
    std::sort(heading_keys.begin(), heading_keys.end(), std::greater<int>());
    if ((int)heading_keys.size() > max_levels)
        heading_keys.resize(max_levels);

    if (heading_keys.empty())
        return nullptr;

    std::unordered_map<int, int> key_to_level;
    for (int i = 0; i < (int)heading_keys.size(); ++i)
        key_to_level[heading_keys[i]] = i;

    // Phase 2: build flat outline list from matching lines
    fz_outline *root  = nullptr;
    fz_outline **tail = &root;

    for (int pageno = 0; pageno < m_page_count; ++pageno)
    {
        fz_stext_page *stext = get_or_build_stext_page(m_ctx, pageno);
        if (!stext)
            continue;
        for (fz_stext_block *block = stext->first_block; block;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;
            for (fz_stext_line *line = block->u.t.first_line; line;
                 line                = line->next)
            {
                if (!line->first_char)
                    continue;

                // Use the maximum char size in the line as its representative
                // size
                float line_size = 0;
                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
                    line_size = std::max(line_size, ch->size);

                const int line_key = (int)std::round(line_size * 10);
                if (key_to_level.find(line_key) == key_to_level.end())
                    continue;

                // Collect UTF-8 text from the line
                std::string title;
                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
                {
                    char buf[8];
                    int n = fz_runetochar(buf, ch->c);
                    title.append(buf, n);
                }

                // Trim leading/trailing whitespace
                const auto not_space = [](unsigned char c)
                {
                    return !std::isspace(c);
                };
                title.erase(
                    title.begin(),
                    std::find_if(title.begin(), title.end(), not_space));
                title.erase(
                    std::find_if(title.rbegin(), title.rend(), not_space)
                        .base(),
                    title.end());

                if (title.empty())
                    continue;

                fz_outline *node = fz_new_outline(m_ctx);
                node->title      = fz_strdup(m_ctx, title.c_str());
                // Use the real chapter-aware location, not fz_make_location(0,
                // pageno) — for chaptered formats (EPUB, and anything else
                // MuPDF splits into multiple fz_document chapters) `pageno`
                // here is a GLOBAL page index, but chapter 0 alone may not
                // contain that many pages; fz_location_from_page_number()
                // resolves it to the correct {chapter, local page} pair so
                // every consumer can uniformly call
                // fz_page_number_from_location() to get the global index back.
                node->page = fz_location_from_page_number(m_ctx, m_doc, pageno);
                node->x    = line->first_char->origin.x;
                node->y    = line->first_char->origin.y;
                node->is_open = 1;

                *tail = node;
                tail  = &node->next;
            }
        }
    }

    fz_drop_outline(m_ctx, m_generated_outline);
    m_generated_outline = root;
    invalidateOutlineEntries();
    return m_generated_outline;
}

void
Model::harvestOutline(fz_outline *node, int depth,
                      std::vector<OutlineEntry> &out) noexcept
{
    for (fz_outline *n = node; n; n = n->next)
    {
        const QString title
            = QString::fromUtf8(n->title ? n->title : "<no title>")
                  .remove(QChar::Null)
                  .remove(QChar::ParagraphSeparator)
                  .remove(QChar::LineSeparator)
                  .remove(QChar(0xFFFD))
                  .trimmed();

        // n->page.page alone is only the LOCAL page-within-chapter number
        // for chaptered formats (EPUB) — resolve it to the document-wide
        // page. EPUB nodes also leave n->page/x/y unresolved and only carry
        // a uri, which resolveOutlineNode() handles.
        float x = n->x, y = n->y;
        const int pageno = resolveOutlineNode(n, &x, &y);

        out.push_back({.title     = title,
                       .depth     = depth,
                       .page      = pageno,
                       .location  = QPointF(x, y),
                       .isHeading = (n->down != nullptr)});
        if (n->down)
            harvestOutline(n->down, depth + 1, out);
    }
}

std::vector<Model::OutlineEntry>
Model::buildOutlineEntries(fz_outline *outline) noexcept
{
    std::vector<OutlineEntry> out;
    if (outline)
        harvestOutline(outline, 0, out);
    return out;
}

void
Model::prefetchOutlineAsync() noexcept
{
    if (!m_doc || m_outline_entries_valid || m_outline_future.isRunning())
        return;

    const int generation = m_outline_generation;
    m_outline_future     = QtConcurrent::run([this, generation]()
    {
        fz_context *ctx = cloneContext();
        if (!ctx)
            return;

        fz_outline *loaded = nullptr;
        std::vector<OutlineEntry> entries;

        {
            // The document is shared with the renderer; take the same lock.
            std::lock_guard<std::mutex> lock(m_doc_mutex);
            fz_try(ctx)
            {
                loaded = fz_load_outline(ctx, m_doc);

                std::function<void(fz_outline *, int)> walk
                    = [&](fz_outline *node, int depth)
                {
                    for (fz_outline *n = node; n; n = n->next)
                    {
                        const QString title
                            = QString::fromUtf8(n->title ? n->title
                                                         : "<no title>")
                                  .remove(QChar::Null)
                                  .remove(QChar::ParagraphSeparator)
                                  .remove(QChar::LineSeparator)
                                  .remove(QChar(0xFFFD))
                                  .trimmed();
                        // Same as resolveOutlineNode(), on this thread's
                        // context.
                        fz_location loc = n->page;
                        float x = n->x, y = n->y;
                        if (loc.chapter < 0 && n->uri)
                            loc = fz_resolve_link(ctx, m_doc, n->uri, &x, &y);
                        entries.push_back({.title = title,
                                           .depth = depth,
                                           .page = fz_page_number_from_location(
                                               ctx, m_doc, loc),
                                           .location  = QPointF(x, y),
                                           .isHeading = (n->down != nullptr)});
                        if (n->down)
                            walk(n->down, depth + 1);
                    }
                };
                walk(loaded, 0);
            }
            fz_catch(ctx)
            {
                entries.clear();
            }
        }

        // Hand the result to the GUI thread (the outline itself is dropped
        // with the main context, which shares its allocator with this one).
        QMetaObject::invokeMethod(
            this,
            [this, loaded, generation, entries = std::move(entries)]() mutable
        {
            if (!m_ctx)
                return;
            if (loaded && !m_outline)
                m_outline = loaded;
            else if (loaded)
                fz_drop_outline(m_ctx, loaded);

            if (generation == m_outline_generation && m_outline
                && !m_outline_entries_valid)
            {
                m_outline_entries       = std::move(entries);
                m_outline_entries_src   = m_outline;
                m_outline_entries_valid = true;
            }
        }, Qt::QueuedConnection);

        fz_drop_context(ctx);
    });
}

const std::vector<Model::OutlineEntry> &
Model::outlineEntries() noexcept
{
    fz_outline *src = getOutline();
    if (!src)
        src = m_generated_outline;
    if (!m_outline_entries_valid || src != m_outline_entries_src)
    {
        m_outline_entries       = buildOutlineEntries(src);
        m_outline_entries_src   = src;
        m_outline_entries_valid = true;
    }
    return m_outline_entries;
}

bool
Model::exportOutlineToFile(const QString &path, fz_outline *outline) noexcept
{
    // m_doc is null for DjVu (it uses the separate DjVuLib API, not MuPDF's
    // fz_document) — outline_to_json() handles a null doc by treating
    // node->page.page as already-global (correct for DjVu, which has no
    // chapter concept), so don't require m_doc here.
    if (!outline || !m_ctx)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QJsonDocument doc(outline_to_json(m_ctx, m_doc, outline));
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

fz_outline *
Model::loadOutlineFromFile(const QString &path) noexcept
{
    if (!m_ctx)
        return nullptr;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return nullptr;
    fz_outline *root = json_to_outline(m_ctx, m_doc, doc.array());
    fz_drop_outline(m_ctx, m_generated_outline);
    m_generated_outline = root;
    return m_generated_outline;
}

void
Model::highlight_text_selection(int pageno, QPointF start, QPointF end,
                                const QString &comment,
                                TextMarkup kind) noexcept
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
        this, pageno, std::move(quads), comment, nullptr, QColor(), kind));
}

void
Model::writeUnderlineAppearance(pdf_annot *annot) noexcept
{
    using US        = Config::Annotations::Underline::Style;
    const auto &cfg = m_config.annotations.underline;

    fz_buffer *buf = nullptr;
    pdf_obj *res   = nullptr;
    fz_var(buf);
    fz_var(res);

    fz_try(m_ctx)
    {
        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        pdf_obj *qp  = pdf_dict_get(m_ctx, obj, PDF_NAME(QuadPoints));
        const int n  = pdf_array_len(m_ctx, qp);
        if (n < 8)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "underline without quad points");

        const float thickness = std::clamp(cfg.thickness, 0.005f, 0.5f);
        const float offset    = std::clamp(cfg.offset, 0.0f, 1.0f);

        buf = fz_new_buffer(m_ctx, 256);

        // Opacity, as MuPDF's own appearances do it.
        const float opacity = pdf_annot_opacity(m_ctx, annot);
        if (opacity < 1.0f)
        {
            res = pdf_new_dict(m_ctx, m_pdf_doc, 1);
            pdf_obj *egs
                = pdf_dict_put_dict(m_ctx, res, PDF_NAME(ExtGState), 1);
            pdf_obj *gs = pdf_dict_put_dict(m_ctx, egs, PDF_NAME(H), 2);
            pdf_dict_put(m_ctx, gs, PDF_NAME(Type), PDF_NAME(ExtGState));
            pdf_dict_put_real(m_ctx, gs, PDF_NAME(CA), opacity);
            pdf_dict_put_real(m_ctx, gs, PDF_NAME(ca), opacity);
            fz_append_printf(m_ctx, buf, "/H gs\n");
        }

        int nc = 3;
        float color[4]{0, 0, 0, 0};
        pdf_annot_color(m_ctx, annot, &nc, color);
        switch (nc)
        {
            case 1:
                fz_append_printf(m_ctx, buf, "%g G\n", color[0]);
                break;
            case 4:
                fz_append_printf(m_ctx, buf, "%g %g %g %g K\n", color[0],
                                 color[1], color[2], color[3]);
                break;
            case 3:
                fz_append_printf(m_ctx, buf, "%g %g %g RG\n", color[0],
                                 color[1], color[2]);
                break;
            default:
                fz_append_printf(m_ctx, buf, "0 G\n");
                break;
        }

        fz_rect rect = fz_empty_rect;
        for (int i = 0; i + 8 <= n; i += 8)
        {
            // Cross-wise order, as MuPDF reads it: upper left, upper right,
            // lower left, lower right.
            fz_point ul{pdf_array_get_real(m_ctx, qp, i + 0),
                        pdf_array_get_real(m_ctx, qp, i + 1)};
            fz_point ur{pdf_array_get_real(m_ctx, qp, i + 2),
                        pdf_array_get_real(m_ctx, qp, i + 3)};
            fz_point ll{pdf_array_get_real(m_ctx, qp, i + 4),
                        pdf_array_get_real(m_ctx, qp, i + 5)};
            fz_point lr{pdf_array_get_real(m_ctx, qp, i + 6),
                        pdf_array_get_real(m_ctx, qp, i + 7)};

            const float h = std::hypot(ul.x - ll.x, ul.y - ll.y);
            const float w = h * thickness;
            auto lerp     = [](fz_point a, fz_point b, float t)
            {
                return fz_make_point(a.x + t * (b.x - a.x),
                                     a.y + t * (b.y - a.y));
            };
            const fz_point a = lerp(ll, ul, offset);
            const fz_point b = lerp(lr, ur, offset);

            fz_append_printf(m_ctx, buf, "%g w\n", w);
            if (cfg.style == US::Dashed)
                fz_append_printf(m_ctx, buf, "0 J [%g %g] 0 d\n", w * 4, w * 2);
            else if (cfg.style == US::Dotted)
                fz_append_printf(m_ctx, buf, "1 J [0 %g] 0 d\n", w * 2);
            else
                fz_append_printf(m_ctx, buf, "0 J [] 0 d\n");
            fz_append_printf(m_ctx, buf, "%g %g m\n%g %g l\nS\n", a.x, a.y, b.x,
                             b.y);

            const fz_point pts[4] = {ul, ur, ll, lr};
            for (const fz_point &p : pts)
                rect = fz_union_rect(
                    rect, fz_make_rect(p.x - w, p.y - w, p.x + w, p.y + w));
        }

        pdf_set_annot_appearance(m_ctx, annot, "N", nullptr, fz_identity, rect,
                                 res, buf);

        // The annotation's rectangle (in the page's own space, like the quad
        // points) must cover the line; pdf_set_annot_rect() refuses text
        // markup annotations, so it is written directly.
        pdf_begin_operation(m_ctx, m_pdf_doc, "Set underline rectangle");
        fz_try(m_ctx) pdf_dict_put_rect(m_ctx, obj, PDF_NAME(Rect), rect);
        fz_always(m_ctx) pdf_end_operation(m_ctx, m_pdf_doc);
        fz_catch(m_ctx) fz_rethrow(m_ctx);
        // Changed by the calls above; clean so that MuPDF does not take it for
        // an edit that needs a new (default) appearance.
        pdf_clean_obj(m_ctx, obj);
    }
    fz_always(m_ctx)
    {
        fz_drop_buffer(m_ctx, buf);
        pdf_drop_obj(m_ctx, res);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Could not write the underline appearance:"
                   << fz_caught_message(m_ctx);
    }
}

int
Model::addUnderlineAnnotation(const int pageno,
                              const std::vector<fz_quad> &quads,
                              const QColor &color,
                              const QString &content) noexcept
{
    int objNum{-1};
    if (quads.empty())
        return objNum;

    pdf_annot *annot = nullptr;
    pdf_page *page   = nullptr;

    fz_try(m_ctx)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_UNDERLINE);
        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_set_annot_quad_points(m_ctx, annot, quads.size(), &quads[0]);

        const QColor c
            = color.isValid()
                  ? color
                  : (m_underline_color.isValid()
                         ? m_underline_color
                         : rgbaToQColor(m_config.annotations.underline.color));
        const float mucolor[3]
            = {static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
               static_cast<float>(c.blueF())};
        pdf_set_annot_color(m_ctx, annot, 3, mucolor);
        pdf_set_annot_opacity(m_ctx, annot, static_cast<float>(c.alphaF()));
        pdf_set_annot_contents(m_ctx, annot, content.toUtf8().constData());

        // MuPDF's own (fixed) underline first, then ours over it.
        pdf_update_annot(m_ctx, annot);
        writeUnderlineAppearance(annot);
        pdf_update_page(m_ctx, page);

        if (pdf_obj *obj = pdf_annot_obj(m_ctx, annot))
            objNum = pdf_to_num(m_ctx, obj);
    }
    fz_always(m_ctx)
    {
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Adding the underline failed:"
                   << fz_caught_message(m_ctx);
        return objNum;
    }

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
    return objNum;
}

int
Model::addMarkupAnnotation(const int pageno, const enum pdf_annot_type type,
                           const std::vector<fz_quad> &quads,
                           const QColor &color, const QString &content) noexcept
{
    int objNum{-1};
    if (quads.empty())
        return objNum;

    pdf_annot *annot = nullptr;
    pdf_page *page   = nullptr;

    // Held across the try, so that an error thrown inside cannot skip the
    // release.
    std::unique_lock<std::mutex> lock(m_doc_mutex);

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        annot = pdf_create_annot(m_ctx, page, type);
        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_set_annot_quad_points(m_ctx, annot, quads.size(), &quads[0]);

        const QColor &preset
            = type == PDF_ANNOT_SQUIGGLY ? m_squiggly_color : m_strikeout_color;
        const QColor c = color.isValid()
                             ? color
                             : (preset.isValid() ? preset : QColor(Qt::red));
        const float mucolor[3]
            = {static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
               static_cast<float>(c.blueF())};
        pdf_set_annot_color(m_ctx, annot, 3, mucolor);
        pdf_set_annot_opacity(m_ctx, annot, static_cast<float>(c.alphaF()));
        pdf_set_annot_contents(m_ctx, annot, content.toUtf8().constData());

        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        if (pdf_obj *obj = pdf_annot_obj(m_ctx, annot))
            objNum = pdf_to_num(m_ctx, obj);
    }
    fz_always(m_ctx)
    {
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Adding the markup failed:" << fz_caught_message(m_ctx);
        return objNum;
    }
    lock.unlock();

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
    return objNum;
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
        // Outline only, so the content underneath stays readable.
        pdf_set_annot_color(m_ctx, annot, 3, m_annot_rect_color);
        pdf_set_annot_border_width(m_ctx, annot, 2.0f);
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
Model::addRedactAnnotation(const int pageno, const fz_rect &rect) noexcept
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

        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_REDACT);

        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_set_annot_rect(m_ctx, annot, rect);
        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (!obj)
            fz_throw(m_ctx, FZ_ERROR_GENERIC,
                     "Failed to get annotation object");

        objNum = pdf_to_num(m_ctx, obj);
    }
    fz_always(m_ctx)
    {
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Adding the redaction mark failed:"
                   << fz_caught_message(m_ctx);
        return objNum;
    }

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }

    return objNum;
}

int
Model::redactionCount() noexcept
{
    if (!m_pdf_doc)
        return 0;

    int count = 0;
    std::lock_guard<std::mutex> lock(m_doc_mutex);
    for (int i = 0; i < m_page_count; ++i)
    {
        pdf_page *page = nullptr;
        fz_var(page);
        fz_try(m_ctx)
        {
            page = pdf_load_page(m_ctx, m_pdf_doc, i);
            for (pdf_annot *a = pdf_first_annot(m_ctx, page); a;
                 a            = pdf_next_annot(m_ctx, a))
                if (pdf_annot_type(m_ctx, a) == PDF_ANNOT_REDACT)
                    ++count;
        }
        fz_always(m_ctx)
            pdf_drop_page(m_ctx, page);
        fz_catch(m_ctx)
            qWarning() << "Counting redactions failed on page" << i << ":"
                       << fz_caught_message(m_ctx);
    }
    return count;
}

int
Model::applyRedactions() noexcept
{
    if (!m_pdf_doc)
        return -1;

    int applied = 0;
    bool failed = false;
    std::vector<int> changed;

    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        for (int i = 0; i < m_page_count; ++i)
        {
            pdf_page *page = nullptr;
            int marks      = 0;
            fz_var(page);
            fz_var(marks);
            fz_try(m_ctx)
            {
                page = pdf_load_page(m_ctx, m_pdf_doc, i);
                for (pdf_annot *a = pdf_first_annot(m_ctx, page); a;
                     a            = pdf_next_annot(m_ctx, a))
                    if (pdf_annot_type(m_ctx, a) == PDF_ANNOT_REDACT)
                        ++marks;

                if (marks > 0)
                {
                    // Black boxes where the content was; text, and the
                    // pixels of images, under a mark are removed.
                    pdf_redact_options opts = {
                        1, PDF_REDACT_IMAGE_PIXELS,
                        PDF_REDACT_LINE_ART_REMOVE_IF_TOUCHED,
                        PDF_REDACT_TEXT_REMOVE};
                    pdf_redact_page(m_ctx, m_pdf_doc, page, &opts);
                    pdf_update_page(m_ctx, page);
                }
            }
            fz_always(m_ctx)
                pdf_drop_page(m_ctx, page);
            fz_catch(m_ctx)
            {
                qWarning() << "Redacting page" << i
                           << "failed:" << fz_caught_message(m_ctx);
                failed = true;
                marks  = 0;
            }

            if (marks > 0)
            {
                applied += marks;
                changed.push_back(i);
            }
        }
    }

    if (applied > 0)
        m_redacted = true;

    for (int i : changed)
    {
        invalidatePageCache(i);
        emit reloadRequested(i);
    }

    return failed ? -1 : applied;
}

int
Model::addShapeAnnotation(const int pageno, const enum pdf_annot_type type,
                          const fz_rect &rect,
                          const std::vector<fz_point> &vertices,
                          const QString &content) noexcept
{
    int objNum       = -1;
    pdf_annot *annot = nullptr;
    pdf_page *page   = nullptr;

    // Held across the try, so that an error thrown inside cannot skip the
    // release.
    std::unique_lock<std::mutex> lock(m_doc_mutex);

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        annot = pdf_create_annot(m_ctx, page, type);

        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        switch (type)
        {
            case PDF_ANNOT_CIRCLE:
                pdf_set_annot_rect(m_ctx, annot, rect);
                pdf_set_annot_color(m_ctx, annot, 3, m_annot_rect_color);
                pdf_set_annot_border_width(m_ctx, annot, 2.0f);
                pdf_set_annot_opacity(m_ctx, annot, m_annot_rect_color[3]);
                break;

            case PDF_ANNOT_POLYGON:
                for (const fz_point &p : vertices)
                    pdf_add_annot_vertex(m_ctx, annot, p);
                pdf_set_annot_color(m_ctx, annot, 3, m_annot_rect_color);
                pdf_set_annot_border_width(m_ctx, annot, 2.0f);
                pdf_set_annot_opacity(m_ctx, annot, m_annot_rect_color[3]);
                break;

            case PDF_ANNOT_FREE_TEXT:
            {
                // An inline note: black text on a pale yellow background.
                const float text_color[3]       = {0.0f, 0.0f, 0.0f};
                const float background_color[3] = {1.0f, 0.97f, 0.7f};
                pdf_set_annot_rect(m_ctx, annot, rect);
                pdf_set_annot_default_appearance(m_ctx, annot, "Helv", 12.0f, 3,
                                                 text_color);
                pdf_set_annot_color(m_ctx, annot, 3, background_color);
                pdf_set_annot_border_width(m_ctx, annot, 0.0f);
                break;
            }

            default:
                fz_throw(m_ctx, FZ_ERROR_GENERIC, "Unsupported shape");
        }

        if (!content.isEmpty())
            pdf_set_annot_contents(m_ctx, annot, content.toUtf8().constData());

        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (!obj)
            fz_throw(m_ctx, FZ_ERROR_GENERIC,
                     "Failed to get annotation object");

        objNum = pdf_to_num(m_ctx, obj);
    }
    fz_always(m_ctx)
    {
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Adding the annotation failed:"
                   << fz_caught_message(m_ctx);
        return objNum;
    }
    lock.unlock();

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
    return objNum;
}

// ---------------------------------------------------------------------------
// Image annotations: a stamp whose appearance is the image, turned by the
// matrix of the appearance form and marked with /LektraImage. MuPDF draws
// its own appearance for a stamp whenever it is changed, so every change of
// one goes through writeImageAppearance().
// ---------------------------------------------------------------------------
bool
Model::isImageAnnot(fz_context *ctx, pdf_annot *annot)
{
    return pdf_annot_type(ctx, annot) == PDF_ANNOT_STAMP
           && pdf_dict_gets(ctx, pdf_annot_obj(ctx, annot), "LektraImage");
}

// The image of the appearance (not kept).
pdf_obj *
Model::imageOf(fz_context *ctx, pdf_annot *annot)
{
    pdf_obj *xobjects = pdf_dict_getp(ctx, pdf_annot_obj(ctx, annot),
                                      "AP/N/Resources/XObject");
    return pdf_dict_len(ctx, xobjects) > 0 ? pdf_dict_get_val(ctx, xobjects, 0)
                                           : nullptr;
}

// Where an image annotation is (see Model::AnnotGeometry).
bool
Model::readImageFrame(fz_context *ctx, pdf_annot *annot, AnnotGeometry &out)
{
    pdf_obj *form = pdf_dict_getp(ctx, pdf_annot_obj(ctx, annot), "AP/N");
    pdf_obj *bbox = pdf_dict_get(ctx, form, PDF_NAME(BBox));
    pdf_obj *mat  = pdf_dict_get(ctx, form, PDF_NAME(Matrix));
    if (!bbox || !mat)
        return false;

    const fz_rect box = pdf_to_rect(ctx, bbox);
    const fz_matrix m = pdf_to_matrix(ctx, mat);
    const float w     = box.x1 - box.x0;
    const float h     = box.y1 - box.y0;
    const float phi   = std::atan2(m.b, m.a);

    // The sides, in the page's own space and then in the page space.
    fz_matrix ctm;
    pdf_page_transform(ctx, pdf_annot_page(ctx, annot), nullptr, &ctm);
    const fz_point right
        = fz_transform_vector({w * std::cos(phi), w * std::sin(phi)}, ctm);
    const fz_point up
        = fz_transform_vector({-h * std::sin(phi), h * std::cos(phi)}, ctm);

    out.rect = pdf_annot_rect(ctx, annot);
    out.center
        = {(out.rect.x0 + out.rect.x1) / 2, (out.rect.y0 + out.rect.y1) / 2};
    out.ux     = right;
    out.uy     = {-up.x, -up.y};
    out.framed = true;
    out.vertices.clear();
    return true;
}

// Draws `image` in the frame.
void
Model::writeImageAppearance(fz_context *ctx, pdf_annot *annot,
                            const AnnotGeometry &g, pdf_obj *image)
{
    pdf_document *doc = pdf_annot_page(ctx, annot)->doc;

    fz_matrix ctm;
    pdf_page_transform(ctx, pdf_annot_page(ctx, annot), nullptr, &ctm);
    const fz_matrix inv = fz_invert_matrix(ctm);

    const fz_point c  = fz_transform_point(g.center, inv);
    const fz_point ux = fz_transform_vector(g.ux, inv);
    const fz_point uy = fz_transform_vector(g.uy, inv);
    const float w     = std::max(std::hypot(ux.x, ux.y), 1.0f);
    const float h     = std::max(std::hypot(uy.x, uy.y), 1.0f);
    const float deg   = std::atan2(ux.y, ux.x) * 180.0f / float(M_PI);

    const fz_matrix m    = fz_rotate(deg);
    const fz_rect box    = {0, 0, w, h};
    const fz_rect turned = fz_transform_rect(box, m);
    const float bw       = turned.x1 - turned.x0;
    const float bh       = turned.y1 - turned.y0;
    const fz_rect rect   = fz_transform_rect(
        {c.x - bw / 2, c.y - bh / 2, c.x + bw / 2, c.y + bh / 2}, ctm);

    pdf_obj *res        = nullptr;
    pdf_obj *xobjects   = nullptr;
    fz_buffer *contents = nullptr;
    fz_var(res);
    fz_var(xobjects);
    fz_var(contents);
    fz_try(ctx)
    {
        res      = pdf_new_dict(ctx, doc, 1);
        xobjects = pdf_new_dict(ctx, doc, 1);
        pdf_dict_puts(ctx, xobjects, "Img", image);
        pdf_dict_put(ctx, res, PDF_NAME(XObject), xobjects);

        contents = fz_new_buffer(ctx, 64);
        fz_append_printf(ctx, contents, "q %g 0 0 %g 0 0 cm /Img Do Q", w, h);

        // The rectangle first: setting it makes MuPDF draw the annotation
        // again, setting the appearance says it is drawn.
        pdf_set_annot_rect(ctx, annot, rect);
        pdf_set_annot_appearance(ctx, annot, "N", nullptr, m, box, res,
                                 contents);
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, contents);
        pdf_drop_obj(ctx, xobjects);
        pdf_drop_obj(ctx, res);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}

// Sets the text of an annotation without MuPDF drawing an image over again.
void
Model::setAnnotContentsKeepImage(fz_context *ctx, pdf_annot *annot,
                                 const char *text)
{
    if (!isImageAnnot(ctx, annot))
    {
        pdf_set_annot_contents(ctx, annot, text);
        return;
    }

    AnnotGeometry g;
    pdf_obj *image = pdf_keep_obj(ctx, imageOf(ctx, annot));
    fz_try(ctx)
    {
        const bool framed = readImageFrame(ctx, annot, g);
        pdf_set_annot_contents(ctx, annot, text);
        if (framed && image)
            writeImageAppearance(ctx, annot, g, image);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, image);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}

int
Model::addImageAnnotation(const int pageno, const QByteArray &data,
                          const AnnotGeometry &frame,
                          const QString &content) noexcept
{
    int objNum       = -1;
    pdf_annot *annot = nullptr;
    pdf_page *page   = nullptr;
    fz_buffer *buf   = nullptr;
    fz_image *image  = nullptr;
    pdf_obj *imgobj  = nullptr;

    std::unique_lock<std::mutex> lock(m_doc_mutex);

    fz_var(buf);
    fz_var(image);
    fz_var(imgobj);
    fz_var(annot);
    fz_var(page);
    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        buf = fz_new_buffer_from_copied_data(
            m_ctx, reinterpret_cast<const unsigned char *>(data.constData()),
            size_t(data.size()));
        image  = fz_new_image_from_buffer(m_ctx, buf);
        imgobj = pdf_add_image(m_ctx, m_pdf_doc, image);

        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_STAMP);
        if (!annot)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to create annotation");

        pdf_dict_puts_drop(m_ctx, pdf_annot_obj(m_ctx, annot), "LektraImage",
                           PDF_TRUE);
        if (!content.isEmpty())
            pdf_set_annot_contents(m_ctx, annot, content.toUtf8().constData());

        writeImageAppearance(m_ctx, annot, frame, imgobj);
        pdf_update_page(m_ctx, page);

        objNum = pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot));
    }
    fz_always(m_ctx)
    {
        pdf_drop_obj(m_ctx, imgobj);
        fz_drop_image(m_ctx, image);
        fz_drop_buffer(m_ctx, buf);
        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Adding the image failed:" << fz_caught_message(m_ctx);
        return -1;
    }
    lock.unlock();

    if (objNum >= 0)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
    return objNum;
}

QByteArray
Model::annotImageData(int pageno, int objNum) noexcept
{
    QByteArray png;
    objNum = resolveAnnot(objNum);

    pdf_page *page = nullptr;
    std::unique_lock<std::mutex> lock(m_doc_mutex);

    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != objNum
                || !isImageAnnot(m_ctx, annot))
                continue;

            pdf_obj *imgobj = imageOf(m_ctx, annot);
            if (!imgobj)
                break;

            fz_image *image  = nullptr;
            fz_pixmap *color = nullptr;
            fz_pixmap *rgb   = nullptr;
            fz_pixmap *mask  = nullptr;
            fz_var(image);
            fz_var(color);
            fz_var(rgb);
            fz_var(mask);
            fz_try(m_ctx)
            {
                image = pdf_load_image(m_ctx, m_pdf_doc, imgobj);
                color = fz_get_pixmap_from_image(m_ctx, image, nullptr, nullptr,
                                                 nullptr, nullptr);
                rgb   = fz_convert_pixmap(m_ctx, color, fz_device_rgb(m_ctx),
                                          nullptr, nullptr,
                                          fz_default_color_params, 0);

                QImage img(rgb->w, rgb->h, QImage::Format_RGBA8888);
                for (int y = 0; y < rgb->h; ++y)
                {
                    const unsigned char *src
                        = rgb->samples + size_t(y) * rgb->stride;
                    unsigned char *dst = img.scanLine(y);
                    for (int x = 0; x < rgb->w; ++x)
                    {
                        dst[4 * x]     = src[rgb->n * x];
                        dst[4 * x + 1] = src[rgb->n * x + 1];
                        dst[4 * x + 2] = src[rgb->n * x + 2];
                        dst[4 * x + 3] = 255;
                    }
                }

                // The transparency of the image is an image of its own.
                if (image->mask)
                {
                    mask = fz_get_pixmap_from_image(m_ctx, image->mask, nullptr,
                                                    nullptr, nullptr, nullptr);
                    for (int y = 0; y < img.height(); ++y)
                    {
                        const int my
                            = std::min(mask->h - 1, int(int64_t(y) * mask->h
                                                        / img.height()));
                        const unsigned char *src
                            = mask->samples + size_t(my) * mask->stride;
                        unsigned char *dst = img.scanLine(y);
                        for (int x = 0; x < img.width(); ++x)
                        {
                            const int mx
                                = std::min(mask->w - 1, int(int64_t(x) * mask->w
                                                            / img.width()));
                            dst[4 * x + 3] = src[mask->n * mx];
                        }
                    }
                }

                QBuffer out(&png);
                out.open(QIODevice::WriteOnly);
                img.save(&out, "PNG");
            }
            fz_always(m_ctx)
            {
                fz_drop_pixmap(m_ctx, mask);
                fz_drop_pixmap(m_ctx, rgb);
                fz_drop_pixmap(m_ctx, color);
                fz_drop_image(m_ctx, image);
            }
            fz_catch(m_ctx)
            {
                fz_rethrow(m_ctx);
            }
            break;
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "annotImageData failed:" << fz_caught_message(m_ctx);
        return {};
    }
    return png;
}

void
Model::aliasAnnot(int from, int to) noexcept
{
    if (from != to)
        m_annot_alias[from] = to;
}

int
Model::resolveAnnot(int objNum) const noexcept
{
    // A chain at most as long as the table, in case of a loop.
    for (size_t i = 0; i <= m_annot_alias.size(); ++i)
    {
        const auto it = m_annot_alias.find(objNum);
        if (it == m_annot_alias.end())
            break;
        objNum = it->second;
    }
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
Model::getAnnotComment(const int pageno, int objNum) noexcept
{
    objNum = resolveAnnot(objNum);
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
Model::addAnnotComment(const int pageno, int objNum,
                       const QString &text) noexcept
{
    objNum         = resolveAnnot(objNum);
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
            setAnnotContentsKeepImage(m_ctx, annot, utf8.constData());
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
Model::removeAnnotComment(const int pageno, int objNum) noexcept
{
    objNum         = resolveAnnot(objNum);
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

            setAnnotContentsKeepImage(m_ctx, annot, "");
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
    std::vector<int> resolved;
    resolved.reserve(objNums.size());
    for (int n : objNums)
        resolved.push_back(resolveAnnot(n));
    if (resolved.empty())
        return;

    // Build fast lookup set
    std::unordered_set<int> to_delete;
    to_delete.reserve(resolved.size());
    for (int n : resolved)
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
                                       pdf_bound_annot(m_ctx, annot)});
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

bool
Model::annotGeometry(int pageno, int objNum, AnnotGeometry &out) noexcept
{
    objNum = resolveAnnot(objNum);
    if (!m_pdf_doc)
        return false;

    bool found     = false;
    pdf_page *page = nullptr;

    // Held across the try, so that an error thrown inside cannot skip the
    // release.
    std::unique_lock<std::mutex> lock(m_doc_mutex);

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

            out.vertices.clear();
            out.framed = false;
            if (isImageAnnot(m_ctx, annot))
            {
                found = readImageFrame(m_ctx, annot, out);
                break;
            }
            // a polygon has no rectangle of its own
            if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_POLYGON)
                out.rect = pdf_annot_rect(m_ctx, annot);
            else
            {
                out.rect = pdf_bound_annot(m_ctx, annot);
                for (int i = 0, c = pdf_annot_vertex_count(m_ctx, annot); i < c;
                     ++i)
                    out.vertices.push_back(pdf_annot_vertex(m_ctx, annot, i));
            }
            found = true;
            break;
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "annotGeometry failed:" << fz_caught_message(m_ctx);
        return false;
    }
    lock.unlock();
    return found;
}

void
Model::setAnnotGeometry(int pageno, int objNum,
                        const AnnotGeometry &geometry) noexcept
{
    objNum = resolveAnnot(objNum);
    if (!m_pdf_doc)
        return;

    bool changed   = false;
    pdf_page *page = nullptr;

    // Held across the try, so that an error thrown inside cannot skip the
    // release.
    std::unique_lock<std::mutex> lock(m_doc_mutex);

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

            if (geometry.framed && isImageAnnot(m_ctx, annot))
            {
                pdf_obj *image = pdf_keep_obj(m_ctx, imageOf(m_ctx, annot));
                fz_try(m_ctx)
                {
                    if (image)
                        writeImageAppearance(m_ctx, annot, geometry, image);
                }
                fz_always(m_ctx)
                {
                    pdf_drop_obj(m_ctx, image);
                }
                fz_catch(m_ctx)
                {
                    fz_rethrow(m_ctx);
                }
            }
            else if (pdf_annot_type(m_ctx, annot) == PDF_ANNOT_POLYGON)
            {
                pdf_clear_annot_vertices(m_ctx, annot);
                for (const fz_point &p : geometry.vertices)
                    pdf_add_annot_vertex(m_ctx, annot, p);
            }
            else
            {
                pdf_set_annot_rect(m_ctx, annot, geometry.rect);
            }
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
        qWarning() << "setAnnotGeometry failed:" << fz_caught_message(m_ctx);
        return;
    }
    lock.unlock();

    if (changed)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
}

void
Model::annotChangeColor(int pageno, int index, const QColor &color) noexcept
{
    index = resolveAnnot(index);
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

            if (isImageAnnot(m_ctx, annot))
                break;

            const float rgb[3] = {color.redF(), color.greenF(), color.blueF()};
            switch (pdf_annot_type(m_ctx, annot))
            {
                case PDF_ANNOT_SQUARE:
                case PDF_ANNOT_CIRCLE:
                case PDF_ANNOT_POLYGON:
                    // Older rects were filled; keep their fill in sync.
                    if (pdf_dict_get(m_ctx, pdf_annot_obj(m_ctx, annot),
                                     PDF_NAME(IC)))
                        pdf_set_annot_interior_color(m_ctx, annot, 3, rgb);
                    pdf_set_annot_color(m_ctx, annot, 3, rgb);
                    break;
                case PDF_ANNOT_FREE_TEXT: // the colour is the background
                case PDF_ANNOT_TEXT:
                case PDF_ANNOT_HIGHLIGHT:
                case PDF_ANNOT_UNDERLINE:
                case PDF_ANNOT_SQUIGGLY:
                case PDF_ANNOT_STRIKE_OUT:
                    pdf_set_annot_color(m_ctx, annot, 3, rgb);
                    break;
                default:
                    break;
            }
            pdf_set_annot_opacity(m_ctx, annot, color.alphaF());
            pdf_update_annot(m_ctx, annot);
            // The update drew MuPDF's own underline again.
            if (pdf_annot_type(m_ctx, annot) == PDF_ANNOT_UNDERLINE)
                writeUnderlineAppearance(annot);
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
Model::getAnnotColor(const int pageno, int objNum) noexcept
{
    objNum = resolveAnnot(objNum);
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
            float rgb[3]{0, 0, 0};
            switch (pdf_annot_type(m_ctx, annot))
            {
                case PDF_ANNOT_SQUARE:
                case PDF_ANNOT_CIRCLE:
                case PDF_ANNOT_POLYGON:
                case PDF_ANNOT_FREE_TEXT: // the colour is the background
                case PDF_ANNOT_TEXT:
                case PDF_ANNOT_HIGHLIGHT:
                case PDF_ANNOT_UNDERLINE:
                case PDF_ANNOT_SQUIGGLY:
                case PDF_ANNOT_STRIKE_OUT:
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

std::vector<Model::AnnotationInfo>
Model::annotationInfos(int pageno) noexcept
{
    std::vector<AnnotationInfo> result;
    if (!m_ctx || !m_doc || !m_pdf_doc || pageno < 0 || pageno >= m_page_count)
        return result;

    pdf_page *page = nullptr;

    fz_try(m_ctx)
    {
        fz_stext_page *stext_page = get_or_build_stext_page(m_ctx, pageno);

        page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            AnnotationInfo info;
            info.type = pdf_annot_type(m_ctx, annot);
            if (info.type == PDF_ANNOT_LINK || info.type == PDF_ANNOT_WIDGET
                || info.type == PDF_ANNOT_POPUP)
                continue;

            // one annotation that cannot be read must not hide the others
            bool readable = true;
            fz_try(m_ctx)
            {
                if (pdf_obj *obj = pdf_annot_obj(m_ctx, annot))
                    info.objNum = pdf_to_num(m_ctx, obj);

                // a highlight has quad points instead of a rectangle
                if (pdf_annot_has_quad_points(m_ctx, annot))
                {
                    const int count = pdf_annot_quad_point_count(m_ctx, annot);
                    info.rect       = fz_empty_rect;
                    for (int i = 0; i < count; ++i)
                    {
                        const fz_quad quad
                            = pdf_annot_quad_point(m_ctx, annot, i);
                        info.quads.push_back(quad);
                        info.rect
                            = fz_union_rect(info.rect, fz_rect_from_quad(quad));
                    }
                }
                else
                    info.rect = pdf_bound_annot(m_ctx, annot);

                if (const char *contents = pdf_annot_contents(m_ctx, annot))
                    info.contents = QString::fromUtf8(contents);

                int n        = 3;
                float rgb[3] = {0, 0, 0};
                pdf_annot_color(m_ctx, annot, &n, rgb);
                info.color.setRgbF(rgb[0], rgb[1], rgb[2],
                                   pdf_annot_opacity(m_ctx, annot));

                if (info.type == PDF_ANNOT_HIGHLIGHT && stext_page)
                    info.text = text_from_quads(stext_page, info.quads);
            }
            fz_catch(m_ctx)
            {
                qWarning() << "annotationInfos: skipped an annotation:"
                           << fz_caught_message(m_ctx);
                readable = false;
            }
            if (!readable)
                continue;

            result.push_back(std::move(info));
        }
    }
    fz_always(m_ctx)
    {
        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "annotationInfos failed:" << fz_caught_message(m_ctx);
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
    std::shared_ptr<const std::vector<CachedLink>> cachedLinks;
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        const PageCacheEntry *entry = m_page_lru_cache.get(job.pageno);
        if (entry)
            cachedLinks = entry->links;
    }

    static const std::vector<CachedLink> noCachedLinks;
    const std::vector<CachedLink> &cachedLinkList
        = cachedLinks ? *cachedLinks : noCachedLinks;

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
                    for (const auto &cl : cachedLinkList)
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
