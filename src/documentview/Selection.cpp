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

static bool
mapRegionToPageRects(QRectF area, GraphicsImageItem *pageItem,
                     QRectF &outLogical, QRect &outPixels) noexcept
{
    if (!pageItem)
        return false;

    const QRectF pageRect = pageItem->mapFromScene(area).boundingRect();
    const qreal dpr       = pageItem->devicePixelRatio();
    const QSize pixSize   = QSize(pageItem->width(), pageItem->height());
    const QRectF logicalBounds(
        QPointF(0.0, 0.0),
        QSizeF(pixSize.width() / dpr, pixSize.height() / dpr));

    outLogical = pageRect.intersected(logicalBounds);
    if (outLogical.isEmpty())
        return false;

    const QRectF pixelRect(outLogical.x() * dpr, outLogical.y() * dpr,
                           outLogical.width() * dpr, outLogical.height() * dpr);
    const QRectF pixmapBounds(QPointF(0.0, 0.0), QSizeF(pixSize));
    const QRectF clippedPixels = pixelRect.intersected(pixmapBounds);
    if (clippedPixels.isEmpty())
        return false;

    outPixels = clippedPixels.toRect();
    return true;
}

void
DocumentView::handleSearchResults(
    const QMap<int, std::vector<Model::SearchHit>> &results) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleSearchResults(): Received"
             << results.size() << "pages with search hits.";
#endif

    // Drop results from a superseded search — no stale spinner-hide, no
    // stale "No matches" modal seconds after the user cancelled or moved on.
    if (m_search_dispatched_gen != m_search_gen)
        return;

    emit searchBarSpinnerShow(false);
    clearSearchHits();
    // clearSearchHits bumped m_search_gen; re-align so buildFlatSearchHitIndex
    // below is not itself considered stale.
    m_search_dispatched_gen = m_search_gen;

    QMap<int, std::vector<Model::SearchHit>> filtered = results;
    filterHitsToNarrow(filtered);

    if (filtered.isEmpty())
    {
        QMessageBox::information(this, tr("Search"),
                                 tr("No matches found for "
                                    "the given term."));
        return;
    }

    m_search_hits = std::move(filtered);
    buildFlatSearchHitIndex();

    m_search_index         = 0;
    m_cached_hit_index     = -2;
    m_cached_hit_page_item = nullptr;

    if (m_config.scrollbars.search_hits)
        renderSearchHitsInScrollbar();

    emit searchCountChanged(static_cast<int>(m_search_hit_flat_refs.size()));

    if (m_config.search.absolute_jump)
        GotoHit(m_search_index);
    else
        GotoHit(getClosestHitIndex());

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnSearchFinished);
#endif
}

void
DocumentView::handlePartialSearchResults(
    const QMap<int, std::vector<Model::SearchHit>> &results) noexcept
{
    // Late partials from a superseded search would repopulate m_search_hits
    // after the user cancelled or launched a new query — drop them.
    if (m_search_dispatched_gen != m_search_gen)
        return;

    QMap<int, std::vector<Model::SearchHit>> filtered = results;
    filterHitsToNarrow(filtered);

    // Merge new batch into existing results — don't replace
    for (auto it = filtered.cbegin(); it != filtered.cend(); ++it)
        m_search_hits.insert(it.key(), it.value());

    buildFlatSearchHitIndex();

    emit searchCountChanged(static_cast<int>(m_search_hit_flat_refs.size()));

    if (m_config.scrollbars.search_hits)
        renderSearchHitsInScrollbar();

    // Jump to first hit only on the very first partial result
    if (m_search_index == -1 && !m_search_hit_flat_refs.empty())
    {
        if (m_config.search.absolute_jump)
            GotoHit(0);
        else
            GotoHit(getClosestHitIndex());
    }
}

int
DocumentView::getClosestHitIndex(bool above) noexcept
{
    if (!m_model->supports_text_search() || m_search_hit_flat_refs.empty())
        return -1;

    const int currentPage = m_pageno;
    const int n           = static_cast<int>(m_search_hit_flat_refs.size());

    // If the current hit is on the current page, step to the adjacent hit
    // in the flat list — this handles multiple hits on the same page correctly.
    if (m_search_index >= 0 && m_search_index < n
        && m_search_hit_flat_refs[m_search_index].page == currentPage)
    {
        if (above)
            return (m_search_index - 1 + n) % n;
        else
            return (m_search_index + 1) % n;
    }

    // User has scrolled to a different page — re-anchor by page.
    if (above)
    {
        // Last hit on a page strictly before currentPage
        auto it = std::lower_bound(m_search_hit_flat_refs.begin(),
                                   m_search_hit_flat_refs.end(), currentPage,
                                   [](const HitRef &ref, int page)
        { return ref.page < page; });

        if (it == m_search_hit_flat_refs.begin())
            return n - 1; // wrap to last

        --it;
        return static_cast<int>(
            std::distance(m_search_hit_flat_refs.begin(), it));
    }
    else
    {
        // First hit on a page strictly after currentPage
        auto it = std::upper_bound(m_search_hit_flat_refs.begin(),
                                   m_search_hit_flat_refs.end(), currentPage,
                                   [](int page, const HitRef &ref)
        { return page < ref.page; });

        if (it == m_search_hit_flat_refs.end())
            return 0; // wrap to first

        return static_cast<int>(
            std::distance(m_search_hit_flat_refs.begin(), it));
    }
}

void
DocumentView::buildFlatSearchHitIndex() noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::buildFlatSearchHitIndex(): Building flat index";
#endif
    if (!m_model->supports_text_search())
        return;

    m_search_hit_flat_refs.clear();
    m_search_hit_flat_refs.reserve(m_model->searchMatchesCount());

    for (auto it = m_search_hits.constBegin(); it != m_search_hits.constEnd();
         ++it)
    {
        const int page   = it.key();
        const auto &hits = it.value();

        for (unsigned int i = 0; i < hits.size(); ++i)
            m_search_hit_flat_refs.push_back({page, static_cast<int>(i)});
    }
}

