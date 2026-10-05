#include "PageRange.hpp"

#include <QFileInfo>
#include <QImageReader>
#include <QPageSize>
#include <QPdfWriter>
#include <QPainter>
#include <QImageWriter>
#include "DocumentView.hpp"

#include <QMovie>

// Annotations
#include "Annotations/HighlightAnnotation.hpp"
#include "Annotations/PopupAnnotation.hpp"
#include "Annotations/RectAnnotation.hpp"
#include "BrowseLinkItem.hpp"

// Commands
#include "Commands/AnnotColorCommand.hpp"
#include "Commands/AnnotCommentCommand.hpp"
#include "Commands/DeleteAnnotationsCommand.hpp"
#include "Commands/RectAnnotationCommand.hpp"
#include "Commands/TextAnnotationCommand.hpp"

// Other
#include "Config.hpp"
#include "DispatchType.hpp"
#include "DocumentContainer.hpp"
#include "GraphicsImageItem.hpp"
#include "GraphicsView.hpp"
#include "InputDialog.hpp"
#include "Lektra.hpp"
#include "LinkHint.hpp"
#include "PropertiesWidget.hpp"
#include "WaitingSpinnerWidget.hpp"
#include "utils.hpp"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QTextCursor>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <qdebug.h>
#include <qguiapplication.h>
#include <qicon.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qpolygon.h>
#include <qstyle.h>

// Get the size of the current page in scene coordinates
QSizeF
DocumentView::pageSceneSize(int pageno) const noexcept
{
    const float scale = m_model->logicalScale();

    const auto pageDim = pageDimensionsPts(pageno);
    double w           = pageDim.width_pts * scale;
    double h           = pageDim.height_pts * scale;

    const int rot
        = static_cast<int>(std::fmod(std::abs(m_model->rotation()), 360.0));
    if (rot == 90 || rot == 270)
        std::swap(w, h);

    return QSizeF(w, h);
}

// Full page dims, or (m_trim_margins) the tight content-bbox dims — points,
// pre-scale/rotation. See header comment.
Model::PageDimension
DocumentView::pageDimensionsPts(int pageno) const noexcept
{
    if (!m_trim_margins)
        return m_model->page_dimension_pts(pageno);

    const auto bbox = m_model->contentBBox(pageno);
    if (bbox.isEmpty())
        return m_model->page_dimension_pts(pageno);

    return Model::PageDimension{bbox.width(), bbox.height()};
}

// Content-bbox rect mapped into the rendered image's own pixel space. See
// header comment.
QRect
DocumentView::contentCropRectPixels(int pageno) const noexcept
{
    const auto bbox = m_model->contentBBox(pageno);
    if (bbox.isEmpty())
        return {};

    const QPointF p0
        = m_model->toPixelSpace(pageno, fz_point{bbox.x0, bbox.y0});
    const QPointF p1
        = m_model->toPixelSpace(pageno, fz_point{bbox.x1, bbox.y1});

    // normalized() handles the corner swap rotation introduces (top-left
    // and bottom-right of the page-space rect don't necessarily map to
    // top-left/bottom-right in device space once rotated).
    return QRectF(p0, p1).normalized().toRect();
}

GraphicsImageItem *
DocumentView::pageItemAt(int pageno) const noexcept
{
    return m_page_items_hash.value(pageno, nullptr);
}

const std::set<int> &
DocumentView::getVisiblePages() noexcept
{
    if (!m_visible_pages_dirty)
        return m_visible_pages_cache;

    m_visible_pages_cache.clear();

    if (m_model->numPages() == 0
        || m_page_offsets.size() < static_cast<size_t>(m_model->numPages() + 1))
    {
        m_visible_pages_dirty = false;
        return m_visible_pages_cache;
    }

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        m_visible_pages_cache.insert(
            std::clamp(m_pageno, 0, m_model->numPages() - 1));
        m_visible_pages_dirty = false;
        return m_visible_pages_cache;
    }

    const QRectF visibleSceneRect
        = m_gview->mapToScene(m_gview->viewport()->rect()).boundingRect();

    double a0, a1;
    if (m_layout_mode == LayoutMode::HORIZONTAL)
    {
        a0 = visibleSceneRect.left();
        a1 = visibleSceneRect.right();
    }
    else
    {
        a0 = visibleSceneRect.top();
        a1 = visibleSceneRect.bottom();
    }

    const int N = m_model->numPages();

    if (m_layout_mode == LayoutMode::BOOK)
    {
        // Iterate by ROW to avoid problems with duplicate offsets in
        // spreads. Row 0 = page 0 (cover), Row 1 = pages 1-2, Row 2 = pages
        // 3-4, etc.
        for (int i = 0; i < N;)
        {
            const double rowStart = m_page_offsets[i];

            int rowEnd_idx;
            if (i == 0)
                rowEnd_idx = 1; // cover is alone
            else
                rowEnd_idx = std::min(i + 2, N); // spread pair

            const double rowEnd
                = (rowEnd_idx < N)
                      ? m_page_offsets[rowEnd_idx]
                      : m_page_offsets[N]; // use sentinel for last row

            // Overlap test: rowStart < a1 && rowEnd > a0
            if (rowStart < a1 && rowEnd > a0)
            {
                // Add all pages in this row
                for (int p = i; p < rowEnd_idx; ++p)
                    m_visible_pages_cache.insert(p);
            }

            // Early exit: if this row starts beyond viewport, no more
            // visible
            if (rowStart >= a1)
                break;

            // Advance to next row
            if (i == 0)
                i = 1;
            else
                i += 2;
        }
    }
    else
    {
        // (offsets are strictly increasing, binary search is safe)
        auto it_last = std::lower_bound(m_page_offsets.begin(),
                                        m_page_offsets.end(), a1);

        auto it_first = std::upper_bound(m_page_offsets.begin(),
                                         m_page_offsets.end(), a0);
        if (it_first != m_page_offsets.begin())
            --it_first;

        int firstPage = std::max(0, static_cast<int>(std::distance(
                                        m_page_offsets.begin(), it_first)));
        int lastPage  = std::max(
            0, static_cast<int>(std::distance(m_page_offsets.begin(), it_last))
                   - 1);

        firstPage = std::clamp(firstPage, 0, N - 1);
        lastPage  = std::clamp(lastPage, 0, N - 1);

        const double spacingScene = m_spacing * m_current_zoom;

        for (int pageno = firstPage; pageno <= lastPage; ++pageno)
        {
            double pageStart = m_page_offsets[pageno];
            double pageEnd
                = (pageno + 1 < static_cast<int>(m_page_offsets.size()))
                      ? m_page_offsets[pageno + 1] - spacingScene
                      : pageStart + pageStride(pageno);

            if (pageEnd > a0 && pageStart < a1)
                m_visible_pages_cache.insert(pageno);
        }
    }

    m_visible_pages_dirty = false;
    return m_visible_pages_cache;
}

void
DocumentView::invalidateVisiblePagesCache() noexcept
{
    m_visible_pages_dirty = true;
    // Called on every scroll and zoom: the page moves out from under a
    // hover preview, so drop it.
    if (m_has_hover_pending || m_hover_preview)
        hideLinkHoverPreview();
}

