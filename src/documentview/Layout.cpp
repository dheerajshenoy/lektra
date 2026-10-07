#include "DocumentView.hpp"

#include <QMovie>
#include <QScopeGuard>

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

static constexpr int HSCROLL_STEP = 50;
static constexpr int VSCROLL_STEP = 50;
namespace
{

bool
locationsEqual(const PageLocation &a, const PageLocation &b) noexcept
{
    return a.pageno == b.pageno && a.x == b.x && a.y == b.y;
}
} // namespace

void
DocumentView::setLayoutMode(const LayoutMode &mode) noexcept
{
    if (m_layout_mode == mode)
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::setLayoutMode(): Changing layout mode to"
             << static_cast<int>(mode);
#endif

    m_layout_mode = mode;
    initConnections();
    invalidateVisiblePagesCache();

    if (m_model->numPages() == 0)
        return;

    clearDocumentItems();
    cachePageStride();
    updateSceneRect();

    // Ensure we are in valid page number
    m_pageno = std::clamp(m_pageno, 0, m_model->numPages() - 1);

    GotoPage(m_pageno);
}

// Rotate page clockwise
void
DocumentView::RotateClock() noexcept
{
    remapNarrowForRotation(true);
    m_model->rotateClock();
    rotateHelper();
}

// Rotate page anticlockwise
void
DocumentView::RotateAnticlock() noexcept
{
    remapNarrowForRotation(false);
    m_model->rotateAnticlock();
    rotateHelper();
}

void
DocumentView::rotateHelper() noexcept
{
    // told on every way out, including the early returns
    const auto notify
        = qScopeGuard([this] { emit rotationChanged(m_model->rotation()); });

    if (m_model->isImage())
    {
        // stopGifPlayback();
        renderImage();
        return;
    }
    cachePageStride();
    const std::set<int> &trackedPages = getVisiblePages();

    if (trackedPages.empty())
        return;

    for (int pageno : trackedPages)
    {
        // m_model->invalidatePageCache(pageno);
        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);
    }

    renderPages();
    GotoPage(m_pageno);
}

// Toggles cropping every rendered page to its tight content bounding box
// (Model::contentBBox), hiding blank margins entirely. Changing this changes
// every page's effective pixel dimensions, exactly like a rotation change —
// same "re-layout + re-render everything visible" sequence as rotateHelper()
// above (not calling it by that name here since "rotateHelper" would be
// misleading for a non-rotation toggle).
void
DocumentView::ToggleTrimMargins() noexcept
{
    m_trim_margins = !m_trim_margins;

    if (m_model->isImage())
    {
        renderImage();
        return;
    }

    cachePageStride();
    const std::set<int> &trackedPages = getVisiblePages();

    if (trackedPages.empty())
        return;

    for (int pageno : trackedPages)
    {
        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);
    }

    renderPages();
    GotoPage(m_pageno);
}

void
DocumentView::FlipH() noexcept
{
    if (m_is_narrow)
    {
        const double l            = m_narrow_local_normalized.left();
        const double t            = m_narrow_local_normalized.top();
        const double w            = m_narrow_local_normalized.width();
        const double h            = m_narrow_local_normalized.height();
        m_narrow_local_normalized = QRectF(1.0 - l - w, t, w, h);
    }
    m_model->toggleFlipH();
    rotateHelper();
}

void
DocumentView::FlipV() noexcept
{
    if (m_is_narrow)
    {
        const double l            = m_narrow_local_normalized.left();
        const double t            = m_narrow_local_normalized.top();
        const double w            = m_narrow_local_normalized.width();
        const double h            = m_narrow_local_normalized.height();
        m_narrow_local_normalized = QRectF(l, 1.0 - t - h, w, h);
    }
    m_model->toggleFlipV();
    rotateHelper();
}

// Cycle to the next fit mode. Only cycles through the "basic" modes
// (Width, Height, Window) — WidthSmart / HeightSmart are opt-in and would
// surprise a user who is just toggling.
void
DocumentView::NextFitMode() noexcept
{
    static constexpr int kNumBasicFitModes
        = static_cast<int>(FitMode::WidthSmart);
    int cur = static_cast<int>(m_fit_mode);
    if (cur < 0 || cur >= kNumBasicFitModes)
        cur = -1; // Start the cycle at Width when leaving a smart mode
    FitMode nextMode = static_cast<FitMode>((cur + 1) % kNumBasicFitModes);
    m_fit_mode       = nextMode;
    setFitMode(nextMode);
    fitModeChanged(nextMode);
}