void
DocumentView::handleClickSelection(int clickType, QPointF scenePos) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleClickSelection(): Handling click type"
             << clickType << "at scene position" << scenePos;
#endif

    int pageIndex               = -1;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageIndex, pageItem))
        return;

    // Map to page-local coordinates
    const QPointF pagePos = pageItem->mapFromScene(scenePos);

    if (clickType == 1) // single click → place cursor or snap visual line
    {
        if (hasTextSelection())
        {
            ClearTextSelection();
            return;
        }

        if (m_gview->mode() == GraphicsView::Mode::VisualLine)
        {
            const float scale = m_model->logicalScale();
            const QPointF modelPos(pagePos.x() / scale, pagePos.y() / scale);

            m_visual_lines = m_model->get_text_lines(pageIndex);
            m_visual_line_index
                = m_model->visual_line_index_at_pos(modelPos, m_visual_lines);
            m_pageno = pageIndex;
            snapVisualLine(false);
            return;
        }
    }

    fz_point pdfPos = {float(pagePos.x()), float(pagePos.y())};

    std::vector<QPolygonF> quads;
    switch (clickType)
    {
        case 2: // double click → select word
        {
            quads = m_model->selectWordAt(pageIndex, pdfPos);
        }
        break;

        case 3: // triple click → select line
            quads = m_model->selectLineAt(pageIndex, pdfPos);
            break;

        case 4: // quadruple click → select entire page
            quads = m_model->selectParagraphAt(pageIndex, pdfPos);
            break;

        default:
            return;
    }

    if (quads.empty())
        return;

    QPainterPath totalPath;

    for (const QPolygonF &poly : quads)
    {
        totalPath.addPolygon(poly);
    }
    m_selection_path_item->setPath(totalPath);
    m_selection_path_item->setPos(pageItem->pos());
    m_selection_path_item->setRotation(pageItem->rotation());
    m_selection_path_item->setScale(pageItem->scale());

    // The quads are [bottom-left, bottom-right, top-right, top-left]. The
    // selection runs from the middle of the left edge of the first quad to
    // the middle of the right edge of the last one: a corner lies on the
    // border between two lines, where the text under it is ambiguous and
    // the copied text could come from the neighbouring line.
    const QTransform toScene  = pageItem->sceneTransform();
    const QPolygonF firstQuad = toScene.map(quads.front());
    const QPolygonF lastQuad  = toScene.map(quads.back());

    m_selection_start = (firstQuad[0] + firstQuad[3]) / 2;
    m_selection_end   = (lastQuad[1] + lastQuad[2]) / 2;

    // Update metadata for copying/highlighting
    m_selection_start_page = pageIndex;
    m_selection_end_page   = pageIndex;

    m_selection_path_item->show();
}

void
DocumentView::handleTextHighlightRequested() noexcept
{
    if (!hasTextSelection())
        return;

    const QPointF start = m_selection_start;
    const QPointF end   = m_selection_end;
    const int startP    = m_selection_start_page;
    const int endP      = m_selection_end_page;

    for (int p = startP; p <= endP; ++p)
    {
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        if (!item)
            continue;

        if (p == startP && p == endP)
        {
            m_model->highlight_text_selection(p, item->mapFromScene(start),
                                              item->mapFromScene(end));
        }
        else if (p == startP)
        {
            // From start point to END of page
            m_model->highlight_text_selection(
                p, item->mapFromScene(start),
                QPointF(item->boundingRect().bottomRight()));
        }
        else if (p == endP)
        {
            // From START of page to end point
            m_model->highlight_text_selection(p, QPointF(0, 0),
                                              item->mapFromScene(end));
        }
        else
        {
            // Full page
            m_model->highlight_text_selection(
                p, QPointF(0, 0), QPointF(item->boundingRect().bottomRight()));
        }
    }

    ClearTextSelection();
}

void
DocumentView::handleTextCommentRequested() noexcept
{
    if (!hasTextSelection())
        return;

    bool ok               = false;
    const QString comment = InputDialog::getText(
        tr("Add Comment"), tr("Enter comment for highlighted text:"), "",
        QString(), ok, this);

    if (!ok)
        return;

    const QPointF start = m_selection_start;
    const QPointF end   = m_selection_end;
    const int startP    = m_selection_start_page;
    const int endP      = m_selection_end_page;

    for (int p = startP; p <= endP; ++p)
    {
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        if (!item)
            continue;

        if (p == startP && p == endP)
        {
            m_model->highlight_text_selection(p, item->mapFromScene(start),
                                              item->mapFromScene(end), comment);
        }
        else if (p == startP)
        {
            m_model->highlight_text_selection(
                p, item->mapFromScene(start),
                QPointF(item->boundingRect().bottomRight()), comment);
        }
        else if (p == endP)
        {
            m_model->highlight_text_selection(p, QPointF(0, 0),
                                              item->mapFromScene(end), comment);
        }
        else
        {
            m_model->highlight_text_selection(
                p, QPointF(0, 0), QPointF(item->boundingRect().bottomRight()),
                comment);
        }
    }

    ClearTextSelection();
}