// Clear links for a specific page
void
DocumentView::clearLinksForPage(int pageno) noexcept
{
    if (m_model->supports_links() && !m_page_links_hash.contains(pageno))
        return;

    auto links = m_page_links_hash.take(pageno); // removes from hash
    for (auto *link : links)
    {
        if (!link)
            continue;

        if (link->scene() == m_gscene)
            m_gscene->removeItem(link);

        delete link;
    }
}

void
DocumentView::clearSearchItemsForPage(int pageno) noexcept
{
    if (!m_model->supports_text_search() && !m_search_items.contains(pageno))
        return;

    QGraphicsPathItem *item
        = m_search_items.take(pageno); // removes item from hash
    if (item)
    {
        if (item->scene() == m_gscene)
            m_gscene->removeItem(item);
        delete item;
    }
}

// Clear links for a specific page
void
DocumentView::clearAnnotationsForPage(int pageno) noexcept
{
    if (!m_model->supports_annotations()
        && !m_page_annotations_hash.contains(pageno))
        return;

    auto annotations
        = m_page_annotations_hash.take(pageno); // removes from hash
    for (auto *annotation : annotations)
    {
        // Remove from scene if still present
        if (annotation && annotation->scene() == m_gscene)
        {
            m_gscene->removeItem(annotation);
            delete annotation;
        }
    }
}

// Render all visible pages
void
DocumentView::renderPages() noexcept
{
    renderPagesImpl(false);
}

void
DocumentView::refreshVisiblePages() noexcept
{
    renderPagesImpl(true);
}

DocumentView::PageRenderKey
DocumentView::currentPageRenderKey() const noexcept
{
    PageRenderKey key;
    key.zoom            = m_current_zoom;
    key.rotation        = m_model->rotation();
    key.dpr             = m_model->DPR();
    key.fg              = m_model->foregroundColor();
    key.bg              = m_model->backgroundColor();
    key.flip_h          = m_model->isFlippedH();
    key.flip_v          = m_model->isFlippedV();
    key.invert          = m_model->invertColor();
    key.trim            = m_trim_margins;
    key.high_contrast   = m_config.behavior.high_contrast;
    key.dont_invert_img = m_config.behavior.dont_invert_images;
    return key;
}

void
DocumentView::setRenderClip(Model::RenderJob &job) const noexcept
{
    if (m_model->isImage())
        return;

    const GraphicsImageItem *item = m_page_items_hash.value(job.pageno, nullptr);
    if (!item)
        return;

    const QRectF br = item->boundingRect();
    if (br.isEmpty())
        return;

    // What is on screen, plus half a window on each side so small scrolls and
    // zoom steps keep landing on sharp pixels.
    QRectF vis = m_gview->mapToScene(m_gview->viewport()->rect()).boundingRect();
    vis.adjust(-vis.width() * 0.5, -vis.height() * 0.5, vis.width() * 0.5,
               vis.height() * 0.5);

    QRectF local = item->mapRectFromScene(vis).intersected(br);
    if (local.isEmpty())
    {
        // Page is off screen (preloaded): render the part nearest its top.
        const QSizeF view
            = item->mapRectFromScene(QRectF(0, 0, vis.width(), vis.height()))
                  .size();
        local = QRectF(br.topLeft(), view.boundedTo(br.size()));
    }

    job.clip_frac = QRectF(local.x() / br.width(), local.y() / br.height(),
                           local.width() / br.width(),
                           local.height() / br.height());
    job.has_clip  = true;
}

bool
DocumentView::regionNeedsRefresh(int pageno) const noexcept
{
    const GraphicsImageItem *item = m_page_items_hash.value(pageno, nullptr);
    if (!item || !item->isPartial() || !item->isVisible())
        return false;

    const QRectF vis
        = item->mapRectFromScene(
              m_gview->mapToScene(m_gview->viewport()->rect()).boundingRect())
              .intersected(item->boundingRect());
    if (vis.isEmpty())
        return false;

    return !item->imageRect().adjusted(-1, -1, 1, 1).contains(vis);
}

void
DocumentView::renderPagesImpl(bool skipCurrent) noexcept
{

    // Guard
    if (m_layout_mode == LayoutMode::SINGLE)
    {
        renderPage();
        return;
    }

    // If any rendered page revealed dimensions that differ from what
    // cachePageStride() assumed, recompute offsets before determining visible
    // pages — otherwise the wrong pages get rendered and gaps remain missing.
    if (m_page_layout_stale)
    {
        cachePageStride(); // clears m_page_layout_stale
        updateSceneRect();
        repositionPages();
        invalidateVisiblePagesCache();
    }

    const std::set<int> &visiblePages = getVisiblePages();
    const std::set<int> preloadPages  = getPreloadPages(visiblePages);

    std::set<int> pages = visiblePages;
    pages.insert(preloadPages.begin(), preloadPages.end());

    // Keep all pages in the active selection range alive so that
    // handleTextSelection never skips a middle page (missing item → gap in the
    // rendered quads) and pageAtScenePos() always resolves for the anchors.
    if (m_selection_start_page >= 0 && m_selection_end_page >= 0)
    {
        for (int p = m_selection_start_page; p <= m_selection_end_page; ++p)
            pages.insert(p);
    }

#ifndef NDEBUG
    qDebug() << "DocumentView::renderPages(): Rendering pages:" << pages;
#endif

    m_gview->setUpdatesEnabled(false);
    m_gscene->blockSignals(true);

    {
        prunePendingRenders(pages);
        removeUnusedPageItems(pages);

        // Prioritize visible pages for rendering, but also include preload
        // pages in the queue
        const PageRenderKey key = currentPageRenderKey();
        for (int pageno : visiblePages)
        {
            if (skipCurrent && m_page_render_keys.contains(pageno)
                && m_page_render_keys.value(pageno) == key
                && !m_pending_renders.contains(pageno)
                && !m_placeholder_pages.contains(pageno)
                && m_page_items_hash.value(pageno, nullptr)
                && !regionNeedsRefresh(pageno))
                continue;
            requestPageRender(pageno);
        }

        // Preload pages
        for (int pageno : preloadPages)
            requestPageRender(pageno, false, false);

        updateSceneRect();
    }
    m_gscene->blockSignals(false);
    m_gview->setUpdatesEnabled(true);

    updateCurrentHitHighlight();

    if (m_visual_line_mode)
    {
        snapVisualLine(false);
    }
}

void
DocumentView::renderImage() noexcept
{
    if (m_model->isAnimated())
    {
        QImage frame = m_model->requestImageRender(false);
        if (frame.isNull())
            return;

        m_pageno                    = 0;
        GraphicsImageItem *pageItem = m_page_items_hash.value(0, nullptr);
        if (pageItem)
        {
            // Apply zoom via transform — no pixel scaling
            pageItem->setScale(m_current_zoom);
            pageItem->setImage(frame);
        }
        else
        {
            createAndAddPageItem(0, frame);
            m_page_items_hash[0]->setScale(m_current_zoom);
        }
        updateSceneRect();
        repositionPages();
        return;
    }

    const bool highQuality = !m_hq_render_timer->isActive();
    QImage img             = m_model->requestImageRender(highQuality);
    if (img.isNull())
    {
        qWarning() << "Failed to render image";
        return;
    }
    m_pageno                    = 0;
    GraphicsImageItem *pageItem = m_page_items_hash.value(0, nullptr);
    if (pageItem)
    {
        pageItem->setScale(1.0);
        pageItem->setImage(img);
    }
    else
    {
        createAndAddPageItem(0, img);
    }
    updateSceneRect();
    repositionPages();
}