// Set the fit mode and adjust zoom accordingly
void
DocumentView::setFitMode(FitMode mode) noexcept
{
#ifndef NDEBUG
    qDebug() << "setFitMode(): Setting fit mode to:" << static_cast<int>(mode);
#endif

    const auto notify
        = qScopeGuard([this] { emit fitModeChanged(m_fit_mode); });

    m_fit_mode = mode;

    double bboxW, bboxH;

    if (m_model->isImage())
    {
        GraphicsImageItem *imageItem = m_page_items_hash.value(0, nullptr);
        if (imageItem && !imageItem->image().isNull() && m_current_zoom > 0.0)
        {
            const QRectF sceneBBox = imageItem->sceneBoundingRect();
            bboxW                  = sceneBBox.width() / m_current_zoom;
            bboxH                  = sceneBBox.height() / m_current_zoom;
        }
        else
        {
            const auto imageDim = m_model->page_dimension_pts(0);
            const double baseW  = imageDim.width_pts;
            const double baseH  = imageDim.height_pts;
            double rot          = static_cast<double>(m_model->rotation());
            rot                 = std::fmod(rot, 360.0);
            if (rot < 0)
                rot += 360.0;

            const double t = deg2rad(rot);
            const double c = std::abs(std::cos(t));
            const double s = std::abs(std::sin(t));
            bboxW          = baseW * c + baseH * s;
            bboxH          = baseW * s + baseH * c;
        }
    }
    else
    {
        const auto pageDim = m_model->page_dimension_pts(m_pageno);

        const double baseW = (pageDim.width_pts / 72.0) * m_model->DPI();
        const double baseH = (pageDim.height_pts / 72.0) * m_model->DPI();
        double rot         = static_cast<double>(m_model->rotation());
        rot                = std::fmod(rot, 360.0);
        if (rot < 0)
            rot += 360.0;

        const double t = deg2rad(rot);
        const double c = std::abs(std::cos(t));
        const double s = std::abs(std::sin(t));
        bboxW          = baseW * c + baseH * s;
        bboxH          = baseW * s + baseH * c;

        if (mode == FitMode::Width && m_layout_mode == LayoutMode::BOOK)
        {
            int leftP  = (m_pageno == 0)
                             ? 0
                             : ((m_pageno % 2 != 0) ? m_pageno : m_pageno - 1);
            int rightP = (m_pageno == 0) ? -1 : leftP + 1;

            auto getW = [&](int p)
            {
                if (p < 0 || p >= m_model->numPages())
                    return 0.0;
                const auto dim = m_model->page_dimension_pts(p);
                return ((dim.width_pts / 72.0) * m_model->DPI()) * c
                       + ((dim.height_pts / 72.0) * m_model->DPI()) * s;
            };

            bboxW = getW(leftP) + getW(rightP);
            if (m_pageno == 0)
                bboxW *= 2.0; // Force cover zoom to respect the logical spine
                              // center
        }
    }

    // For smart fit modes, replace the raw page bbox with the tight
    // content bbox so blank margins don't consume the fit budget. Cache
    // the fractional content bbox so we can also centre the viewport on
    // the content after zooming, regardless of rotation.
    Model::ContentBBox contentPtBox{};
    bool haveContentBox = false;
    if ((mode == FitMode::WidthSmart || mode == FitMode::HeightSmart)
        && !m_model->isImage())
    {
        contentPtBox   = m_model->contentBBox(m_pageno);
        const auto dim = m_model->page_dimension_pts(m_pageno);
        // Only meaningful if the content really is smaller than the page —
        // otherwise fall back to the regular fit calculation.
        const bool tighter
            = (contentPtBox.width() > 0.0f
               && contentPtBox.width() < dim.width_pts * 0.995f)
              || (contentPtBox.height() > 0.0f
                  && contentPtBox.height() < dim.height_pts * 0.995f);
        if (tighter)
        {
            const double cbaseW
                = (contentPtBox.width() / 72.0) * m_model->DPI();
            const double cbaseH
                = (contentPtBox.height() / 72.0) * m_model->DPI();
            double rot = static_cast<double>(m_model->rotation());
            rot        = std::fmod(rot, 360.0);
            if (rot < 0)
                rot += 360.0;
            const double t = deg2rad(rot);
            const double c = std::abs(std::cos(t));
            const double s = std::abs(std::sin(t));
            bboxW          = cbaseW * c + cbaseH * s;
            bboxH          = cbaseW * s + cbaseH * c;
            haveContentBox = true;
        }
    }

    double newZoom = m_current_zoom;
    switch (mode)
    {
        case FitMode::Width:
        case FitMode::WidthSmart:
        {
            const int viewWidth  = m_gview->viewport()->width();
            const int viewHeight = m_gview->viewport()->height();
            double z             = static_cast<double>(viewWidth) / bboxW;
            // If this zoom would force a vertical scrollbar, account for its
            // width so the calculation is stable across repeated presses.
            if (bboxH * z > viewHeight)
                z = static_cast<double>(viewWidth - m_config.scrollbars.size)
                    / bboxW;
            newZoom = z;
        }
        break;

        case FitMode::Height:
        case FitMode::HeightSmart:
        {
            const int viewWidth  = m_gview->viewport()->width();
            const int viewHeight = m_gview->viewport()->height();
            double z             = static_cast<double>(viewHeight) / bboxH;
            if (bboxW * z > viewWidth)
                z = static_cast<double>(viewHeight - m_config.scrollbars.size)
                    / bboxH;
            newZoom = z;
        }
        break;

        case FitMode::Window:
        {
            const int viewWidth  = m_gview->viewport()->width();
            const int viewHeight = m_gview->viewport()->height();

            const double zoomX = static_cast<double>(viewWidth) / bboxW;
            const double zoomY = static_cast<double>(viewHeight) / bboxH;

            newZoom = std::min(zoomX, zoomY);
        }
        break;

        default:
            break;
    }

    setZoom(newZoom, false);

    // For smart fits, scroll so the content bbox is centred in the viewport;
    // margins fall off-screen (or into scroll-past space) instead of sitting
    // at the top-left of the view. mapToScene() handles rotation for us.
    if (haveContentBox)
    {
        if (auto *pageItem = m_page_items_hash.value(m_pageno, nullptr))
        {
            const auto dim         = m_model->page_dimension_pts(m_pageno);
            const QRectF localRect = pageItem->boundingRect();
            const double pw = std::max(1.0, static_cast<double>(dim.width_pts));
            const double ph
                = std::max(1.0, static_cast<double>(dim.height_pts));
            const double fx = 0.5 * (contentPtBox.x0 + contentPtBox.x1) / pw;
            const double fy = 0.5 * (contentPtBox.y0 + contentPtBox.y1) / ph;
            const QPointF localCenter(localRect.x() + fx * localRect.width(),
                                      localRect.y() + fy * localRect.height());
            m_gview->centerOn(pageItem->mapToScene(localCenter));
        }
    }
}

// Set zoom factor directly
void
DocumentView::setZoom(double factor, bool restoreLocation) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::setZoom(): Setting zoom to factor:" << factor;
#endif

    factor = std::clamp(factor, MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);

    if (restoreLocation)
    {
        // Use anchored zoom with viewport center as anchor
        const QPointF viewCenter
            = m_gview->mapToScene(m_gview->viewport()->width() / 2,
                                  m_gview->viewport()->height() / 2);
        setZoomAnchored(factor, viewCenter);
    }
    else
    {
        m_current_zoom = factor;
        if (!m_model->isImage())
            invalidateVisiblePagesCache();
        zoomHelper(PageLocation{-1, 0, 0});
    }
}

// Set zoom factor with anchor point (for pinch-to-zoom gestures)
void
DocumentView::setZoomAnchored(double factor, QPointF anchorScenePos) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::setZoomAnchored(): Zooming to" << factor
             << "anchored at" << anchorScenePos;