// Handle text selection from GraphicsView
void
DocumentView::handleTextSelection(QPointF start, QPointF end) noexcept
{
    if (!m_model->supports_text_selection())
        return;

    if (start == m_selection_start && end == m_selection_end)
        return;

    int startPage                    = -1;
    int endPage                      = -1;
    GraphicsImageItem *startPageItem = nullptr;
    GraphicsImageItem *endPageItem   = nullptr;

    if (!pageAtScenePos(start, startPage, startPageItem)
        || !pageAtScenePos(end, endPage, endPageItem))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::handleTextSelection(): Handling text selection"
             << "from" << startPage << "to" << endPage;
#endif

    if (startPage > endPage)
    {
        std::swap(startPage, endPage);
        std::swap(start, end);
    }

    QPainterPath totalPath;

    for (int p = startPage; p <= endPage; ++p)
    {
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        if (!item)
            continue;
        QRectF bounds = item->boundingRect();

        // Define logical anchors based on the current visual rotation
        QPointF docStart, docEnd;
        switch (static_cast<int>(m_model->rotation()))
        {
            case 90:
                docStart = QPointF(bounds.width(), 0);  // Top-right
                docEnd   = QPointF(0, bounds.height()); // Bottom-left
                break;
            case 180:
                docStart = QPointF(bounds.width(),
                                   bounds.height()); // Bottom-right
                docEnd   = QPointF(0, 0);            // Top-left
                break;
            case 270:
                docStart = QPointF(0, bounds.height()); // Bottom-left
                docEnd   = QPointF(bounds.width(), 0);  // Top-right
                break;
            default:                      // 0
                docStart = QPointF(0, 0); // Top-left
                docEnd   = QPointF(bounds.width(),
                                   bounds.height()); // Bottom-right
                break;
        }

        std::vector<QPolygonF> quads;
        QPointF localStart = item->mapFromScene(start);
        QPointF localEnd   = item->mapFromScene(end);

        if (p == startPage && p == endPage)
        {
            quads = m_model->computeTextSelectionQuad(p, localStart, localEnd);
        }
        else if (p == startPage)
        {
            // From click point to the end of the document flow
            quads = m_model->computeTextSelectionQuad(p, localStart, docEnd);
        }
        else if (p == endPage)
        {
            // From the start of the document flow to the current cursor
            quads = m_model->computeTextSelectionQuad(p, docStart, localEnd);
        }
        else
        {
            // Full page selection
            quads = m_model->computeTextSelectionQuad(p, docStart, docEnd);
        }

        // const QTransform toScene = item->sceneTransform();
        // for (const QPolygonF &poly : quads)
        // {
        //     totalPath.addPolygon(toScene.map(poly));
        // }
        QTransform toPrimary = item->sceneTransform()
                               * startPageItem->sceneTransform().inverted();
        for (const QPolygonF &poly : quads)
        {
            totalPath.addPolygon(toPrimary.map(poly));
        }
    }

    m_selection_path_item->setPath(totalPath);
    m_selection_path_item->setPos(startPageItem->pos());
    m_selection_path_item->setRotation(startPageItem->rotation());
    m_selection_path_item->setScale(startPageItem->scale());

    m_selection_start = start;
    m_selection_end   = end;

    // Update metadata for copying/highlighting
    m_selection_start_page = startPage;
    m_selection_end_page   = endPage;

    m_selection_path_item->show();

    if (m_config.selection.copy_on_select)
        YankSelection();

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnTextSelected);
#endif
}

// Cycle to the next selection mode
void
DocumentView::NextSelectionMode() noexcept
{
    GraphicsView::Mode nextMode = m_gview->getNextMode();

    if (nextMode == GraphicsView::Mode::VisualLine)
    {
        set_visual_line_mode(true);
    }
    else
    {
        if (m_visual_line_mode)
            set_visual_line_mode(false);
        m_gview->setMode(nextMode);
    }
    emit selectionModeChanged(nextMode);
}

void
DocumentView::clearSearchHits() noexcept
{
#ifndef NDEBUG
    qDebug()
        << "DocumentView::clearSearchHits(): Clearing previous search hits";
#endif
    // Supersede any in-flight search: results whose dispatched-gen does not
    // match this new value will be dropped by the handlers below.
    ++m_search_gen;
    for (auto *item : m_search_items)
    {
        if (item && item->scene() == m_gscene)
            item->setPath(QPainterPath()); // clear instead of delete
    }
    m_search_index         = -1;
    m_cached_hit_index     = -2;
    m_cached_hit_page_item = nullptr;
    m_search_items.clear();
    m_search_hits.clear();
    m_search_hit_flat_refs.clear();

    m_hscroll->setSearchMarkers({});
    m_vscroll->setSearchMarkers({});
    m_gview->setScrollbarsPinned(false);
}

// When a narrow region is active, discard all hits that don't fall inside
// it. The narrow rect is stored as fractions of the page item bounding
// rect; hit quads are in fz coordinates and become page-item-local when
// multiplied by logicalScale(). Pages other than the narrow page are
// dropped entirely.
void
DocumentView::filterHitsToNarrow(
    QMap<int, std::vector<Model::SearchHit>> &results) const noexcept
{
    if (!m_is_narrow || m_narrow_page < 0)
        return;

    const int endPage
        = (m_narrow_page_end < 0) ? m_narrow_page : m_narrow_page_end;
    const auto scale = m_model->logicalScale();

    QMap<int, std::vector<Model::SearchHit>> out;
    for (int p = m_narrow_page; p <= endPage; ++p)
    {
        const auto it = results.constFind(p);
        if (it == results.constEnd())
            continue;

        const auto *pageItem = m_page_items_hash.value(p, nullptr);
        if (!pageItem)
            continue;
        const QSizeF sz = pageItem->boundingRect().size();
        if (sz.isEmpty())
            continue;

        const QRectF narrowLocal(m_narrow_local_normalized.left() * sz.width(),
                                 m_narrow_local_normalized.top() * sz.height(),
                                 m_narrow_local_normalized.width() * sz.width(),
                                 m_narrow_local_normalized.height()
                                     * sz.height());

        std::vector<Model::SearchHit> kept;
        kept.reserve(it.value().size());
        for (const auto &hit : it.value())
        {
            const double cx = (hit.quad.ul.x + hit.quad.lr.x) * 0.5 * scale;
            const double cy = (hit.quad.ul.y + hit.quad.lr.y) * 0.5 * scale;
            if (narrowLocal.contains(cx, cy))
                kept.push_back(hit);
        }
        if (!kept.empty())
            out.insert(p, std::move(kept));
    }
    results = std::move(out);
}