// Render a specific page (used when LayoutMode is SINGLE)
void
DocumentView::renderPage() noexcept
{
    if (m_model->isImage())
    {
        renderImage();
        return;
    }

    if (m_model->isAnimated())
    {
        updateSceneRect();
        repositionPages();
        updateCurrentHitHighlight();
        return;
    }

    m_gview->setUpdatesEnabled(false);
    m_gscene->blockSignals(true);
    {
        prunePendingRenders({m_pageno});
        removeUnusedPageItems({m_pageno});

        // Promote preload item to visible if available — instant display
        if (m_page_items_hash.contains(m_pageno))
        {
            GraphicsImageItem *item
                = m_page_items_hash.value(m_pageno, nullptr);
            if (m_preload_pages.contains(m_pageno))
            {
                m_preload_pages.remove(m_pageno);
                item->show();
                updateSceneRect();
                m_gscene->blockSignals(false);
                m_gview->setUpdatesEnabled(true);
                updateCurrentHitHighlight();
                // Still request a fresh render in case zoom changed etc,
                // but the preload gives instant feedback
                requestPageRender(m_pageno);
                return;
            }
        }

        requestPageRender(m_pageno);
        updateSceneRect();
    }
    m_gscene->blockSignals(false);
    m_gview->setUpdatesEnabled(true);

    updateCurrentHitHighlight();
}

void
DocumentView::startNextRenderJob() noexcept
{

#ifndef NDEBUG
    qDebug() << "DocumentView::startNextRenderJob(): Queue size: "
             << (m_visible_render_queue.size() + m_render_queue.size());
#endif

    while (!m_visible_render_queue.isEmpty() || !m_render_queue.isEmpty())
    {
        // Visible-page queue is always drained first; fall back to preload
        // queue
        int pageno = (!m_visible_render_queue.isEmpty())
                         ? m_visible_render_queue.dequeue()
                         : m_render_queue.dequeue();

        if (!m_pending_renders.contains(pageno))
            continue;

        auto job = m_model->createRenderJob(pageno);
        if (!m_trim_margins)
            setRenderClip(job);

        // Capture zoom at dispatch time so stale callbacks from a previous
        // zoom level can be detected and dropped in the lambda below.
        const double dispatchZoom       = m_current_zoom;
        const PageRenderKey dispatchKey = currentPageRenderKey();

        QPointer<DocumentView> self(this);
        m_model->requestPageRender(job,
                                   [self, pageno, dispatchZoom, dispatchKey](
                                       const Model::PageRenderResult &result)
        {
            if (!self)
                return;

            DocumentView *view = self.data();

            view->m_pending_renders.remove(pageno);

            // Discard renders from a previous zoom level — the bake will have
            // already queued fresh renders at the correct zoom.
            if (!qFuzzyCompare(dispatchZoom, view->m_current_zoom))
            {
                view->startNextRenderJob();
                return;
            }

            QImage image = std::move(result.image);

            if (!image.isNull() && view->m_trim_margins && !result.partial)
            {
                const QRect crop
                    = view->contentCropRectPixels(pageno).intersected(
                        image.rect());
                if (crop.isValid() && !crop.isEmpty())
                    image = image.copy(crop);
            }

            if (!image.isNull())
            {
                view->m_page_render_keys[pageno] = dispatchKey;

                if (view->m_awaiting_first_render)
                {
                    view->m_awaiting_first_render = false;
                    view->m_spinner->stop();
                    view->m_spinner->hide();
                }

                if (view->m_layout_mode == LayoutMode::SINGLE
                    && pageno != view->m_pageno)
                {
                    // Store as a hidden preload item in the scene for
                    // instant display later
                    view->m_gscene->blockSignals(true);
                    view->setUpdatesEnabled(false);
                    {
                        view->renderPageFromImage(pageno, std::move(image), result.full_size,
                                              result.region);
                        // Mark as preload and hide it for instant display later
                        if (view->m_page_items_hash.contains(pageno))
                        {
                            view->m_preload_pages.insert(pageno);
                            view->m_page_items_hash[pageno]->hide();
                        }
                    }
                    view->setUpdatesEnabled(true);
                    if (!view->m_thumbnail_mode)
                        view->renderLinks(pageno, result.links);
                    view->m_gscene->blockSignals(false);
                    view->startNextRenderJob();
                    return;
                }

                view->m_gscene->blockSignals(true);
                view->setUpdatesEnabled(false);
                {
                    view->renderPageFromImage(pageno, std::move(image), result.full_size,
                                              result.region);
                    if (!view->m_thumbnail_mode)
                    {
                        view->renderLinks(pageno, result.links);
                        view->renderAnnotations(pageno, result.annotations);
                        view->renderSearchHitsForPage(pageno);
                    }
                    view->updateCurrentHitHighlight();
                }
                view->setUpdatesEnabled(true);
                view->m_gscene->blockSignals(false);
                // m_gview->viewport()->update();

                // The view may have moved while this was rendering.
                if (view->regionNeedsRefresh(pageno))
                    view->m_scroll_page_update_timer->start();

                if (view->m_pending_jump.pageno == pageno)
                    view->GotoLocation(view->m_pending_jump);

                if (view->m_scroll_to_hit_pending && view->m_search_index >= 0
                    && !view->m_search_hit_flat_refs.empty()
                    && view->m_search_hit_flat_refs[view->m_search_index].page
                           == pageno)
                {
                    view->m_scroll_to_hit_pending = false;
                    view->updateCurrentHitHighlight();
                    // Stop timers before centerOn so the scroll signal it
                    // generates doesn't trigger another renderPages.
                    view->m_scroll_page_update_timer->stop();
                    view->m_hq_render_timer->stop();
                    view->scrollToCurrentHit();
                }
            }
            else
            {
                qWarning() << "Failed to render page" << pageno;
            }

            view->startNextRenderJob();
        });
    }
}

// Remove pending renders for pages that are no longer visible and not
// in-flight
void
DocumentView::prunePendingRenders(const std::set<int> &visiblePages) noexcept
{
    for (auto it = m_pending_renders.begin(); it != m_pending_renders.end();)
        it = visiblePages.count(*it) ? ++it : m_pending_renders.erase(it);

    auto filterQueue = [&](QQueue<int> &q)
    {
        QQueue<int> filtered;
        while (!q.isEmpty())
        {
            const int p = q.dequeue();
            if (visiblePages.contains(p))
                filtered.enqueue(p);
        }
        q = std::move(filtered);
    };
    filterQueue(m_visible_render_queue);
    filterQueue(m_render_queue);
}