#endif

    if (m_model->isImage())
    {
        GraphicsImageItem *imageItem = m_page_items_hash.value(0, nullptr);
        if (!imageItem)
            return;

        // 1. Capture the pixel-exact position of the cursor in the viewport
        const QPointF anchorViewport = m_gview->mapFromScene(anchorScenePos);

        // 2. Map the scene anchor to the image's LOCAL coordinates.
        // This point (e.g., the nose of a person in a photo) remains constant
        // relative to the pixels even when we scale or rotate the item.
        const QPointF localPos = imageItem->mapFromScene(anchorScenePos);

        // 3. Update the model and the item scale
        m_current_zoom = factor;
        m_model->setZoom(m_current_zoom);

        // Use setScale directly to ensure it matches the zoom factor precisely
        imageItem->setScale(m_current_zoom);

        // 4. Update the layout.
        // repositionPages() likely moves the item (setPos), which changes
        // mapToScene logic.
        updateSceneRect();
        repositionPages();

        // 5. Find where that same LOCAL pixel is in the scene NOW after
        // scaling/repositioning
        const QPointF newScenePos = imageItem->mapToScene(localPos);

        // 6. Calculate the new center.
        // We want: NewScenePos to appear at AnchorViewport pixels.
        // QGraphicsView::centerOn(target) puts 'target' at the exact center of
        // the viewport.
        const QPointF viewportCenter(m_gview->viewport()->width() / 2.0,
                                     m_gview->viewport()->height() / 2.0);
        const QPointF centerOffset = anchorViewport - viewportCenter;
        const QPointF targetCenter = newScenePos - centerOffset;

        m_gview->centerOn(targetCenter);

        m_gview->flashScrollbars();

        // Debounced HQ re-render: after rapid zoom settles, replace the
        // transform-scaled (blurry) pixels with a smooth re-render at the
        // exact target dimensions. Mirrors the document HQ zoom approach.
        if (!m_model->isAnimated())
            m_hq_render_timer->start();

        emit zoomChanged(m_current_zoom);
#ifdef WITH_LUA
        dispatchLuaEvent(DispatchType::OnZoomChanged);
#endif
        return;
    }

    factor = std::clamp(factor, MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);

    if (qFuzzyCompare(factor, m_current_zoom))
        return;

    // Capture anchor in page-local normalised coords.
    // Runs unconditionally — even when the cursor is in a margin/gap.
    // relX/relY default to 0.5 so the SINGLE restore block has a sane fallback.
    const QPointF anchorViewport = m_gview->mapFromScene(anchorScenePos);
    const QPointF viewportRatio(
        anchorViewport.x() / m_gview->viewport()->width(),
        anchorViewport.y() / m_gview->viewport()->height());

    int anchorPage              = -1;
    GraphicsImageItem *pageItem = nullptr;
    double relX = 0.5, relY = 0.5;
    if (pageAtScenePos(anchorScenePos, anchorPage, pageItem) && pageItem)
    {
        const QPointF localPos     = pageItem->mapFromScene(anchorScenePos);
        const QSizeF pagePixelSize = pageItem->boundingRect().size();
        relX                       = localPos.x() / pagePixelSize.width();
        relY                       = localPos.y() / pagePixelSize.height();
    }
    else if (const int nearest = nearestPageToScenePos(anchorScenePos);
             nearest >= 0)
    {
        // The anchor is in a margin or a gap: without a page to hold on to the
        // view would end up somewhere else after the relayout. The position
        // relative to the nearest page is kept instead (it may be outside it).
        if (GraphicsImageItem *item = m_page_items_hash.value(nearest, nullptr))
        {
            const QPointF localPos = item->mapFromScene(anchorScenePos);
            const QSizeF pageSize  = item->boundingRect().size();
            if (pageSize.width() > 0 && pageSize.height() > 0)
            {
                anchorPage = nearest;
                relX       = localPos.x() / pageSize.width();
                relY       = localPos.y() / pageSize.height();
            }
        }
    }

    // SINGLE: synchronous relayout + anchor restore
    if (m_layout_mode == LayoutMode::SINGLE)
    {
        m_current_zoom = factor;
        m_gview->setUpdatesEnabled(false);
        m_gscene->blockSignals(true);
        invalidateVisiblePagesCache();
        ClearTextSelection();
        m_model->setZoom(m_current_zoom);
        cachePageStride();
        updateSceneRect();
        repositionPages();
        m_gscene->blockSignals(false);
        m_gview->setUpdatesEnabled(true);

        restoreZoomAnchor(anchorPage, relX, relY, viewportRatio);

        // Sharpen once zooming settles instead of on every step.
        m_scroll_page_update_timer->start();

        m_gview->flashScrollbars();
        emit zoomChanged(m_current_zoom);
#ifdef WITH_LUA
        dispatchLuaEvent(DispatchType::OnZoomChanged);
#endif
        return;
    }

    // Multi-page (VERTICAL, HORIZONTAL, BOOK): synchronous relayout, same as
    // SINGLE above. Layout/positioning (cachePageStride/repositionPages) is
    // cheap — it only touches already-loaded items via setPos/setScale, no
    // rendering — so there is no need to defer it behind a GPU view-transform
    // and "bake" it later; doing so previously caused a visible jump whenever
    // the deferred bake's layout didn't pixel-match the interim transform.
    // Only the actual expensive re-render (in renderPages(), triggered by
    // m_scroll_page_update_timer below) still needs debouncing.
    {
        m_current_zoom = factor;
        m_gview->setUpdatesEnabled(false);
        m_gscene->blockSignals(true);
        m_vscroll->blockSignals(true);
        m_hscroll->blockSignals(true);
        invalidateVisiblePagesCache();
        ClearTextSelection();
        m_model->setZoom(m_current_zoom);
        cachePageStride();
        updateSceneRect();
        repositionPages();
        m_vscroll->blockSignals(false);
        m_hscroll->blockSignals(false);
        m_gscene->blockSignals(false);
        m_gview->setUpdatesEnabled(true);

        restoreZoomAnchor(anchorPage, relX, relY, viewportRatio);

        m_scroll_page_update_timer->start();
    }

    m_gview->flashScrollbars();
    emit zoomChanged(m_current_zoom);
#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnZoomChanged);
#endif
}

