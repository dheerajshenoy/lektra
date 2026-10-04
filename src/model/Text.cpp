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

std::vector<QPolygonF>
Model::selectAtHelper(int pageno, fz_point pt, int snapMode) noexcept
{
    std::vector<QPolygonF> out;
    // DjVu has no fz_context/fz_document (m_ctx/m_doc are null — it uses
    // the separate DjVuLib API instead), so fz_try(m_ctx) below would
    // dereference a null context. Bail out cleanly instead, matching
    // computeTextSelectionQuad()'s existing DjVu guard.
    if (m_filetype == FileType::DJVU)
        return out;

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
    // See selectAtHelper() above — DjVu has no fz_context/fz_document.
    if (m_filetype == FileType::DJVU)
        return out;

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
              bool use_regex, int pageTo) noexcept
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
        = QtConcurrent::run([this, pageFrom, pageTo, page_count = m_page_count,
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
        const int stop_at
            = (pageTo < 0) ? page_count : std::min(pageTo + 1, page_count);
        for (int batch_start = pageFrom;
             batch_start < stop_at && !m_search_cancelled.load();
             batch_start += BATCH)
        {
            const int batch_end = std::min(batch_start + BATCH, stop_at);

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

bool
Model::hasTextLayer() noexcept
{
    if (m_has_text_layer >= 0)
        return m_has_text_layer == 1;

    if (!supports_text_selection())
        return false;

    fz_context *ctx = cloneContext();
    if (!ctx)
        return false;

    bool found  = false;
    bool failed = false;
    fz_try(ctx)
    {
        for (int p = 0; p < m_page_count && !found; ++p)
        {
            const fz_stext_page *stext = get_or_build_stext_page(ctx, p);
            for (const fz_stext_block *b = stext->first_block; b && !found;
                 b = b->next)
            {
                if (b->type != FZ_STEXT_BLOCK_TEXT)
                    continue;
                for (const fz_stext_line *l = b->u.t.first_line; l && !found;
                     l = l->next)
                    for (const fz_stext_char *c = l->first_char; c; c = c->next)
                        if (c->c > ' ')
                        {
                            found = true;
                            break;
                        }
            }
        }
    }
    fz_catch(ctx)
    {
        failed = true;
    }

    fz_drop_context(ctx);
    if (!failed)
        m_has_text_layer = found ? 1 : 0;
    return found;
}

std::vector<Model::CachedTextChar>
Model::textCharsForPage(int pageno) noexcept
{
    buildTextCacheForPages({pageno});

    std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
    const CachedTextPage *cache = m_text_cache.find(pageno);
    return cache ? cache->chars : std::vector<CachedTextChar>{};
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