void
DocumentView::removeUnusedPageItems(const std::set<int> &visibleSet) noexcept
{
    std::vector<int> toRemove;
    for (auto it = m_page_items_hash.cbegin(); it != m_page_items_hash.cend();
         ++it)
    {
        const int pageno = it.key();
        auto *item       = it.value();

        if (visibleSet.count(pageno))
            continue;

        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);

        if (!item)
        {
            toRemove.push_back(pageno);
            continue;
        }

        // Keep placeholders to avoid flicker during fast scroll — hide so
        // they don't repaint but remain ready when rendering finishes.
        if (m_placeholder_pages.contains(pageno))
        {
            if (item->scene() == m_gscene)
                item->hide();
            continue;
        }

        toRemove.push_back(pageno);
    }
    for (int pageno : toRemove)
    {
        auto *item = m_page_items_hash.take(pageno);
        m_preload_pages.remove(pageno);

        if (item && item->scene() == m_gscene)
            m_gscene->removeItem(item);
        delete item;
    }
}

// Remove a page item from the scene and delete it
void
DocumentView::removePageItem(int pageno) noexcept
{
    if (m_page_items_hash.contains(pageno))
    {
        GraphicsImageItem *item = m_page_items_hash.take(pageno);
        m_placeholder_pages.remove(pageno);
        m_preload_pages.remove(pageno);
        if (item && item->scene() == m_gscene)
            m_gscene->removeItem(item);
        delete item;
    }
}

void
DocumentView::cachePageStride() noexcept
{
    m_page_layout_stale = false;

    const int N = m_model->numPages();
    if (N <= 0)
        return;

    m_page_offsets.resize(N + 1);

    const double spacingScene = m_spacing * m_current_zoom;
    const double rot          = std::fmod(std::abs(m_model->rotation()), 360.0);
    const bool rotated        = (rot == 90.0 || rot == 270.0);

    // Helper to get extent quickly
    auto getExtents = [&](int p, double &w, double &h)
    {
        const auto dim = pageDimensionsPts(p);
        w = (dim.width_pts / 72.0) * m_model->DPI() * m_current_zoom;
        h = (dim.height_pts / 72.0) * m_model->DPI() * m_current_zoom;
        if (rotated)
            std::swap(w, h);
    };

    double cursor   = 0.0;
    double maxCross = 0.0;

    if (m_layout_mode == LayoutMode::BOOK)
    {
        for (int i = 0; i < N;)
        {
            if (i == 0)
            { // Cover
                double w, h;
                getExtents(i, w, h);
                m_page_offsets[i] = cursor;
                maxCross          = std::max(maxCross, w * 2.0);
                cursor += h + spacingScene;
                i++;
            }
            else
            {
                // Spreads
                double w1, h1, w2 = 0, h2 = 0;
                getExtents(i, w1, h1);
                if (i + 1 < N)
                    getExtents(i + 1, w2, h2);

                m_page_offsets[i] = cursor;
                if (i + 1 < N)
                    m_page_offsets[i + 1] = cursor;

                maxCross = std::max(maxCross, w1 + w2);
                cursor += std::max(h1, h2) + spacingScene;
                i += 2;
            }
        }
    }
    else
    {
        if (m_thumbnail_mode && m_thumbnail_label_height == 0.0)
        {
            QFont font;
            font.setPointSizeF(m_config.thumbnail.font_size);
            const QFontMetricsF metrics(font);
            m_thumbnail_label_height
                = metrics.height() + (metrics.height() * 0.25) + 10.0;
        }

        const bool horizontal = (m_layout_mode == LayoutMode::HORIZONTAL);
        for (int i = 0; i < N; ++i)
        {
            m_page_offsets[i] = cursor;
            double w, h;
            getExtents(i, w, h);
            cursor += (horizontal ? w : h) + spacingScene;

            if (m_thumbnail_mode)
                cursor += m_thumbnail_label_height;

            maxCross = std::max(maxCross, horizontal ? h : w); // <-- add this
        }
    }

    m_page_offsets[N]       = cursor;
    m_max_page_cross_extent = maxCross;
    invalidateVisiblePagesCache();
}

std::set<int>
DocumentView::getPreloadPages(const std::set<int> &visiblePages) noexcept
{
    if (visiblePages.empty())
        return {};

    const int numPages     = m_model->numPages();
    const int firstVisible = *visiblePages.begin();
    const int lastVisible  = *visiblePages.rbegin();

    // Use the stride of the current page as the preload lookahead distance
    const double preloadDistance
        = pageStride(std::clamp(m_pageno, 0, numPages - 1))
          * m_config.behavior.preload_pages;

    const double ahead  = m_page_offsets[lastVisible] + preloadDistance;
    const double behind = m_page_offsets[firstVisible] - preloadDistance;

    std::set<int> preloadPages;

    for (int p = firstVisible - 1; p >= 0; --p)
    {
        if (m_page_offsets[p] < behind)
            break;
        preloadPages.insert(p);
    }

    for (int p = lastVisible + 1; p < numPages; ++p)
    {
        if (m_page_offsets[p] > ahead)
            break;
        preloadPages.insert(p);
    }

    return preloadPages;
}

// Update the scene rect based on number of pages and page stride
void
DocumentView::updateSceneRect() noexcept
{
    const double viewW = m_gview->viewport()->width();
    const double viewH = m_gview->viewport()->height();

    // Compute the full layout rect without touching the view yet.
    QRectF layoutRect;
    if (m_layout_mode == LayoutMode::SINGLE)
    {
        const QSizeF page    = pageSceneSize(m_pageno);
        const double xMargin = std::max(0.0, (viewW - page.width()) / 2.0);
        const double yMargin = std::max(0.0, (viewH - page.height()) / 2.0);
        const double sceneW  = std::max(viewW, page.width());
        const double sceneH  = std::max(viewH, page.height());
        layoutRect           = QRectF(-xMargin, -yMargin, sceneW, sceneH);
    }
    else if (m_layout_mode == LayoutMode::HORIZONTAL)
    {
        const double totalWidth = totalPageExtent();
        const double sceneH     = std::max(viewH, m_max_page_cross_extent);
        const double xMargin    = std::max(0.0, (viewW - totalWidth) / 2.0);
        const double yMargin
            = std::max(0.0, (viewH - pageSceneSize(m_pageno).height()) / 2.0);
        layoutRect
            = QRectF(-xMargin, -yMargin, totalWidth + 2.0 * xMargin, sceneH);
    }
    else if (m_layout_mode == LayoutMode::BOOK)
    {
        const double totalHeight  = totalPageExtent();
        const double sceneW       = std::max(viewW, m_max_page_cross_extent);
        const double cappedSceneW = std::min(sceneW, 20000.0);
        const double yMargin
            = std::max(0.0, (viewH - pageSceneSize(m_pageno).height()) / 2.0);
        layoutRect
            = QRectF(0, -yMargin, cappedSceneW, totalHeight + 2.0 * yMargin);
    }
    else
    {
        // VERTICAL
        double totalHeight  = totalPageExtent();
        const double sceneW = std::max(viewW, m_max_page_cross_extent);
        const double yMargin
            = std::max(0.0, (viewH - pageSceneSize(m_pageno).height()) / 2.0);

        layoutRect = QRectF(0, -yMargin, sceneW, totalHeight + 2.0 * yMargin);

        if (m_thumbnail_mode)
            layoutRect = layoutRect.united(m_gscene->itemsBoundingRect());
    }

    // Always keep the layout rect up to date for page positioning.
    m_layout_scene_rect = layoutRect;

    if (m_is_narrow)
    {
        // Only make one setSceneRect call to avoid double scroll-signal firing
        // that would create a render loop (each setSceneRect can change the
        // scrollbar value and restart the render timer).
        const QRectF nr = narrowSceneRect();
        if (nr.isValid())
            m_gview->setSceneRect(nr);
        else
            m_gview->setSceneRect(layoutRect);
    }
    else
    {
        m_gview->setSceneRect(layoutRect);
    }
}