// Given a page-local anchor point (relX, relY on anchorPage) and where it
// should land in the viewport (viewportRatio), re-center the view so that
// point stays visually fixed after a zoom relayout. Shared by both the
// SINGLE and multi-page branches of setZoomAnchored() above.
void
DocumentView::restoreZoomAnchor(int anchorPage, double relX, double relY,
                                const QPointF &viewportRatio) noexcept
{
    if (anchorPage < 0)
        return;

    // Re-fetch the page item — it may have been rebuilt by repositionPages().
    GraphicsImageItem *pageItem = m_page_items_hash.value(anchorPage, nullptr);
    if (!pageItem)
        return;

    const QSizeF newPageSize = pageItem->boundingRect().size();
    const QPointF newLocalPos(relX * newPageSize.width(),
                              relY * newPageSize.height());
    const QPointF newScenePos = pageItem->mapToScene(newLocalPos);

    const QPointF currentCenter
        = m_gview->mapToScene(m_gview->viewport()->rect().center());
    const QPointF desiredAnchorViewport(
        viewportRatio.x() * m_gview->viewport()->width(),
        viewportRatio.y() * m_gview->viewport()->height());
    const QPointF anchorOffset
        = m_gview->mapToScene(desiredAnchorViewport.toPoint()) - currentCenter;
    m_gview->centerOn(newScenePos - anchorOffset);
}

void
DocumentView::CenterOnLocation(const PageLocation &targetLocation) noexcept
{
    if (targetLocation.pageno < 0
        || targetLocation.pageno >= m_model->numPages())
        return;

    // Continuous / LTR layouts
    GraphicsImageItem *pageItem
        = m_page_items_hash.value(targetLocation.pageno, nullptr);
    if (!pageItem)
        return;

    const QPointF targetPixelPos = m_model->toPixelSpace(
        targetLocation.pageno, {targetLocation.x, targetLocation.y});

    const QPointF scenePos = pageItem->mapToScene(targetPixelPos);

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        GotoPage(targetLocation.pageno);
    }
    else
    {
        m_gview->centerOn(scenePos);
    }
}

void
DocumentView::GotoLocation(const PageLocation &targetLocation) noexcept
{
    if (m_model->numPages() == 0)
        return;

    // Sanitize NaN coordinates - default to center of page
    PageLocation sanitized = targetLocation;
    if (std::isnan(sanitized.x) || std::isnan(sanitized.y))
    {
        const auto pageDim = m_model->page_dimension_pts(sanitized.pageno);
        if (std::isnan(sanitized.x))
            sanitized.x = pageDim.width_pts / 2.0f;
        if (std::isnan(sanitized.y))
            sanitized.y = pageDim.height_pts / 2.0f;
    }

    // HANDLE PENDING RENDERS
    if (!m_page_items_hash.contains(sanitized.pageno))
    {
#ifndef NDEBUG
        qDebug() << "DocumentView::GotoLocation(): Target page"
                 << sanitized.pageno
                 << "not yet rendered. Deferring jump until render.";
#endif
        m_pending_jump = sanitized;
        GotoPage(sanitized.pageno);
        return;
    }

#ifndef NDEBUG
    qDebug() << "DocumentView::GotoLocation(): Requested "
                "target location:"
             << sanitized.pageno << sanitized.x << sanitized.y
             << "in document with" << m_model->numPages() << "pages.";
#endif

    // Continuous / LTR layouts
    GraphicsImageItem *pageItem
        = m_page_items_hash.value(sanitized.pageno, nullptr);
    if (!pageItem)
        return;
    if (m_placeholder_pages.contains(sanitized.pageno))
    {
        m_pending_jump = sanitized;
        GotoPage(sanitized.pageno);
        return;
    }

    const QPointF targetPixelPos
        = m_model->toPixelSpace(sanitized.pageno, {sanitized.x, sanitized.y});

    const QPointF scenePos = pageItem->mapToScene(targetPixelPos);

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        if (m_pageno != sanitized.pageno)
            GotoPage(sanitized.pageno);
    }

    m_gview->centerOn(scenePos);

    if (m_jump_marker)
        m_jump_marker->showAt(scenePos.x(), scenePos.y());

    m_old_jump_marker_loc = sanitized;
    m_pending_jump        = {-1, 0, 0};
}

void
DocumentView::GotoLocationWithHistory(
    const PageLocation &targetLocation) noexcept
{
    const PageLocation current = CurrentLocation();
    if (current.pageno != -1)
        addToHistory(current);

    addToHistory(targetLocation);
    GotoLocation(targetLocation);
}

void
DocumentView::GotoPageWithHistory(int pageno) noexcept
{
    const PageLocation current = CurrentLocation();
    if (current.pageno != -1)
        addToHistory(current);

    GotoPage(pageno);
    const PageLocation target = CurrentLocation();
    if (target.pageno != -1)
        addToHistory(target);
}

// Go to specific page number
// Does not render page directly, just adjusts scrollbar

/*
 * NOTE: You have to handle history saving yourself and is not handled
 * inside this function
 */
void
DocumentView::GotoPage(int pageno) noexcept
{
    if (pageno < 0 || pageno >= m_model->numPages())
        return;

    m_pageno = pageno;

    if (!m_visible_pages_cache.contains(pageno))
        invalidateVisiblePagesCache();

    emit currentPageChanged(pageno + 1);

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        ClearTextSelection();
        renderPage();
    }
    else if (m_layout_mode == LayoutMode::HORIZONTAL)
    {
        // Center the view on the horizontal middle of the page
        const double x
            = pageOffset(pageno) + pageSceneSize(pageno).width() / 2.0;
        m_gview->centerOn(QPointF(x, m_gview->sceneRect().center().y()));
    }
    else
    {
        // Center on the spread
        const double y
            = pageOffset(pageno) + pageSceneSize(pageno).height() / 2.0;
        m_gview->centerOn(QPointF(m_gview->sceneRect().center().x(), y));
    }

    if (m_visual_line_mode)
    {
        m_visual_line_index = 0;
        snapVisualLine();
    }

    // Caret navigation (caretStepLeft/Right, caretMoveVertical) keeps
    // m_caret_pageno in sync itself when it drives the page change; a page
    // change from elsewhere (search, outline, goto) just hides the now-stale
    // caret — the next caret move resyncs it to the new page.
    if (m_caret_mode && pageno != m_caret_pageno)
        hideCaret();

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnPageChanged);
#endif
}