void
DocumentView::SearchCancel() noexcept
{
    if (!m_model->supports_text_search())
        return;

    m_model->searchCancel();
    emit searchBarSpinnerShow(false);
    clearSearchHits();
    renderSearchHitsInScrollbar();
    emit searchCountChanged(-1);
    emit searchIndexChanged(-1);

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnSearchCancelled);
#endif
}

void
DocumentView::Search(const QString &term, bool useRegex) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::SearchFromHere(): Searching for term:" << term;
#endif
    if (!m_model->supports_text_search())
        return;

    if (!m_model->hasTextLayer())
    {
        QMessageBox::information(
            this, tr("Search"),
            tr("This document has no text layer, so it can't be searched."));
        return;
    }

    clearSearchHits();
    if (term.isEmpty())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        emit searchClearRequested();
        return;
    }

    // Check if term has atleast one uppercase letter
    bool caseSensitive = std::any_of(term.cbegin(), term.cend(),
                                     [](QChar c) { return c.isUpper(); });

    emit searchBarSpinnerShow(true);

    // Narrow region takes precedence over directional scope: restrict to
    // the single narrow page (handleSearchResults further trims hits by
    // the narrow rect). Otherwise honor m_search_scope for search_below
    // (from current page forward) and search_above (up to current page).
    int pageFrom = 0;
    int pageTo   = -1;
    if (m_is_narrow)
    {
        pageFrom = m_narrow_page;
        pageTo   = (m_narrow_page_end < 0) ? m_narrow_page : m_narrow_page_end;
    }
    else if (m_search_scope == SearchScope::Below)
    {
        pageFrom = m_pageno;
    }
    else if (m_search_scope == SearchScope::Above)
    {
        pageTo = m_pageno;
    }
    // Snapshot the search generation just before dispatching so result
    // handlers can distinguish this search from any that supersedes it.
    m_search_dispatched_gen = m_search_gen;
    m_model->search(term, caseSensitive, pageFrom, useRegex, pageTo);

    // One-shot: revert to full-document scope after the search is dispatched.
    m_search_scope = SearchScope::All;

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnSearchStarted);
#endif
}

void
DocumentView::SearchInPage(const int pageno, const QString &term) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::SearchInPage(): Searching page: " << pageno
             << " for term: " << term;
#endif
    if (!m_model->supports_text_search())
        return;

    if (!m_model->hasTextLayer())
    {
        QMessageBox::information(
            this, tr("Search"),
            tr("This document has no text layer, so it can't be searched."));
        return;
    }

    clearSearchHits();
    if (term.isEmpty())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    emit searchBarSpinnerShow(true);
    // Check if term has atleast one uppercase letter
    bool caseSensitive = std::any_of(term.cbegin(), term.cend(),
                                     [](QChar c) { return c.isUpper(); });

    // m_search_hits = m_model->search(term);
    m_search_dispatched_gen = m_search_gen;
    m_model->searchInPage(pageno, term, caseSensitive);
#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnSearchStarted);
#endif
}

void
DocumentView::NextHit() noexcept
{
    if (m_config.search.absolute_jump)
    {
        const int n    = static_cast<int>(m_search_hit_flat_refs.size());
        m_search_index = (m_search_index + 1) % n;
        GotoHit(m_search_index);
    }
    else
        GotoHit(getClosestHitIndex(false));
}

void
DocumentView::PrevHit() noexcept
{
    if (m_config.search.absolute_jump)
    {
        const int n    = static_cast<int>(m_search_hit_flat_refs.size());
        m_search_index = (m_search_index - 1 + n) % n;
        GotoHit(m_search_index);
    }
    else
        GotoHit(getClosestHitIndex(true));
}

// Navigate to a specific search hit by index
void
DocumentView::GotoHit(int index) noexcept
{
    if (m_search_hit_flat_refs.empty())
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::GotoHit(): Going to search hit index:" << index;
#endif

    if (index < 0)
        index = static_cast<int>(m_search_hit_flat_refs.size()) - 1;
    else if (index >= static_cast<int>(m_search_hit_flat_refs.size()))
        index = 0;

    const HitRef ref       = m_search_hit_flat_refs[index];
    m_search_index         = index;
    m_cached_hit_index     = -2;
    m_cached_hit_page_item = nullptr;
    m_pageno               = ref.page;
    const auto &hit        = m_search_hits[ref.page][ref.indexInPage];
    const float scale      = m_model->logicalScale();

    emit searchIndexChanged(index);
    emit currentPageChanged(ref.page + 1);

    // Compute hit centre in scene coordinates directly from cached offsets.
    const double hitX = (hit.quad.ul.x + hit.quad.ur.x) * scale / 2.0;
    const double hitY = (hit.quad.ul.y + hit.quad.ll.y) * scale / 2.0;

    QPointF scenePos;
    if (m_layout_mode == LayoutMode::HORIZONTAL)
    {
        scenePos
            = QPointF(pageOffset(ref.page) + hitX,
                      pageXOffset(ref.page, pageSceneSize(ref.page).width(),
                                  m_gview->sceneRect().width())
                          + hitY);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        const QRectF sr = m_gview->sceneRect();
        // Ensure the hit is calculated relative to the centered page
        // position
        scenePos        = QPointF(
            sr.left() + (sr.width() - pageSceneSize(ref.page).width()) / 2.0
                + hitX,
            sr.top() + (sr.height() - pageSceneSize(ref.page).height()) / 2.0
                + hitY);
    }
    // else if (m_layout_mode == LayoutMode::SINGLE)
    // {
    //     const QRectF sr = m_gview->sceneRect();
    //     scenePos        = QPointF(
    //         sr.x() + (sr.width() - pageSceneSize(ref.page).width()) / 2.0
    //             + hitX,
    //         sr.y() + (sr.height() - pageSceneSize(ref.page).height())
    //         / 2.0
    //             + hitY);
    // }
    else // TOP_TO_BOTTOM, BOOK
    {
        scenePos
            = QPointF(pageXOffset(ref.page, pageSceneSize(ref.page).width(),
                                  m_gview->sceneRect().width())
                          + hitX,
                      pageOffset(ref.page) + hitY);
    }

    m_scroll_to_hit_pending = true;
    m_scroll_page_update_timer->stop();
    m_hq_render_timer->stop();

    m_gview->centerOn(scenePos);

    // If the page is already rendered, the render callback won't reliably
    // fire for this hit — update the highlight immediately.
    if (m_page_items_hash.contains(ref.page)
        && !m_placeholder_pages.contains(ref.page))
    {
        m_scroll_to_hit_pending = false;
        updateCurrentHitHighlight();
    }

    if (m_layout_mode == LayoutMode::SINGLE)
        renderPage();
    else
        renderPages();
}