bool
DocumentView::scenePosToPage(QPointF scenePos, int &pageno,
                             QPointF &pagePoint) const noexcept
{
    GraphicsImageItem *item = nullptr;
    if (!pageAtScenePos(scenePos, pageno, item) || !item)
        return false;

    const fz_point pt
        = m_model->toPDFSpace(pageno, item->mapFromScene(scenePos));
    pagePoint = QPointF(pt.x, pt.y);
    return true;
}

bool
DocumentView::pagePosToScene(int pageno, QPointF pagePoint,
                             QPointF &scenePos) const noexcept
{
    const GraphicsImageItem *item = m_page_items_hash.value(pageno, nullptr);
    if (!item)
        return false;

    const QPointF local = m_model->toPixelSpace(
        pageno, fz_point{float(pagePoint.x()), float(pagePoint.y())});
    scenePos = item->mapToScene(local);
    return true;
}

int
DocumentView::nearestPageToScenePos(QPointF scenePos) const noexcept
{
    const int N = m_model->numPages();
    if (N <= 0)
        return -1;
    if (m_layout_mode == LayoutMode::SINGLE)
        return m_pageno;
    if (m_page_offsets.size() < static_cast<size_t>(N + 1))
        return -1;

    const double coord = (m_layout_mode == LayoutMode::HORIZONTAL)
                             ? scenePos.x()
                             : scenePos.y();
    const auto it = std::upper_bound(m_page_offsets.cbegin(),
                                     m_page_offsets.cend(), coord);
    return std::clamp(
        static_cast<int>(std::distance(m_page_offsets.cbegin(), it) - 1), 0,
        N - 1);
}

bool
DocumentView::pageAtScenePos(QPointF scenePos, int &outPageIndex,
                             GraphicsImageItem *&outPageItem) const noexcept
{
    outPageIndex = -1;
    outPageItem  = nullptr;

    const int N = m_model->numPages();
    if (N <= 0 || m_page_offsets.size() < static_cast<size_t>(N + 1))
        return false;

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        auto it = m_page_items_hash.find(m_pageno);
        if (it != m_page_items_hash.end() && it.value()
            && it.value()->sceneBoundingRect().contains(scenePos))
        {
            outPageIndex = m_pageno;
            outPageItem  = it.value();
            return true;
        }
        return false;
    }

    // pageOffset(i) is the main-axis start of page i.
    // upper_bound(coord) gives the first entry strictly greater than coord,
    // so the page that owns coord is one slot before that iterator.
    const double coord = (m_layout_mode == LayoutMode::HORIZONTAL)
                             ? scenePos.x()
                             : scenePos.y();

    const auto it1 = std::upper_bound(m_page_offsets.cbegin(),
                                      m_page_offsets.cend(), coord);
    // Candidate is the page whose slot contains coord on the main axis.
    int candidate
        = static_cast<int>(std::distance(m_page_offsets.cbegin(), it1) - 1);
    candidate = std::clamp(candidate, 0, N - 1);

    // ── Try candidate, then expand outward
    // With variable page sizes the binary search is exact for the main
    // axis, but the cross-axis check (sceneBoundingRect) can still miss
    // e.g. during a zoom animation frame. Expanding ±1 covers that
    // transient case without ever needing more — the binary search already
    // pins the main-axis page correctly, so ±1 is now a genuine safety net
    // rather than the primary mechanism.

    std::vector<int> candidates;
    if (m_layout_mode == LayoutMode::BOOK)
    {
        candidates.push_back(candidate);
        // Spread partner
        if (candidate == 0)
        {
            // Cover is alone, but check page 1 as neighbor
            candidates.push_back(1);
        }
        else if (candidate % 2 != 0)
        {
            // Odd page (left side) → partner is candidate+1
            candidates.push_back(candidate + 1);
            candidates.push_back(candidate - 1);
        }
        else
        {
            // Even page (right side) → partner is candidate-1
            candidates.push_back(candidate - 1);
            candidates.push_back(candidate + 1);
        }
    }
    else
    {
        candidates = {candidate, candidate - 1, candidate + 1};
    }

    for (int pg : candidates)
    {
        if (pg < 0 || pg >= N)
            continue;
        auto jt = m_page_items_hash.find(pg);
        if (jt != m_page_items_hash.end() && jt.value()
            && jt.value()->sceneBoundingRect().contains(scenePos))
        {
            outPageIndex = pg;
            outPageItem  = jt.value();
            return true;
        }
    }

    return false;
}

void
DocumentView::clearVisiblePages() noexcept
{
    // m_page_items_hash
    for (auto it = m_page_items_hash.begin(); it != m_page_items_hash.end();
         ++it)
    {
        GraphicsImageItem *item = it.value();
        if (item->scene() == m_gscene)
        {
            m_gscene->removeItem(item);
            delete item;
        }
    }
    m_page_items_hash.clear();
    m_placeholder_pages.clear();
    m_preload_pages.clear();
}

void
DocumentView::clearVisibleLinks() noexcept
{
    if (!m_model->supports_links())
        return;
    for (auto it = m_page_links_hash.begin(); it != m_page_links_hash.end();
         ++it)
    {
        for (auto *link : it.value())
        {
            if (link->scene() == m_gscene)
                m_gscene->removeItem(link);
            delete link;
        }
    }
    m_page_links_hash.clear();
}

void
DocumentView::clearVisibleAnnotations() noexcept
{
    if (!m_model->supports_annotations())
        return;
    for (auto it = m_page_annotations_hash.begin();
         it != m_page_annotations_hash.end(); ++it)
    {
        for (auto *annot : it.value())
        {
            if (annot->scene() == m_gscene)
                m_gscene->removeItem(annot);
            delete annot;
        }
    }
    m_page_annotations_hash.clear();
}

// Private helper — O(log N), call only in multi-page modes.
int
DocumentView::pageAtAxisCoord(double coord) const noexcept
{
    const auto it = std::upper_bound(m_page_offsets.cbegin(),
                                     m_page_offsets.cend(), coord);
    const int candidate
        = static_cast<int>(std::distance(m_page_offsets.cbegin(), it) - 1);
    return std::clamp(candidate, 0, m_model->numPages() - 1);
}

void
DocumentView::updateCurrentPage() noexcept
{
    ensureVisiblePagePlaceholders();

#ifndef NDEBUG
    qDebug()
        << "DocumentView::updateCurrentPage(): Updating current page based "
        << "on scroll position. Current page:" << m_pageno + 1;
#endif

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        emit currentPageChanged(m_pageno + 1);
        return;
    }

    const int viewportHalf = (m_layout_mode == LayoutMode::HORIZONTAL)
                                 ? m_gview->viewport()->width() / 2
                                 : m_gview->viewport()->height() / 2;

    const int scrollPos = (m_layout_mode == LayoutMode::HORIZONTAL)
                              ? m_hscroll->value()
                              : m_vscroll->value();

    // Map the viewport centre into scene coordinates on the layout axis.
    // QGraphicsView scroll values are in scene-pixel units, so this is
    // direct.
    const double centerCoord = static_cast<double>(scrollPos + viewportHalf);

    const int new_page = pageAtAxisCoord(centerCoord);
    if (new_page == m_pageno)
        return;

    m_pageno = new_page;
    emit currentPageChanged(new_page + 1);

    // Reset index when scrolling into a new page
    if (m_visual_line_mode)
    {
        m_visual_line_index = -1;
        snapVisualLine(false);
    }

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnPageChanged);
#endif
}