// Go to next page
void
DocumentView::GotoNextPage() noexcept
{
    if (m_pageno >= m_model->numPages() - 1)
        return;

    if (m_layout_mode == DocumentView::LayoutMode::BOOK)
    {
        int next = (m_pageno == 0) ? 1 : m_pageno + 2;
        GotoPage(std::min(next, m_model->numPages() - 1));
    }
    else
    {
        GotoPage(m_pageno + 1);
    }
}

void
DocumentView::GotoPrevPage() noexcept
{
    if (m_pageno == 0)
        return;

    if (m_layout_mode == DocumentView::LayoutMode::BOOK)
    {
        int prev = (m_pageno <= 2) ? 0 : m_pageno - 2;
        GotoPage(prev);
    }
    else
    {
        GotoPage(m_pageno - 1);
    }
}

// Zoom in by a fixed factor
void
DocumentView::ZoomIn() noexcept
{
    if (m_current_zoom >= MAX_ZOOM_FACTOR)
        return;

    const double newZoom = std::clamp(m_current_zoom * m_config.zoom.factor,
                                      MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);
    setZoomAnchored(newZoom, zoomCommandAnchor());
}

// Zoom out by a fixed factor
void
DocumentView::ZoomOut() noexcept
{
    if (m_current_zoom <= MIN_ZOOM_FACTOR)
        return;

    const double newZoom = std::clamp(m_current_zoom / m_config.zoom.factor,
                                      MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);
    setZoomAnchored(newZoom, zoomCommandAnchor());
}

QPointF
DocumentView::zoomCommandAnchor() const noexcept
{
    // The cursor can be anywhere on the screen when a key is pressed: only
    // use it if it is over the view, or the zoom pivots around a point that
    // is not even shown and the view ends up somewhere else.
    if (m_config.zoom.anchor_to_mouse)
    {
        const QPoint cursor
            = m_gview->viewport()->mapFromGlobal(QCursor::pos());
        if (m_gview->viewport()->rect().contains(cursor))
            return m_gview->mapToScene(cursor);
    }
    return m_gview->mapToScene(m_gview->viewport()->rect().center());
}

// Reset zoom to 100%
void
DocumentView::ZoomReset() noexcept
{
    PageLocation loc = CurrentLocation();
    m_current_zoom   = 1.0f;
    invalidateVisiblePagesCache();
    zoomHelper(loc);
}

// Scroll left by a fixed amount
void
DocumentView::ScrollLeft() noexcept
{
    if (m_caret_mode)
    {
        caretMoveLeft();
    }
    else if (m_visual_line_mode)
    {
        visual_line_move(Direction::LEFT);
    }
    else
    {
        m_hscroll->setUpdatesEnabled(false);
        m_hscroll->setValue(m_hscroll->value() - HSCROLL_STEP);
        m_hscroll->setUpdatesEnabled(true);
    }
}

// Scroll right by a fixed amount
void
DocumentView::ScrollRight() noexcept
{
    if (m_caret_mode)
    {
        caretMoveRight();
    }
    else if (m_visual_line_mode)
    {
        visual_line_move(Direction::RIGHT);
    }
    else
    {
        m_hscroll->setUpdatesEnabled(false);
        m_hscroll->setValue(m_hscroll->value() + HSCROLL_STEP);
        m_hscroll->setUpdatesEnabled(true);
    }
}

// Scroll up by a fixed amount
void
DocumentView::ScrollUp() noexcept
{
    if (m_caret_mode)
    {
        caretMoveUp();
    }
    else if (m_visual_line_mode)
    {
        visual_line_move(Direction::UP);
    }
    else
    {
        m_vscroll->setUpdatesEnabled(false);
        m_vscroll->setValue(m_vscroll->value() - VSCROLL_STEP);
        m_vscroll->setUpdatesEnabled(true);
    }
}

void
DocumentView::ScrollDown_HalfPage() noexcept
{
    GraphicsImageItem *pageItem = m_page_items_hash.value(m_pageno, nullptr);
    if (!pageItem)
        return;

    m_vscroll->setUpdatesEnabled(false);
    m_vscroll->setValue(m_vscroll->value() + pageItem->height() / 2);
    m_vscroll->setUpdatesEnabled(true);
}

void
DocumentView::ScrollUp_HalfPage() noexcept
{
    GraphicsImageItem *pageItem = m_page_items_hash.value(m_pageno, nullptr);
    if (!pageItem)
        return;

    m_vscroll->setUpdatesEnabled(false);
    m_vscroll->setValue(m_vscroll->value() - pageItem->height() / 2);
    m_vscroll->setUpdatesEnabled(true);
}

// Scroll down by a fixed amount
void
DocumentView::ScrollDown() noexcept
{
    if (m_caret_mode)
    {
        caretMoveDown();
    }
    else if (m_visual_line_mode)
    {
        visual_line_move(Direction::DOWN);
    }
    else
    {
        m_vscroll->setUpdatesEnabled(false);
        m_vscroll->setValue(m_vscroll->value() + VSCROLL_STEP);
        m_vscroll->setUpdatesEnabled(true);
    }
}

void
DocumentView::ScrollBy(int dx, int dy) noexcept
{
    if (dx)
        m_hscroll->setValue(m_hscroll->value() + dx);
    if (dy)
        m_vscroll->setValue(m_vscroll->value() + dy);
}

void
DocumentView::ScrollTo(int x, int y) noexcept
{
    m_hscroll->setValue(x);
    m_vscroll->setValue(y);
}

QPoint
DocumentView::scrollPosition() const noexcept
{
    return {m_hscroll->value(), m_vscroll->value()};
}

QPoint
DocumentView::scrollMaximum() const noexcept
{
    return {m_hscroll->maximum(), m_vscroll->maximum()};
}

std::vector<int>
DocumentView::VisiblePages() noexcept
{
    if (m_model->isImage())
        return {0};
    const std::set<int> &pages = getVisiblePages();
    return {pages.begin(), pages.end()};
}

// Toggle auto-resize mode
void
DocumentView::ToggleAutoResize() noexcept
{
    m_auto_resize = !m_auto_resize;
}

// Toggle thumbnail panel
void
DocumentView::ToggleThumbnailPanel() noexcept
{
    DocumentContainer *container = this->container();
    assert(container && "DocumentView should have a valid container");

    if (!container->thumbnailView())
        container->createThumbnailView(this);

    container->toggleThumbnailView();
}