bool
DocumentView::SelectTextRange(const PageLocation &from,
                              const PageLocation &to) noexcept
{
    if (!m_model->supports_text_selection())
        return false;

    GraphicsImageItem *a = m_page_items_hash.value(from.pageno, nullptr);
    GraphicsImageItem *b = m_page_items_hash.value(to.pageno, nullptr);
    if (!a || !b || m_placeholder_pages.contains(from.pageno)
        || m_placeholder_pages.contains(to.pageno))
        return false;

    const QPointF start
        = a->mapToScene(m_model->toPixelSpace(from.pageno, {from.x, from.y}));
    const QPointF end
        = b->mapToScene(m_model->toPixelSpace(to.pageno, {to.x, to.y}));

    // handleTextSelection ignores an unchanged selection; clear first so
    // re-selecting the same range after a clear still takes effect.
    ClearTextSelection();
    handleTextSelection(start, end);
    return hasTextSelection();
}

// Toggle text highlight mode
void
DocumentView::ToggleTextHighlight() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::TextHighlight)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::TextHighlight;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

void
DocumentView::ToggleTextSelection() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::TextSelection)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::TextSelection;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle region selection mode
void
DocumentView::ToggleRegionSelect() noexcept
{
    const auto newMode
        = (m_gview->mode() == GraphicsView::Mode::RegionSelection)
              ? m_gview->getDefaultMode()
              : GraphicsView::Mode::RegionSelection;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle annotation rectangle mode
void
DocumentView::ToggleAnnotRect() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::AnnotRect)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::AnnotRect;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle annotation selection mode
void
DocumentView::ToggleAnnotSelect() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::AnnotSelect)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::AnnotSelect;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle annotation popup mode
void
DocumentView::ToggleAnnotPopup() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::AnnotPopup)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::AnnotPopup;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Clear the current text selection
void
DocumentView::ClearTextSelection() noexcept
{
    if (!hasTextSelection())
        return;

#ifndef NDEBUG
    qDebug() << "ClearTextSelection(): Clearing text selection";
#endif

    if (m_selection_path_item)
    {
        m_selection_path_item->setPath(QPainterPath());
        m_selection_path_item->hide();
    }

    m_last_selection_start = m_selection_start;
    m_last_selection_end   = m_selection_end;

    m_selection_start = QPointF();
    m_selection_end   = QPointF();

    m_selection_start_page = -1;
    m_selection_end_page   = -1;
}

// Yank the current text selection to clipboard
void
DocumentView::YankSelection(bool formatted) noexcept
{
    if (!hasTextSelection())
        return;

    QString fullText;

    // Copy the state so we can normalize it safely
    QPointF start = m_selection_start;
    QPointF end   = m_selection_end;
    int startP    = m_selection_start_page;
    int endP      = m_selection_end_page;

    for (int p = startP; p <= endP; ++p)
    {
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        assert(item && "Page is not yet in the hash map");

        QString text;
        if (p == startP && p == endP)
        {
            text = m_model->get_selected_text(p, item->mapFromScene(start),
                                              item->mapFromScene(end),
                                              formatted);
        }
        else if (p == startP)
        {
            // From start point to END of page
            text = m_model->get_selected_text(
                p, item->mapFromScene(start),
                QPointF(item->boundingRect().bottomRight()), formatted);
        }
        else if (p == endP)
        {
            // From START of page to end point
            text = m_model->get_selected_text(
                p, QPointF(0, 0), item->mapFromScene(end), formatted);
        }
        else
        {
            // Full page
            text = m_model->get_selected_text(
                p, QPointF(0, 0), QPointF(item->boundingRect().bottomRight()),
                formatted);
        }

        fullText += text;

        // Add a newline between pages to prevent text merging
        if (p < endP && !text.isEmpty())
            fullText += "\n";
    }

    QGuiApplication::clipboard()->setText(fullText);
}