void
DocumentView::ensureVisiblePagePlaceholders() noexcept
{
    const std::set<int> &visiblePages = getVisiblePages();

    // Quick check - if we already have all pages, return early
    bool allExist = true;
    for (int pageno : visiblePages)
    {
        if (!m_page_items_hash.contains(pageno))
        {
            allExist = false;
            break;
        }
    }

    if (allExist)
        return;

    for (int pageno : visiblePages)
    {
        if (!m_page_items_hash.contains(pageno))
            createAndAddPlaceholderPageItem(pageno);
    }
}

void
DocumentView::clearDocumentItems() noexcept
{
    invalidateVisiblePagesCache();
    m_page_render_keys.clear();

    // Reset narrow state when document is cleared
    m_is_narrow   = false;
    m_narrow_page = -1;
    m_gview->clearNarrowRect();

    m_page_annotations_hash.clear();
    m_page_links_hash.clear();
    m_page_items_hash.clear();
    m_search_items.clear();
    m_pending_renders.clear();
    m_visible_render_queue.clear();
    m_render_queue.clear();
    m_placeholder_pages.clear();
    m_preload_pages.clear();

    // Remove all items from scene EXCEPT the persistent ones
    // (selection path, search hit, etc.)
    const auto items = m_gscene->items();
    for (auto *item : items)
    {
        if (item != m_selection_path_item && item != m_current_search_hit_item
            && item != m_jump_marker && item != m_visual_line_item)
        {
            m_gscene->removeItem(item);
            delete item;
        }
    }

    ClearTextSelection();
    m_gscene->setSceneRect(QRectF()); // Reset scene bounds
}

// Request rendering of a specific page (ASYNC)
void
DocumentView::requestPageRender(int pageno, bool force, bool visible) noexcept
{
    if (!force && m_pending_renders.contains(pageno))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::requestPageRender(): Requesting page render for "
                "pageno = "
             << pageno;
#endif

    m_pending_renders.insert(pageno);
    if (visible)
        m_visible_render_queue.enqueue(pageno);
    else
        m_render_queue.enqueue(pageno);
    createAndAddPlaceholderPageItem(pageno);
    startNextRenderJob();
}

void
DocumentView::renderPageFromImage(int pageno, QImage image, QSize fullSize,
                                   QRect region) noexcept
{
    // Remove old item (placeholder OR real page) BEFORE adding the new
    // item, since createAndAddPageItem overwrites the hash entry.  Without
    // this, re-renders after zoom leave orphaned items in the scene
    // (visible stale pages and unbounded memory growth).
#ifndef NDEBUG
    qDebug()
        << "DocumentView::renderPageFromImage(): Rendering page from image "
        << "for pageno = " << pageno;
#endif
    bool wasHighlighted = false;
    auto it             = m_page_items_hash.find(pageno);
    if (it != m_page_items_hash.end())
    {
        GraphicsImageItem *old = it.value();
        if (old && old->scene() == m_gscene)
        {
            wasHighlighted = old->isHighlighted();
            m_gscene->removeItem(old);
            delete old;
        }
        m_page_items_hash.remove(pageno);
    }

    // New item pointer will differ from the cached one — force one recompute.
    m_cached_hit_page_item = nullptr;

    createAndAddPageItem(pageno, std::move(image), fullSize, region);

    if (wasHighlighted)
        if (auto *newItem = m_page_items_hash.value(pageno, nullptr))
            newItem->setHighlighted(true);
    clearLinksForPage(pageno);
    clearAnnotationsForPage(pageno);
    clearSearchItemsForPage(pageno);

    // If this is the narrow page, refresh the dim overlay and scene rect so
    // they reflect the new page geometry (e.g. after rotation re-render).
    if (m_is_narrow && pageno == m_narrow_page)
        refreshNarrowVisuals();

    // Detect when a page's true dimensions differ from what cachePageStride()
    // assumed (e.g. cover page is a different size than content pages). In
    // that case the cached offsets are wrong and gaps between pages disappear
    // visually. Flag for a re-layout on the next renderPages() pass.
    if (!m_page_layout_stale && m_layout_mode != LayoutMode::SINGLE
        && m_layout_mode != LayoutMode::BOOK
        && pageno + 1 < static_cast<int>(m_page_offsets.size()))
    {
        const bool hz           = (m_layout_mode == LayoutMode::HORIZONTAL);
        const QSizeF sz         = pageSceneSize(pageno);
        const double trueExtent = hz ? sz.width() : sz.height();
        const double usedStride
            = m_page_offsets[pageno + 1] - m_page_offsets[pageno];
        const double trueStride = trueExtent + m_spacing * m_current_zoom;
        if (std::abs(usedStride - trueStride) > 0.5)
            m_page_layout_stale = true;
    }
}

void
DocumentView::createAndAddPlaceholderPageItem(int pageno) noexcept
{
    if (m_page_items_hash.contains(pageno))
        return;

    const QSizeF logicalSize = pageSceneSize(pageno);
    if (logicalSize.isEmpty())
        return;

    // Create a minimal 1x1 image for placeholder (memory efficient)
    QImage img(1, 1, QImage::Format_RGB32);
    img.fill(m_model->invertColor() ? Qt::black : Qt::white);

    auto *pageItem = new GraphicsImageItem();
    pageItem->setImage(img);
    pageItem->setTransform(
        QTransform::fromScale(logicalSize.width() / img.width(),
                              logicalSize.height() / img.height()));

    const double pageW = logicalSize.width();
    const double pageH = logicalSize.height();
    const QRectF sr    = m_layout_scene_rect.isValid() ? m_layout_scene_rect
                                                       : m_gview->sceneRect();

    if (m_layout_mode == LayoutMode::HORIZONTAL)
    {
        const double yOffset = (m_max_page_cross_extent - pageH) / 2.0;
        const double xPos    = pageOffset(pageno);
        pageItem->setPos(xPos, yOffset);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        const double xPos = sr.x() + (sr.width() - pageW) / 2.0;
        const double yPos = sr.y() + (sr.height() - pageH) / 2.0;
        pageItem->setPos(xPos, yPos);
    }
    else
    {
        // BOOK or TOP_TO_BOTTOM
        pageItem->setPos(pageXOffset(pageno, pageW, sr.width()),
                         pageOffset(pageno));
    }

    m_gscene->addItem(pageItem);
    m_page_items_hash[pageno] = pageItem;
    m_placeholder_pages.insert(pageno);
}

void
DocumentView::createAndAddPageItem(int pageno, QImage img, QSize fullSize,
                                   QRect region) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::createAndAddPageItem(): Adding page item for "
             << "pageno = " << pageno;