// Go to the first page
void
DocumentView::GotoFirstPage() noexcept
{
    GotoPageWithHistory(0);
    m_vscroll->setValue(0);
}
// Go to the last page
void
DocumentView::GotoLastPage() noexcept
{
    GotoPageWithHistory(m_model->numPages() - 1);
    m_vscroll->setValue(m_vscroll->maximum());
}

// Go back in history
void
DocumentView::GoBackHistory() noexcept
{
    if (m_loc_history_index <= 0
        || m_loc_history_index >= static_cast<int>(m_loc_history.size()))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::GoBackHistory(): Going back in history";
#endif

    m_loc_history_index -= 1;
    const PageLocation target = m_loc_history[m_loc_history_index];
    emit historyChanged();
    GotoLocation(target);
}

// Go forward in history
void
DocumentView::GoForwardHistory() noexcept
{
    if (m_loc_history_index < 0
        || m_loc_history_index + 1 >= static_cast<int>(m_loc_history.size()))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::GoForwardHistory(): Going forward in history";
#endif

    m_loc_history_index += 1;
    const PageLocation target = m_loc_history[m_loc_history_index];
    emit historyChanged();
    GotoLocation(target);
}

void
DocumentView::enterEvent(QEnterEvent *e)
{
    QWidget::enterEvent(e);
    if (m_config.split.focus_follows_mouse)
    {
        if (auto cont = container())
            cont->focusView(this);
    }
}

void
DocumentView::resizeEvent(QResizeEvent *event)
{
    // TODO: Maybe do this only when auto resize is enabled ?
    invalidateVisiblePagesCache();

    if (m_resize_timer)
        m_resize_timer->start();

    // Update the text selection path item to shift with the viewport during
    // resize, so it remains correctly aligned

    QWidget::resizeEvent(event);
}

void
DocumentView::handleDeferredResize() noexcept
{
    // clearDocumentItems();
    cachePageStride();
    updateSceneRect();

    if (m_layout_mode == LayoutMode::SINGLE)
        renderPage();
    else
        renderPages();

    if (m_auto_resize)
    {
        setFitMode(m_fit_mode);
        fitModeChanged(m_fit_mode);
    }
}

void
DocumentView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (!m_deferred_fit)
        return;

    setFitMode(m_fit_mode);
    m_deferred_fit = false;
}

void
DocumentView::addToHistory(const PageLocation &location) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::addLocationToHistory(): Adding location to "
             << "history: Page =" << location.pageno << ", x =" << location.x
             << ", y =" << location.y;
#endif
    if (location.pageno < 0)
        return;

    if (m_loc_history_index + 1 < static_cast<int>(m_loc_history.size()))
    {
        m_loc_history.erase(m_loc_history.begin() + m_loc_history_index + 1,
                            m_loc_history.end());
    }

    if (!m_loc_history.empty()
        && locationsEqual(m_loc_history.back(), location))
    {
        m_loc_history_index = static_cast<int>(m_loc_history.size() - 1);
        return;
    }

    m_loc_history.push_back(location);
    m_loc_history_index = static_cast<int>(m_loc_history.size() - 1);
    emit historyChanged();
}

void
DocumentView::setInvertColor(bool invert) noexcept
{
    // The view's local option is the source of truth, so re-applying the
    // behavior section later keeps the toggled state.
    m_config.behavior.invert_mode = invert;
    m_model->setInvertColor(invert);
    if (m_model->isAnimated())
    {
        renderImage();
        return;
    }

    if (m_layout_mode == LayoutMode::SINGLE)
        renderPage();
    else
        renderPages();
}

// Returns the current location in the document that the user is viewing
PageLocation
DocumentView::CurrentLocation() noexcept
{
    if (m_page_items_hash.isEmpty())
        return {-1, 0, 0};

    // Use viewport center — must match what GotoLocation restores via
    // centerOn()
    const QPointF viewCenter = m_gview->mapToScene(
        m_gview->viewport()->width() / 2, m_gview->viewport()->height() / 2);

    // Find the actual page at the viewport center
    int pageIndex               = -1;
    GraphicsImageItem *pageItem = nullptr;
    if (!pageAtScenePos(viewCenter, pageIndex, pageItem) || !pageItem)
    {
        // Fallback to m_pageno if no page at center (e.g., between pages)
        pageItem = m_page_items_hash.value(m_pageno, nullptr);
        if (!pageItem)
            return {-1, 0, 0};
        pageIndex = m_pageno;
    }

    const QPointF itemLocal       = pageItem->mapFromScene(viewCenter);
    const QPointF logicalPixelPos = itemLocal * pageItem->scale();
    const fz_point pdfPos = m_model->toPDFSpace(pageIndex, logicalPixelPos);

    return {pageIndex, pdfPos.x, pdfPos.y};
}

void
DocumentView::remapNarrowForRotation(bool clockwise) noexcept
{
    if (!m_is_narrow)
        return;
    const double l = m_narrow_local_normalized.left();
    const double t = m_narrow_local_normalized.top();
    const double w = m_narrow_local_normalized.width();
    const double h = m_narrow_local_normalized.height();
    // After a 90° rotation the page swaps width/height in device space.
    // Remap the normalized rect so it continues to cover the same visual area.
    // 90° CW:  (l, t, w, h) → (1−t−h, l,     h, w)
    // 90° CCW: (l, t, w, h) → (t,     1−l−w, h, w)
    if (clockwise)
        m_narrow_local_normalized = QRectF(1.0 - t - h, l, h, w);
    else
        m_narrow_local_normalized = QRectF(t, 1.0 - l - w, h, w);
}