void
DocumentView::handleContextMenuRequested(const QPoint &globalPos,
                                         bool *handled) noexcept
{

#ifndef NDEBUG
    qDebug() << "DocumentView::handleContextMenuRequested(): Context menu "
             << "requested at global position:" << globalPos;
#endif
    QMenu *menu    = new QMenu(this);
    auto addAction = [this, &menu](const QString &text, const auto &slot)
    {
        QAction *action = new QAction(text, menu); // sets parent = menu
        connect(action, &QAction::triggered, this, slot);
        menu->addAction(action);
    };

    const bool selectionActive
        = m_selection_path_item && !m_selection_path_item->path().isEmpty();
    const bool annotModeActive
        = m_gview->mode() == GraphicsView::Mode::AnnotSelect
          || m_gview->mode() == GraphicsView::Mode::AnnotPopup;
    SelectedAnnotations selectedAnnots
        = annotModeActive && m_model->supports_annotations()
              ? getSelectedAnnotations()
              : SelectedAnnotations{};

    const bool hasAnnots = !selectedAnnots.empty();
    bool hasActions      = false;

    // if (selectionActive &&
    // m_selection_path_item->path().contains(scenePos))
    //     emit textSelectionRightClickRequested(globalPos, scenePos);

    if (selectionActive)
    {
        addAction(tr("Copy Text"), [this]() { YankSelection(true); });
        addAction(tr("Copy Unformatted Text"),
                  [this]() { YankSelection(false); });
        if (m_model->supports_annotations())
        {
            addAction(tr("Comment"), &DocumentView::handleTextCommentRequested);
            addAction(tr("Highlight Text"),
                      &DocumentView::handleTextHighlightRequested);
        }
#ifdef WITH_LUA
        dispatchLuaEvent(DispatchType::OnTextSelectionContextMenuRequested);
        applyLuaContextMenu(ContextMenuType::TextSelection, menu);
#endif
        hasActions = true;
    }

    if (hasAnnots)
    {
        if (hasActions)
            menu->addSeparator();

        // Delete selected annotations
        addAction(tr("Delete Annotations"), [this, selectedAnnots]()
        {
            QHash<int, QSet<int>> objNumsByPage;
            for (const auto &[pageno, annot] : selectedAnnots)
            {
                if (!annot)
                    continue;
                objNumsByPage[pageno].insert(annot->index());
            }

            for (auto it = objNumsByPage.cbegin(); it != objNumsByPage.cend();
                 ++it)
            {
                m_model->undoStack()->push(new DeleteAnnotationsCommand(
                    m_model, it.key(), it.value()));
            }
            // setModified(true);
        });

        // Change color of the selected annotations
        // TODO: Put this under an undo command
        addAction(tr("Change Color"), [this, selectedAnnots]()
        {
            QColor current_color;
            if (selectedAnnots.size() == 1)
            {
                current_color = m_model->getAnnotColor(
                    m_pageno, selectedAnnots.at(0).second->index());
            }

            ColorDialog cp(m_global.misc.color_dialog_colors, current_color,
                           this);

            cp.setWindowTitle(tr("Select Annotation Color"));

            if (cp.exec() == QDialog::Accepted)
            {
                QColor c = cp.selectedColor();
                if (c.isValid())
                {
                    for (const auto &[pageno, annot] : selectedAnnots)
                        m_model->annotChangeColor(pageno, annot->index(), c);
                }
            }
        });
        hasActions = true;
    }

    if (!hasActions)
    {
        delete menu;
        return;
    }

    if (handled)
        *handled = true;

    menu->exec(globalPos);

    menu->deleteLater(); // Schedule menu for deletion after it closes
}

void
DocumentView::updateCurrentHitHighlight() noexcept
{
    if (m_thumbnail_mode)
        return;

    if (m_search_index < 0
        || m_search_index >= static_cast<int>(m_search_hit_flat_refs.size()))
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    const HitRef ref = m_search_hit_flat_refs[m_search_index];

    GraphicsImageItem *pageItem = m_page_items_hash.value(ref.page, nullptr);
    if (!pageItem || !pageItem->scene())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        m_cached_hit_index     = -2;
        m_cached_hit_page_item = nullptr;
        return;
    }

    // Skip recomputation if nothing has changed since the last call.
    // pageItem pointer changes on re-render or zoom (new item created), so this
    // correctly invalidates when the transform may have changed.
    if (m_search_index == m_cached_hit_index
        && pageItem == m_cached_hit_page_item)
        return;

    m_cached_hit_index     = m_search_index;
    m_cached_hit_page_item = pageItem;

    const float scale = m_model->logicalScale();
    const auto &hit   = m_search_hits[ref.page][ref.indexInPage];

    QPolygonF poly;
    poly.reserve(4);
    poly << QPointF(hit.quad.ul.x * scale, hit.quad.ul.y * scale)
         << QPointF(hit.quad.ur.x * scale, hit.quad.ur.y * scale)
         << QPointF(hit.quad.lr.x * scale, hit.quad.lr.y * scale)
         << QPointF(hit.quad.ll.x * scale, hit.quad.ll.y * scale);

    QPainterPath path;
    path.addPolygon(pageItem->sceneTransform().map(poly));

    m_current_search_hit_item->setPath(path);
}

void
DocumentView::scrollToCurrentHit() noexcept
{
    if (m_search_index < 0
        || m_search_index >= static_cast<int>(m_search_hit_flat_refs.size()))
        return;

    const QPainterPath &path = m_current_search_hit_item->path();
    if (path.isEmpty())
        return;

    m_gview->centerOn(path.boundingRect().center());
}

void
DocumentView::renderSearchHitsForPage(int pageno) noexcept
{
    if (!m_config.search.highlight_matches || !m_search_hits.contains(pageno))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::renderSearchHitsForPage(): Rendering search "
             << "hits for page:" << pageno;
#endif

    const auto &hits = m_search_hits.value(pageno); // Local copy

    // Validate the Page Item still exists in the scene
    const GraphicsImageItem *pageItem
        = m_page_items_hash.value(pageno, nullptr);

    if (!pageItem)
        return;

    QGraphicsPathItem *item = ensureSearchItemForPage(pageno);
    if (!item)
        return;

    QPainterPath allPath;

    const QTransform toScene = pageItem->sceneTransform();

    const auto scale = m_model->logicalScale();

    for (unsigned int i = 0; i < hits.size(); ++i)
    {
        const Model::SearchHit &hit = hits[i];
        QPolygonF poly;
        poly.reserve(4);
        poly << QPointF(hit.quad.ul.x * scale, hit.quad.ul.y * scale)
             << QPointF(hit.quad.ur.x * scale, hit.quad.ur.y * scale)
             << QPointF(hit.quad.lr.x * scale, hit.quad.lr.y * scale)
             << QPointF(hit.quad.ll.x * scale, hit.quad.ll.y * scale);

        allPath.addPolygon(toScene.map(poly));
    }

    // Set colors
    item->setPath(allPath);
    item->setBrush(rgbaToQColor(m_config.search.match_color));
}