#endif
    auto *pageItem = new GraphicsImageItem();
    if (fullSize.isValid() && !region.isEmpty())
        pageItem->setPartialImage(std::move(img), fullSize, region);
    else
        pageItem->setImage(std::move(img));

    // Logical scene size of the rendered image.
    const QSizeF logicalSize = pageSceneSize(pageno);
    const double pageW       = logicalSize.width();
    const double pageH       = logicalSize.height();
    const QRectF sr = m_layout_scene_rect.isValid() ? m_layout_scene_rect
                                                    : m_gview->sceneRect();

    if (m_layout_mode == LayoutMode::HORIZONTAL)
    {
        const double yPos = (m_max_page_cross_extent - pageH) / 2.0;
        pageItem->setPos(pageOffset(pageno), yPos);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        pageItem->setPos(sr.x() + (sr.width() - pageW) / 2.0,
                         sr.y() + (sr.height() - pageH) / 2.0);
    }
    else // TOP_TO_BOTTOM & BOOK
    {
        const qreal xPos = pageXOffset(pageno, pageW, sr.width());
        const qreal yPos = pageOffset(pageno);
        pageItem->setPos(xPos, yPos);
    }

    if (m_thumbnail_mode)
    {
        if (m_config.thumbnail.show_page_numbers)
            pageItem->setPageNumber(pageno, m_config.thumbnail.font_size);
    }

    m_gscene->addItem(pageItem);
    m_page_items_hash[pageno] = pageItem;
    m_placeholder_pages.remove(pageno);
    m_preload_pages.remove(pageno);
}

// Reposition every live page item at the new zoom
void
DocumentView::repositionPages()
{
    const QRectF sr = m_layout_scene_rect.isValid() ? m_layout_scene_rect
                                                    : m_gview->sceneRect();

    // For VERTICAL, each page may have a different width so we must
    // compute the centering offset per-page inside the loop. Using a single
    // offset based on m_pageno mispositions all pages that differ in width.

    for (auto it = m_page_items_hash.begin(); it != m_page_items_hash.end();
         ++it)
    {
        const int i             = it.key();
        GraphicsImageItem *item = it.value();

        if (!item)
            continue;

        const bool isPlaceholder = m_placeholder_pages.contains(i);

        double pageWidthScene  = 0.0;
        double pageHeightScene = 0.0;

        if (isPlaceholder)
        {
            // Use this page's own dimensions, not the current page's.
            const QSizeF logicalSize = pageSceneSize(i);
            const QImage &img        = item->image();
            if (!img.isNull() && img.width() > 0 && img.height() > 0)
            {
                item->setScale(1.0);
                item->setTransform(
                    QTransform::fromScale(logicalSize.width() / img.width(),
                                          logicalSize.height() / img.height()));
            }
            pageWidthScene  = logicalSize.width();
            pageHeightScene = logicalSize.height();
        }
        else
        {
            // Scale the existing image so its height matches the target
            // physical pixel height for *this* page at the new zoom level.
            // For images rotated 90°/270° the rendered height corresponds to
            // the original page width, so swap the dimension used.
            const auto &pageDimR = pageDimensionsPts(i);
            const double rot90
                = std::fmod(std::abs(m_model->rotation()), 360.0);
            const bool swapped
                = (rot90 == 90.0 || rot90 == 270.0)
                  && (m_model->isImage()
                      || m_model->fileType() == Model::FileType::DJVU);
            const double heightPts
                = swapped ? pageDimR.width_pts : pageDimR.height_pts;
            const double targetPixelHeight = heightPts * m_model->DPR()
                                             * m_current_zoom * m_model->DPI()
                                             / 72.0;

            const QImage &img = item->image();
            if (img.isNull() || img.height() == 0 || img.width() == 0)
            {
                qWarning() << "DocumentView::repositionPages(): Current image "
                           << "is null or has zero width/height for page" << i
                           << "- skipping scaling to avoid errors.";
                continue;
            }

            // height() is the whole page's pixel height even when only part
            // of it is resident.
            const double currentImageHeight
                = static_cast<double>(item->height());

            if (currentImageHeight <= 0.0)
            {
                qWarning() << "DocumentView::repositionPages(): Current image "
                           << "height is zero or negative for page" << i
                           << "- skipping scaling to avoid division by zero.";
                continue; // avoid division by zero
            }

            item->setScale(targetPixelHeight / currentImageHeight);

            pageWidthScene  = item->boundingRect().width() * item->scale();
            pageHeightScene = item->boundingRect().height() * item->scale();
        }

        if (m_layout_mode == LayoutMode::HORIZONTAL)
        {
            const double yOffset
                = (m_max_page_cross_extent - pageHeightScene) / 2.0;
            const double xPos = pageOffset(i);
            item->setPos(xPos, yOffset);
        }
        else if (m_layout_mode == LayoutMode::SINGLE)
        {
            item->setPos(sr.x() + (sr.width() - pageWidthScene) / 2.0,
                         sr.y() + (sr.height() - pageHeightScene) / 2.0);
        }
        else // TOP_TO_BOTTOM
        {
            item->setPos(pageXOffset(i, pageWidthScene, sr.width()),
                         pageOffset(i));
        }

        // m_model->invalidatePageCache(i);
        clearLinksForPage(i);
        clearAnnotationsForPage(i);
        clearSearchItemsForPage(i);
    }

    renderSearchHitsInScrollbar();

    if (m_visual_line_mode)
        snapVisualLine(false);

    if (m_is_narrow)
        refreshNarrowVisuals();
}

void
DocumentView::stopPendingRenders() noexcept
{
    m_pending_renders.clear();
    m_visible_render_queue.clear();
    m_render_queue.clear();

    if (m_model)
        m_model->waitForPendingRenders();
}

// Helper: O(1) start position of page i in scene axis coordinates
double
DocumentView::pageOffset(int pageno) const noexcept
{
    if (pageno < 0 || pageno >= static_cast<int>(m_page_offsets.size()) - 1)
        return 0.0;
    return m_page_offsets[pageno];
}

double
DocumentView::pageXOffset(int pageno, double pageW,
                          double sceneW) const noexcept
{
    if (m_layout_mode == LayoutMode::BOOK)
    {
        const double spacingScene = m_spacing * m_current_zoom;
        const double spineX       = sceneW / 2.0;
        if (pageno == 0)
            return spineX + spacingScene; // Cover is on the right
        return (pageno % 2 != 0)
                   ? (spineX - pageW)
                   : spineX + spacingScene; // Odd=Left, Even=Right
    }
    return (sceneW - pageW) / 2.0; // Centered for Single/Top-to-Bottom
}

// Helper: stride (extent + spacing) of a specific page
double
DocumentView::pageStride(int pageno) const noexcept
{
    if (pageno < 0 || pageno >= static_cast<int>(m_page_offsets.size()) - 1)
        return 0.0;

    if (m_layout_mode == LayoutMode::BOOK)
    {
        // Look ahead to the index of the next row
        int nextIdx = (pageno == 0) ? 1 : pageno + (pageno % 2 != 0 ? 2 : 1);
        nextIdx
            = std::min(nextIdx, static_cast<int>(m_page_offsets.size()) - 1);
        return m_page_offsets[nextIdx] - m_page_offsets[pageno];
    }

    return m_page_offsets[pageno + 1] - m_page_offsets[pageno];
}