QRectF
DocumentView::narrowSceneRect() const noexcept
{
    const int endPage
        = (m_narrow_page_end < 0) ? m_narrow_page : m_narrow_page_end;
    const bool isPageRange = (endPage != m_narrow_page);

    // For a multi-page (pages-narrow) range, derive bounds from the
    // pre-computed page offsets rather than from materialized page items.
    // Items may not exist yet for the whole range, so using m_page_items_hash
    // produces an incomplete / empty rect that makes scrolling wonky.
    if (isPageRange && static_cast<int>(m_page_offsets.size()) > endPage + 1)
    {
        const double start = m_page_offsets[m_narrow_page];
        const double end   = m_page_offsets[endPage + 1];
        const QRectF &lr   = m_layout_scene_rect;

        if (m_layout_mode == LayoutMode::HORIZONTAL)
            return QRectF(start, lr.top(), end - start, lr.height());

        // VERTICAL / SINGLE / BOOK — main axis is Y
        return QRectF(lr.left(), start, lr.width(), end - start);
    }

    // Region-narrow (single page): use the page item for the exact local rect.
    QRectF unioned;
    for (int p = m_narrow_page; p <= endPage; ++p)
    {
        const auto *pageItem = m_page_items_hash.value(p, nullptr);
        if (!pageItem)
            continue;
        const QSizeF sz = pageItem->boundingRect().size();
        if (sz.isEmpty())
            continue;
        const QRectF localRect(m_narrow_local_normalized.left() * sz.width(),
                               m_narrow_local_normalized.top() * sz.height(),
                               m_narrow_local_normalized.width() * sz.width(),
                               m_narrow_local_normalized.height()
                                   * sz.height());
        const QRectF sceneRect = pageItem->mapToScene(localRect).boundingRect();
        unioned = unioned.isNull() ? sceneRect : unioned.united(sceneRect);
    }
    return unioned;
}

void
DocumentView::refreshNarrowVisuals() noexcept
{
    const QRectF nr = narrowSceneRect();
    if (!nr.isValid())
        return;

    m_gview->setNarrowRect(nr);
    m_gview->setSceneRect(nr);
}

void
DocumentView::applyNarrow(QRectF sceneRect) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(sceneRect.center(), pageno, pageItem))
        return;

    const QRectF localRect = pageItem->mapFromScene(sceneRect).boundingRect();
    const QSizeF sz        = pageItem->boundingRect().size();
    if (sz.isEmpty())
        return;

    m_narrow_page             = pageno;
    m_narrow_page_end         = pageno;
    m_narrow_local_normalized = QRectF(
        localRect.left() / sz.width(), localRect.top() / sz.height(),
        localRect.width() / sz.width(), localRect.height() / sz.height());
    m_is_narrow = true;

    // Zoom to fit the narrow region in the viewport
    const double vw = m_gview->viewport()->width();
    const double vh = m_gview->viewport()->height();
    const double nw = sceneRect.width();
    const double nh = sceneRect.height();
    if (nw > 0 && nh > 0)
    {
        const double fitZoom
            = std::min(vw * m_current_zoom / nw, vh * m_current_zoom / nh);
        setZoom(std::clamp(fitZoom, MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR), false);
    }

    refreshNarrowVisuals();
    m_gview->centerOn(narrowSceneRect().center());
    m_gview->flashScrollbars();
    emit narrowModeChanged(true);

    // Restore previous mode after region draw. Default to TextSelection for
    // text-capable docs and RegionSelection for image docs (no text layer).
    const auto restoreMode
        = (m_gview->getDefaultMode() != GraphicsView::Mode::None)
              ? m_gview->getDefaultMode()
              : (m_model->isImage() ? GraphicsView::Mode::RegionSelection
                                    : GraphicsView::Mode::TextSelection);
    m_gview->setMode(restoreMode);
    emit selectionModeChanged(restoreMode);
}

void
DocumentView::NarrowToSectionByTitle(const QString &title) noexcept
{
    if (!m_model)
        return;

    fz_outline *outline = m_model->getOutline();
    if (!outline)
        outline = m_model->getGeneratedOutline();
    if (!outline)
        return;

    struct Section
    {
        QString title;
        int depth;
        int startPage0;
        int endPage0;
    };

    QList<Section> sections;
    std::function<void(fz_outline *, int)> harvest
        = [&](fz_outline *node, int depth)
    {
        for (fz_outline *n = node; n; n = n->next)
        {
            const int pageno = m_model->resolveOutlineNode(n);
            if (pageno >= 0)
                sections.append({QString(n->title ? n->title : "").simplified(),
                                 depth, pageno, -1});
            if (n->down)
                harvest(n->down, depth + 1);
        }
    };
    harvest(outline, 0);

    if (sections.isEmpty())
        return;

    const int totalPages = m_model->numPages();

    for (int i = 0; i < sections.size(); ++i)
    {
        const QString cp1 = sections[i].title + ".";
        const QString cp2 = sections[i].title + " ";
        int end           = totalPages - 1;
        for (int j = i + 1; j < sections.size(); ++j)
        {
            const bool descendant = sections[j].depth > sections[i].depth
                                    || sections[j].title.startsWith(cp1)
                                    || sections[j].title.startsWith(cp2);
            if (sections[j].startPage0 > sections[i].startPage0 && !descendant)
            {
                end = sections[j].startPage0;
                break;
            }
        }
        sections[i].endPage0 = std::max(end, sections[i].startPage0);
    }

    int chosen = -1;
    for (int i = 0; i < sections.size(); ++i)
    {
        if (sections[i].title.compare(title, Qt::CaseInsensitive) == 0)
        {
            chosen = i;
            break;
        }
    }
    if (chosen < 0)
    {
        for (int i = 0; i < sections.size(); ++i)
        {
            if (sections[i].title.contains(title, Qt::CaseInsensitive))
            {
                chosen = i;
                break;
            }
        }
    }

    if (chosen < 0)
        return;

    NarrowToPages(sections[chosen].startPage0 + 1,
                  sections[chosen].endPage0 + 1);
}

void
DocumentView::NarrowToRegion() noexcept
{
    if (m_is_narrow)
    {
        WidenRegion();
        return;
    }
    startRegionSelect([this](QRectF area) { applyNarrow(area); });
}

void
DocumentView::ZoomToRegion(QRectF sceneRect) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(sceneRect.center(), pageno, pageItem))
        return;

    const QRectF localRect = pageItem->mapFromScene(sceneRect).boundingRect();
    const QSizeF sz        = pageItem->boundingRect().size();
    if (sz.isEmpty())
        return;

    // Selection midpoint in page-local normalized coords, captured before
    // the zoom change so it can be re-mapped to the new page geometry below
    // — same idea as the anchor-restore used for cursor-anchored zoom.
    const double relX
        = (localRect.left() + localRect.width() / 2.0) / sz.width();
    const double relY
        = (localRect.top() + localRect.height() / 2.0) / sz.height();

    const double vw = m_gview->viewport()->width();
    const double vh = m_gview->viewport()->height();
    const double nw = sceneRect.width();
    const double nh = sceneRect.height();
    if (nw <= 0 || nh <= 0)
        return;

    const double fitZoom
        = std::min(vw * m_current_zoom / nw, vh * m_current_zoom / nh);
    setZoom(std::clamp(fitZoom, MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR), false);

    // Re-fetch the page item — repositionPages() may have rebuilt it — then
    // center the viewport on the selection's midpoint at the new zoom.
    GraphicsImageItem *newItem = m_page_items_hash.value(pageno, nullptr);
    if (newItem)
    {
        const QSizeF newSz = newItem->boundingRect().size();
        m_gview->centerOn(newItem->mapToScene(
            QPointF(relX * newSz.width(), relY * newSz.height())));
    }
    m_gview->flashScrollbars();
}