void
DocumentView::renderSearchHitsInScrollbar() noexcept
{
    if (!m_model->supports_text_search())
        return;

    m_vscroll->setSearchMarkers({});
    m_hscroll->setSearchMarkers({});

    if (m_search_hit_flat_refs.empty())
        return;

    // SINGLE mode has no scrollbar to mark — only one page is ever shown.
    if (m_layout_mode == LayoutMode::SINGLE)
        return;

    // Scene coordinates are logical pixels, so use logicalScale, not
    // physicalScale (which is DPR-multiplied and overshoots on HiDPI).
    const double pdfToSceneScale = m_model->logicalScale();

    std::vector<double> markers;
    markers.reserve(m_search_hit_flat_refs.size());
    if (m_layout_mode == LayoutMode::VERTICAL
        || m_layout_mode == LayoutMode::BOOK
        || m_layout_mode == LayoutMode::GRID)
    {
        for (const auto &hitRef : m_search_hit_flat_refs)
        {
            const auto &hit = m_search_hits[hitRef.page][hitRef.indexInPage];
            markers.push_back(pageOffset(hitRef.page)
                              + hit.quad.ul.y
                                    * pdfToSceneScale); // ← was missing
        }
        m_vscroll->setSearchMarkers(std::move(markers));
    }
    else // LEFT_TO_RIGHT
    {
        for (const auto &hitRef : m_search_hit_flat_refs)
        {
            const auto &hit = m_search_hits[hitRef.page][hitRef.indexInPage];
            markers.push_back(pageOffset(hitRef.page)
                              + hit.quad.ul.x * pdfToSceneScale);
        }
        m_hscroll->setSearchMarkers(std::move(markers));
    }

    if (m_config.scrollbars.search_hits)
        m_gview->setScrollbarsPinned(true);
}

QGraphicsPathItem *
DocumentView::ensureSearchItemForPage(int pageno) noexcept
{
    if (m_model->supports_text_search() && m_search_items.contains(pageno))
        return m_search_items.value(pageno, nullptr);

    auto *item = m_gscene->addPath(QPainterPath());
    item->setBrush(QColor(255, 230, 150, 120));
    item->setPen(Qt::NoPen);
    item->setZValue(ZVALUE_SEARCH_HITS);

    m_search_items[pageno] = item;
    return item;
}

void
DocumentView::ReselectLastTextSelection() noexcept
{
    handleTextSelection(m_last_selection_start, m_last_selection_end);
}

void
DocumentView::CopyTextFromRegion(QRectF area) noexcept
{
    if (!m_model->supports_text_selection())
        return;
    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    const QPointF pageStart = pageItem->mapFromScene(area.topLeft());
    const QPointF pageEnd   = pageItem->mapFromScene(area.bottomRight());

    const std::string text = m_model->getTextInArea(pageno, pageStart, pageEnd);

    QClipboard *clip = QGuiApplication::clipboard();
    clip->setText(QString::fromStdString(text));
}

QImage
DocumentView::regionImage(QRectF area) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return {};

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return {};

    return pageItem->imageRegion(pixelRect);
}

QImage
DocumentView::imageAt(QPointF scenePos) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(scenePos, pageno, pageItem) || !pageItem)
        return {};

    const QPointF logicalPt = pageItem->mapFromScene(scenePos);
    return m_model->imageAt(pageno, logicalPt).image;
}

void
DocumentView::CopyRegionAsImage(QRectF area) noexcept
{
    const QImage img = regionImage(area);

    if (!img.isNull())
    {
        QClipboard *clip = QGuiApplication::clipboard();
        clip->setImage(img);
    }
}

void
DocumentView::CopyRegionAsImageAtDPI(QRectF area) noexcept
{
    bool ok = false;
    int targetDPI
        = QInputDialog::getInt(this, tr("Copy Region at Custom DPI"),
                               tr("Render DPI:"), 300, 72, 1200, 72, &ok);
    if (!ok)
        return;

    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF logicalRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, logicalRect, pixelRect))
        return;

    // Pass the logical rect — buildPageTransforms uses logicalScale() so
    // dev_to_page maps logical pixels (item-local coords / DPR) → PDF pts.
    QImage img
        = m_model->renderRegionAtDPI(pageno, logicalRect, float(targetDPI));

    if (img.isNull())
    {
        // Raster / DjVu fallback: crop the existing render (physical pixels)
        // and upscale.
        img = pageItem->imageRegion(pixelRect);
        if (!img.isNull())
        {
            const float currentScale
                = float(m_model->zoom()) * float(m_model->DPI());
            const float scale = float(targetDPI) / currentScale;
            if (scale > 1.0f)
                img = img.scaled(
                    qRound(img.width() * scale), qRound(img.height() * scale),
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }

    if (!img.isNull())
        QGuiApplication::clipboard()->setImage(img);
}

void
DocumentView::SaveRegionAsImage(QRectF area) noexcept
{
    int pageno                  = -1;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;

    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;

    const QImage img = pageItem->imageRegion(pixelRect);
    if (img.isNull())
        return;

    QFileDialog fd(this);

    const QString fileName = fd.getSaveFileName(
        this, tr("Save Image"), "",
        tr("PNG Image") + " (*.png), " + tr("JPEG Image") + " (*.jpg *.jpeg), "
            + tr("BMP Image") + " (*.bmp);; All Files (*)");
    if (fileName.isEmpty())
        return;

    QString format;

    if (fileName.endsWith(".png", Qt::CaseInsensitive))
        format = "PNG";

    else if (fileName.endsWith(".jpg", Qt::CaseInsensitive)
             || fileName.endsWith(".jpeg", Qt::CaseInsensitive))
        format = "JPEG";

    else if (fileName.endsWith(".bmp", Qt::CaseInsensitive))
        format = "BMP";

    else
        format = "PNG";

    img.save(fileName, format.toLatin1().constData());
}

void
DocumentView::OpenRegionInViewer(QRectF area, bool withDefaultViewer) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;

    QImage img = pageItem->imageRegion(pixelRect);
    if (img.isNull())
        return;

    // Save to a temporary file
    auto *tempFile
        = new QTemporaryFile(QDir::tempPath() + "/lektra_XXXXXX.png", this);
    tempFile->setAutoRemove(false);
    if (!tempFile->open())
    {
        delete tempFile;
        return;
    }

    if (!img.save(tempFile, "PNG"))
    {
        delete tempFile;
        return;
    }
    tempFile->close();

    if (withDefaultViewer)
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempFile->fileName()));
    else
        emit openFileInNewTabRequested(tempFile->fileName(), {});
}