QImage
DocumentView::currentPageImage() noexcept
{
    const bool plainImage = m_model->isImage();
    const int pageno      = plainImage ? 0 : m_pageno;

    if (!plainImage)
    {
        const QSizeF size = m_model->pageSizePts(pageno, true);
        if (!size.isEmpty())
        {
            const QImage image = m_model->renderPtsRegion(
                pageno, QRectF(QPointF(0, 0), size), 150.0f);
            if (!image.isNull())
                return image;
        }
    }

    // Image documents (and formats without a point-space renderer): what is
    // already on screen.
    GraphicsImageItem *item = m_page_items_hash.value(pageno, nullptr);
    if (!item || m_placeholder_pages.contains(pageno))
        return {};
    return item->imageRegion(QRect(0, 0, item->width(), item->height()));
}

// A page as a picture for exporting. A document with pages is rendered at
// `dpi`; a picture is taken with its own pixels (animated images and SVG as
// they are shown).
QImage
DocumentView::renderPageForExport(int pageno, int dpi) noexcept
{
    QImage image;
    if (m_model->isImage())
    {
        if (!m_model->isAnimated())
        {
            QImageReader reader(filePath());
            reader.setAutoTransform(true);
            const QByteArray kind = reader.format();
            if (kind != "svg" && kind != "svgz")
                image = reader.read();
        }
        if (image.isNull())
            image = currentPageImage();
        return image;
    }

    const QSizeF size = m_model->pageSizePts(pageno, true);
    if (!size.isEmpty())
        image = m_model->renderPtsRegion(pageno, QRectF(QPointF(0, 0), size),
                                         static_cast<float>(dpi));
    if (image.isNull() && pageno == m_pageno)
        image = currentPageImage(); // what is on screen, if nothing else works
    return image;
}

bool
DocumentView::exportPages(const QStringList &namesIn,
                          const std::vector<int> &pages, int dpi,
                          bool overwrite, QStringList *written, QString *error,
                          bool *existing, bool split) noexcept
{
    auto fail = [error](const QString &message)
    {
        if (error)
            *error = message;
        return false;
    };
    if (existing)
        *existing = false;

    if (!m_model || m_model->numPages() <= 0)
        return fail(tr("There is no document"));
    if (pages.empty())
        return fail(tr("No pages were given"));
    for (const int p : pages)
        if (p < 0 || p >= m_model->numPages())
            return fail(tr("There is no page %1").arg(p + 1));
    if (dpi < 10 || dpi > 1200)
        return fail(tr("The resolution must be between 10 and 1200 dpi"));

    QStringList names;
    for (const QString &n : namesIn)
        if (!n.trimmed().isEmpty())
            names << n.trimmed();
    if (names.isEmpty())
        return fail(tr("No file name was given"));

    // PNG unless a name says something else.
    for (QString &n : names)
        if (QFileInfo(n).suffix().isEmpty())
            n += QStringLiteral(".png");
    const QString suffix = QFileInfo(names.first()).suffix().toLower();
    for (const QString &n : std::as_const(names))
        if (QFileInfo(n).suffix().toLower() != suffix)
            return fail(tr("All the files must be of the same kind (%1)").arg(suffix));

    static const QStringList writerFormats
        = {"pdf", "svg", "txt", "text", "html", "xhtml", "cbz", "docx", "odt"};
    const bool picture = suffix != QLatin1String("pdf") && suffix != QLatin1String("svg")
                         && QImageWriter::supportedImageFormats().contains(suffix.toLatin1());
    const bool writer = writerFormats.contains(suffix);
    if (!picture && !writer)
        return fail(tr("Cannot write \"%1\" files (try png, jpg, webp, bmp, tif, pdf, "
                       "svg, txt, html, cbz, docx or odt)")
                        .arg(suffix));
    if (!picture && suffix != QLatin1String("pdf") && !m_model->supportsWriterExport())
        return fail(tr("%1 cannot be written from an image or DjVu document (try "
                       "png, jpg or pdf)")
                        .arg(suffix));

    // The files that will be made. Pictures and SVG are always one file per
    // page; the other formats make one file for all the pages, unless asked to
    // `split` them.
    const bool oneFilePerPage = picture || suffix == QLatin1String("svg") || split;
    QStringList targets;
    if (!oneFilePerPage)
    {
        if (names.size() != 1)
            return fail(tr("%1 is one file: give one file name").arg(suffix));
        targets = names;
    }
    else if (names.size() == 1)
    {
        const int highest = *std::max_element(pages.begin(), pages.end()) + 1;
        const int width   = QString::number(highest).size();
        for (const int p : pages)
            targets << ((pages.size() == 1 && !page_range::hasPlaceholder(names.first()))
                            ? names.first()
                            : page_range::nameForPage(names.first(), p + 1, width));
    }
    else if (names.size() == static_cast<qsizetype>(pages.size()))
        targets = names;
    else
        return fail(tr("Give one name, or one name for each of the %1 pages")
                        .arg(pages.size()));

    QStringList sorted = targets;
    sorted.sort();
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        return fail(tr("Two pages would be written to the same file"));
    for (const QString &t : std::as_const(targets))
    {
        const QFileInfo info(t);
        if (!info.dir().exists())
            return fail(tr("The folder %1 does not exist").arg(info.absolutePath()));
        if (!overwrite && info.exists())
        {
            if (existing)
                *existing = true;
            return fail(tr("%1 already exists").arg(t));
        }
    }

    // Writing: each file gets its pages (one page for pictures, SVG and split
    // files; all of them otherwise).
    auto writeFile = [&](const std::vector<int> &filePages, const QString &target) -> bool
    {
        if (picture)
        {
            const int p        = filePages.front();
            const QImage image = renderPageForExport(p, dpi);
            if (image.isNull())
                return fail(tr("Page %1 could not be rendered").arg(p + 1));
            QImageWriter w(target, suffix.toLatin1());
            if (!w.write(image))
                return fail(tr("Page %1: %2").arg(p + 1).arg(w.errorString()));
            return true;
        }
        if (m_model->supportsWriterExport())
        {
            QString problem;
            if (!m_model->exportWithWriter(filePages, {target}, suffix, &problem))
                return fail(problem);
            return true;
        }
        // pdf from an image or DjVu document: the pages as pictures in a pdf
        QPdfWriter pdf(target);
        pdf.setResolution(dpi);
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter;
        bool first = true;
        for (const int p : filePages)
        {
            const QImage image = renderPageForExport(p, dpi);
            if (image.isNull())
                return fail(tr("Page %1 could not be rendered").arg(p + 1));
            pdf.setPageSize(QPageSize(QSizeF(image.width() * 72.0 / dpi,
                                             image.height() * 72.0 / dpi),
                                      QPageSize::Point));
            if (first)
            {
                if (!painter.begin(&pdf))
                    return fail(tr("Could not write %1").arg(target));
                first = false;
            }
            else
                pdf.newPage();
            painter.drawImage(QRect(0, 0, pdf.width(), pdf.height()), image);
        }
        painter.end();
        return true;
    };

    if (oneFilePerPage)
    {
        for (qsizetype i = 0; i < targets.size(); ++i)
            if (!writeFile({pages[i]}, targets.at(i)))
                return false;
    }
    else if (!writeFile(pages, targets.first()))
        return false;

    if (written)
        *written = targets;
    return true;
}