void
DocumentView::ZoomToSelection() noexcept
{
    startRegionSelect([this](QRectF area) { ZoomToRegion(area); });
}

void
DocumentView::WidenRegion() noexcept
{
    if (!m_is_narrow)
        return;
    m_is_narrow               = false;
    m_narrow_page             = -1;
    m_narrow_page_end         = -1;
    m_narrow_local_normalized = {};
    m_gview->clearNarrowRect();
    updateSceneRect();
    m_gview->flashScrollbars();
    emit narrowModeChanged(false);
    emit narrowPageRangeChanged(-1, -1, m_model ? m_model->numPages() : 0);
}

// Narrow to an inclusive range of 1-indexed pages. The narrow rect covers
// the full extent of every page in the range so scrolling stays within
// those pages and search/filter is limited to them.
void
DocumentView::NarrowToPages(int startPage1, int endPage1) noexcept
{
    if (!m_model)
        return;

    const int total = m_model->numPages();
    if (total <= 0)
        return;

    int start = std::min(startPage1, endPage1);
    int end   = std::max(startPage1, endPage1);
    if (start < 1)
        start = 1;
    if (end > total)
        end = total;
    if (start > total || end < 1)
        return;

    m_narrow_page             = start - 1;
    m_narrow_page_end         = end - 1;
    m_narrow_local_normalized = QRectF(0.0, 0.0, 1.0, 1.0);
    m_is_narrow               = true;

    // Ensure the first page in the range is materialized before computing
    // the scene rect.
    GotoPage(start);

    refreshNarrowVisuals();
    const QRectF nr = narrowSceneRect();
    if (nr.isValid())
        m_gview->centerOn(nr.center());
    m_gview->flashScrollbars();
    emit narrowModeChanged(true);
    emit narrowPageRangeChanged(start, end, m_model->numPages());
}

// Re display the jump marker (e.g. after a jump link is activated), useful
// if user lost track of it for example.
void
DocumentView::Reshow_jump_marker() noexcept
{
    if (m_old_jump_marker_loc.pageno < 0)
        return;

    GraphicsImageItem *pageItem
        = m_page_items_hash.value(m_old_jump_marker_loc.pageno, nullptr);
    if (!pageItem)
        return;

    const QPointF targetPixelPos = m_model->toPixelSpace(
        m_old_jump_marker_loc.pageno,
        {m_old_jump_marker_loc.x, m_old_jump_marker_loc.y});
    const QPointF scenePos = pageItem->mapToScene(targetPixelPos);

    m_jump_marker->showAt(scenePos.x(), scenePos.y());
}

void
DocumentView::zoomHelper(const PageLocation &loc) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::zoomHelper(): Zooming to" << m_current_zoom;
#endif

    if (m_model->isImage())
    {
        m_model->setZoom(m_current_zoom);
        renderImage();
    }
    else
    {
        m_gview->setUpdatesEnabled(false);
        m_gscene->blockSignals(true);
        ClearTextSelection();
        m_model->setZoom(m_current_zoom);
        cachePageStride();
        updateSceneRect();
        repositionPages();
        m_gscene->blockSignals(false);
        m_gview->setUpdatesEnabled(true);

        m_gview->flashScrollbars();

        // Restore the exact viewport position
        if (loc.pageno != -1)
            CenterOnLocation(loc);
        else
            // Fallback if we were out of bounds
            GotoPage(m_pageno);
    }

    emit zoomChanged(m_current_zoom);
#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnZoomChanged);
#endif
}

void
DocumentView::handleHScrollValueChanged(int value) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleHScrollValueChanged(): Scrollbar value "
             << "changed to" << value;
#endif

    // Don't render on scroll for single-page documents or images
    if (m_layout_mode == LayoutMode::SINGLE)
        return;

    // During fast scrolling, only invalidate cache, don't trigger render
    invalidateVisiblePagesCache();

    updateCurrentPage();

    // Immediately request renders for currently visible pages so they don't
    // appear blank during fast scrolling (requestPageRender is a no-op if the
    // page is already pending).
    // Pages that already have a sharp render are left alone: the debounced
    // refresh below decides whether they need a new one.
    for (int pageno : getVisiblePages())
    {
        if (!m_pending_renders.contains(pageno)
            && !m_placeholder_pages.contains(pageno)
            && m_page_items_hash.value(pageno, nullptr)
            && m_page_render_keys.value(pageno) == currentPageRenderKey())
            continue;
        requestPageRender(pageno);
    }

    // Always restart the timer (debouncing)
    m_scroll_page_update_timer->start();

    // Don't trigger HQ render during rapid scrolling
    m_hq_render_timer->stop();
}

void
DocumentView::handleVScrollValueChanged(int /*value */) noexcept
{
    // Don't render on scroll for single-page documents or images
    if (m_layout_mode == LayoutMode::SINGLE)
        return;

    // During fast scrolling, only invalidate cache, don't trigger render
    invalidateVisiblePagesCache();

    updateCurrentPage();

    // Immediately request renders for currently visible pages so they don't
    // appear blank during fast scrolling (requestPageRender is a no-op if the
    // page is already pending).
    // Pages that already have a sharp render are left alone: the debounced
    // refresh below decides whether they need a new one.
    for (int pageno : getVisiblePages())
    {
        if (!m_pending_renders.contains(pageno)
            && !m_placeholder_pages.contains(pageno)
            && m_page_items_hash.value(pageno, nullptr)
            && m_page_render_keys.value(pageno) == currentPageRenderKey())
            continue;
        requestPageRender(pageno);
    }

    // Always restart the timer (debouncing)
    m_scroll_page_update_timer->start();

    // Don't trigger HQ render during rapid scrolling
    m_hq_render_timer->stop();
}