void
DocumentView::startRegionSelect(std::function<void(QRectF)> cb) noexcept
{
    m_region_select_cb = std::move(cb);
    m_gview->setMode(GraphicsView::Mode::RegionSelection);
}

void
DocumentView::handleRegionSelectRequested(QRectF area) noexcept
{
    if (m_region_select_cb)
    {
        auto cb            = std::move(m_region_select_cb);
        m_region_select_cb = nullptr;
        m_gview->clearRubberBand();
        cb(area);
        return;
    }

    QMenu *menu = new QMenu(this);
    connect(menu, &QMenu::aboutToHide, this, [this, menu]()
    {
        m_gview->clearRubberBand();
        menu->deleteLater();
    });

    menu->addAction(tr("Narrow to Region"),
                    [this, area]() { applyNarrow(area); });
    menu->addAction(tr("Zoom to Selection"),
                    [this, area]() { ZoomToRegion(area); });
    menu->addSeparator();
    menu->addAction(tr("Copy Region as Image"),
                    [this, area]() { CopyRegionAsImage(area); });
    // Re-rendering at a custom DPI only makes sense for vector/text-based
    // formats (PDF, EPUB, XPS…); for raster images it would just upscale
    // pixels.
    if (!m_model->isImage())
        menu->addAction(tr("Copy Region as Image (Custom DPI)..."),
                        [this, area]() { CopyRegionAsImageAtDPI(area); });
    menu->addAction(tr("Save Region as Image"),
                    [this, area]() { SaveRegionAsImage(area); });
    menu->addAction(tr("Open Region in new tab"),
                    [this, area]() { OpenRegionInViewer(area); });
    menu->addAction(tr("Open Region with Default Viewer"),
                    [this, area]() { OpenRegionInViewer(area, true); });
    if (m_model->supports_text_selection())
        menu->addAction(tr("Copy Text from Region"),
                        [this, area]() { CopyTextFromRegion(area); });

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnRegionSelectionContextMenuRequested);
    applyLuaContextMenu(ContextMenuType::RegionSelection, menu);
#endif

    menu->popup(QCursor::pos());
}

// Copy current page as image to clipboard
void
DocumentView::Copy_page_image() noexcept
{
    if (!m_model)
        return;

    int pageno                  = -1;
    GraphicsImageItem *pageItem = nullptr;

    const QPointF sceneCenter = m_gview->mapToScene(
        m_gview->viewport()->width() / 2, m_gview->viewport()->height() / 2);

    if (!pageAtScenePos(sceneCenter, pageno, pageItem))
        return;

    const QImage img = pageItem->imageRegion(
        QRect(0, 0, pageItem->width(), pageItem->height()));

    if (!img.isNull())
    {
        QClipboard *clip = QGuiApplication::clipboard();
        clip->setImage(img);
    }
}

QString
DocumentView::selectionText(bool formatted,
                            std::string separator) const noexcept
{
    // Copy the state so we can normalize it safely
    QPointF start = m_selection_start;
    QPointF end   = m_selection_end;
    int startP    = m_selection_start_page;
    int endP      = m_selection_end_page;
    QString fullText;

    // No selection: both pages are -1. Looping from -1 used to look up a page
    // item that doesn't exist and dereference it.
    if (startP < 0 || endP < startP)
        return fullText;

    for (int p = startP; p <= endP; ++p)
    {
        // A page of a long selection can be out of the loaded range (its
        // item was dropped while scrolling); there is no geometry to map
        // the selection through, so it contributes no text.
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        if (!item)
            continue;

        QString text;
        if (p == startP && p == endP)
        {
            text = m_model->get_selected_text(p, item->mapFromScene(start),
                                              item->mapFromScene(end),
                                              formatted);
        }
        else if (p == startP)
        {
            // From start point to END of page
            text = m_model->get_selected_text(
                p, item->mapFromScene(start),
                QPointF(item->boundingRect().bottomRight()), formatted);
        }
        else if (p == endP)
        {
            // From START of page to end point
            text = m_model->get_selected_text(
                p, QPointF(0, 0), item->mapFromScene(end), formatted);
        }
        else
        {
            // Full page
            text = m_model->get_selected_text(
                p, QPointF(0, 0), QPointF(item->boundingRect().bottomRight()),
                formatted);
        }

        fullText += text;

        if (p < endP && !text.isEmpty())
            fullText += QString::fromStdString(separator);
    }

    return fullText;
}
