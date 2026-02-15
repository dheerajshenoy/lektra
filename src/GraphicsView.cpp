#include "GraphicsView.hpp"

#include "Config.hpp"

#include <QApplication>
#include <QGestureEvent>
#include <QGraphicsItem>
#include <QGuiApplication>
#include <QLineF>
#include <QMenu>
#include <QNativeGestureEvent>
// #include <QOpenGLWidget>
#include <QPinchGesture>
#include <QScroller>
#include <QSwipeGesture>
#include <qsurfaceformat.h>

GraphicsView::GraphicsView(const Config &config, QWidget *parent)
    : QGraphicsView(parent), m_config(config)
{
    setMouseTracking(true);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setAcceptDrops(false);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing);
    setOptimizationFlag(QGraphicsView::DontSavePainterState);
    setContentsMargins(0, 0, 0, 0);
    setCacheMode(QGraphicsView::CacheBackground);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    // Overlay scrollbars - no reserved space
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Hide timer setup
    m_scrollbar_hide_timer.setSingleShot(true);
    m_scrollbar_hide_timer.setInterval(1500);
    connect(&m_scrollbar_hide_timer, &QTimer::timeout, this, [this]()
    {
        if (!m_autoHide)
            return;
        // Don't hide while actively dragging a scrollbar
        if (m_activeScrollbar)
        {
            m_scrollbar_hide_timer.start();
            return;
        }
        // Don't hide if any mouse button is pressed (fast scrolling protection)
        if (QGuiApplication::mouseButtons() != Qt::NoButton)
        {
            m_scrollbar_hide_timer.start();
            return;
        }
        // Don't hide if mouse is over a scrollbar
        if (scrollbarAt(mapFromGlobal(QCursor::pos())))
        {
            m_scrollbar_hide_timer.start();
            return;
        }
        hideScrollbars();
    });

    bindScrollbarActivity(verticalScrollBar(), horizontalScrollBar());

    // QSurfaceFormat format;
    // format.setSamples(4);
    // QOpenGLWidget *glWidget = new QOpenGLWidget();
    // glWidget->setFormat(format);
    // setViewport(glWidget);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    // Enable gesture events on the viewport (important for QGraphicsView)
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);

    // Qt gesture framework (often touchscreens; sometimes trackpads depending
    // on platform/plugin)
    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::SwipeGesture);

    // Optional: kinetic scrolling for touch (won’t harm trackpad, but mostly
    // for touchscreen) QScroller::grabGesture(viewport(),
    // QScroller::TouchGesture);

    if (!m_rubberBand)
        m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    m_rubberBand->hide();
}

void
GraphicsView::bindScrollbarActivity(QScrollBar *vertical,
                                    QScrollBar *horizontal) noexcept
{
    if (vertical)
    {
        connect(vertical, &QScrollBar::valueChanged, this,
                &GraphicsView::onScrollbarActivity, Qt::UniqueConnection);
    }
    if (horizontal)
    {
        connect(horizontal, &QScrollBar::valueChanged, this,
                &GraphicsView::onScrollbarActivity, Qt::UniqueConnection);
    }
}

void
GraphicsView::onScrollbarActivity() noexcept
{
    showScrollbars();
    restartHideTimer();
}

void
GraphicsView::clearRubberBand() noexcept
{
    if (!m_rubberBand)
        return;
    m_rubberBand->hide();
    m_rect = QRect();
}

void
GraphicsView::updateCursorForMode() noexcept
{
#ifndef NDEBUG
    qDebug() << "GraphicsView::updateCursorForMode(): Updating cursor for "
             << "mode:" << static_cast<int>(m_mode);
#endif
    if (m_selecting
        && (m_mode == Mode::TextSelection || m_mode == Mode::TextHighlight))
    {
        setCursor(Qt::IBeamCursor);
    }
    else
    {
        unsetCursor();
    }
}

void
GraphicsView::setMode(Mode mode) noexcept
{
    m_selecting = false;

    switch (m_mode)
    {
        case Mode::RegionSelection:
        case Mode::AnnotRect:
            if (m_rubberBand)
                m_rubberBand->hide();
            break;

        case Mode::TextSelection:
        case Mode::TextHighlight:
            emit textSelectionDeletionRequested();
            break;

        case Mode::AnnotSelect:
            emit annotSelectClearRequested();
            break;

        default:
            break;
    }

    m_mode = mode;
    updateCursorForMode();
}

void
GraphicsView::mousePressEvent(QMouseEvent *event)
{
    // Check if click is on overlay scrollbar
    if (QScrollBar *bar = scrollbarAt(event->pos()))
    {
        m_activeScrollbar = bar;
        m_scrollbar_hide_timer.stop();
        forwardMouseEvent(bar, event);
        return;
    }

#ifdef HAS_SYNCTEX
    if (m_mode == Mode::TextSelection && event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ShiftModifier))
    {
        emit synctexJumpRequested(mapToScene(event->pos()));
        m_ignore_next_release = true;
        event->accept();
        return; // don't forward to QGraphicsView
    }
#endif

    if ((m_mode == Mode::TextSelection || m_mode == Mode::TextHighlight)
        && event->button() == Qt::LeftButton)
    {
        if (QGraphicsItem *item = itemAt(event->pos()))
        {
            if (item->data(0).toString() == "link")
            {

                if (event->modifiers() & Qt::ControlModifier)
                {
                    emit linkCtrlClickRequested(mapToScene(event->pos()));
                    event->accept();
                }
                else
                {
                    QGraphicsView::mousePressEvent(event);
                }
                return;
            }
        }
    }

    // Multi-click tracking (avoid QLineF sqrt)
    if (m_mode == Mode::TextSelection && event->button() == Qt::LeftButton)
    {
        const bool timerOk = m_clickTimer.isValid()
                             && m_clickTimer.elapsed() < MULTI_CLICK_INTERVAL;

        const QPointF d    = event->pos() - m_lastClickPos;
        const double dist2 = d.x() * d.x() + d.y() * d.y();
        const double thresh2
            = CLICK_DISTANCE_THRESHOLD * CLICK_DISTANCE_THRESHOLD;

        const bool isMultiClick = timerOk && (dist2 < thresh2);

        m_clickCount = isMultiClick ? (m_clickCount + 1) : 1;
        if (m_clickCount > 4)
            m_clickCount = 1;

        m_lastClickPos = event->pos();
        m_clickTimer.restart();

        const QPointF scenePos = mapToScene(event->pos());

        if (m_clickCount == 2)
        {
            emit doubleClickRequested(scenePos);
            event->accept();
            return;
        }
        if (m_clickCount == 3)
        {
            emit tripleClickRequested(scenePos);
            event->accept();
            return;
        }
        if (m_clickCount == 4)
        {
            emit quadrupleClickRequested(scenePos);
            event->accept();
            return;
        }

        // single click
        emit textSelectionDeletionRequested();
    }

    switch (m_mode)
    {
        case Mode::RegionSelection:
        case Mode::AnnotRect:
        case Mode::AnnotSelect:
        {
            m_start     = event->pos();
            m_rect      = QRect();
            m_dragging  = false;
            m_selecting = true;

            m_rubberBand->setGeometry(QRect(m_start, QSize()));
            m_rubberBand->show();

            event->accept();
            return; // handled
        }

        case Mode::AnnotPopup:
        {
            if (event->button() == Qt::LeftButton)
            {
                emit annotPopupRequested(mapToScene(event->pos()));
                event->accept();
                return; // handled
            }
            break;
        }

        case Mode::TextSelection:
        case Mode::TextHighlight:
        {
            if (event->button() == Qt::LeftButton)
            {
                m_selecting = true;
                updateCursorForMode();
                const QPointF scenePos = mapToScene(event->pos());
                m_mousePressPos        = scenePos;
                m_selection_start      = scenePos;
                m_lastMovePos          = event->pos();

                event->accept();
                return; // handled
            }
            break;
        }

        default:
            break;
    }

    QGraphicsView::mousePressEvent(event);
}

void
GraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    // Forward to active scrollbar if dragging
    if (m_activeScrollbar)
    {
        forwardMouseEvent(m_activeScrollbar, event);
        return;
    }

    // If we are selecting text/highlight, throttle signals
    if ((m_mode == Mode::TextSelection || m_mode == Mode::TextHighlight)
        && m_selecting)
    {
        if ((event->pos() - m_lastMovePos).manhattanLength()
            < MOVE_EMIT_THRESHOLD_PX)
        {
            event->accept();
            return;
        }
        m_lastMovePos = event->pos();

        const QPointF scenePos = mapToScene(event->pos());
        if (m_mode == Mode::TextSelection || m_mode == Mode::TextHighlight)
            emit textSelectionRequested(m_selection_start, scenePos);
        // else
        //     emit textHighlightRequested(m_selection_start, scenePos);

        event->accept();
        return; // handled
    }

    // Rubber band modes: no mapToScene needed during drag
    if ((m_mode == Mode::AnnotSelect || m_mode == Mode::RegionSelection
         || m_mode == Mode::AnnotRect)
        && (event->buttons() & Qt::LeftButton) && m_selecting)
    {
        if (!m_dragging
            && (event->pos() - m_start).manhattanLength() > m_drag_threshold)
        {
            m_dragging = true;
        }

        if (m_dragging)
        {
            m_rect = QRect(m_start, event->pos()).normalized();
            m_rubberBand->setGeometry(m_rect);
        }

        event->accept();
        return; // handled
    }

    QGraphicsView::mouseMoveEvent(event);
}

void
GraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    // Complete scrollbar drag
    if (m_activeScrollbar)
    {
        forwardMouseEvent(m_activeScrollbar, event);
        m_activeScrollbar = nullptr;
        restartHideTimer();
        return;
    }

    if (m_ignore_next_release)
    {
        m_ignore_next_release = false;
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    const bool wasSelecting = m_selecting;
    m_selecting             = false;

    // If we weren't doing any interaction, let base handle it
    if (!wasSelecting)
    {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    // Text modes
    if (m_mode == Mode::TextSelection || m_mode == Mode::TextHighlight)
    {
        updateCursorForMode();

        const QPointF scenePos = mapToScene(event->pos());
        const int dist = (scenePos.toPoint() - m_mousePressPos.toPoint())
                             .manhattanLength();
        const bool isDrag = dist > m_drag_threshold;

        if (m_mode == Mode::TextSelection)
        {
            if (!isDrag || m_selection_start == scenePos)
                emit textSelectionDeletionRequested();
            else if (event->button() == Qt::LeftButton)
                emit textSelectionRequested(m_selection_start, scenePos);
        }
        else
        {
            if (isDrag)
            {
                emit textSelectionRequested(m_selection_start, scenePos);
                emit textHighlightRequested(m_selection_start, scenePos);
            }
        }

        m_dragging = false;
        event->accept();
        return; // handled
    }

    // Rubber band modes
    if (m_mode == Mode::RegionSelection || m_mode == Mode::AnnotRect
        || m_mode == Mode::AnnotSelect)
    {
        const QRectF sceneRect  = mapToScene(m_rect).boundingRect();
        const bool hasSelection = m_dragging && !sceneRect.isEmpty();

        if (m_mode != Mode::RegionSelection || !hasSelection)
            clearRubberBand();

        if (!m_dragging && m_mode == Mode::AnnotSelect)
        {
            emit annotSelectRequested(mapToScene(event->pos()));
        }
        else
        {
            if (hasSelection)
            {
                if (m_mode == Mode::RegionSelection)
                    emit regionSelectRequested(sceneRect);
                else if (m_mode == Mode::AnnotRect)
                    emit annotRectRequested(sceneRect);
                else
                    emit annotSelectRequested(sceneRect);
            }
        }

        m_dragging = false;
        event->accept();
        return; // handled
    }

    m_dragging = false;
    QGraphicsView::mouseReleaseEvent(event);
}

void
GraphicsView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier)
    {
        if (event->angleDelta().y() > 0)
            emit zoomInRequested();
        else
            emit zoomOutRequested();
        event->accept();
        return; // do NOT call base
    }

    // const int delta = !event->pixelDelta().isNull() ? event->pixelDelta().y()
    //                                                 :
    //                                                 event->angleDelta().y();

    QGraphicsView::wheelEvent(event);
}

void
GraphicsView::contextMenuEvent(QContextMenuEvent *event)
{
    bool handled = false;
    emit contextMenuRequested(event->globalPos(), &handled);
    if (handled)
    {
        event->accept();
        return;
    }

    QGraphicsView::contextMenuEvent(event);
}

// bool
// GraphicsView::viewportEvent(QEvent *event)
// {
//     // 1) High-level Qt gestures (Pinch/Swipe)
//     if (event->type() == QEvent::Gesture)
//     {
//         auto *ge = static_cast<QGestureEvent *>(event);

//         // Pinch zoom (touchscreens; sometimes trackpads)
//         if (QGesture *g = ge->gesture(Qt::PinchGesture))
//         {
//             auto *pinch = static_cast<QPinchGesture *>(g);

//             if (pinch->state() == Qt::GestureStarted)
//             {
//                 m_lastPinchScale = pinch->scaleFactor();
//                 m_zoomAccum      = 0.0;
//             }
//             else
//             {
//                 const qreal scaleNow = pinch->scaleFactor();
//                 const qreal ratio    = (m_lastPinchScale > 0.0)
//                                            ? (scaleNow / m_lastPinchScale)
//                                            : 1.0;
//                 m_lastPinchScale     = scaleNow;

//                 // Convert multiplicative ratio into additive “energy”
//                 // log(ratio) is symmetrical for in/out.
//                 if (ratio > 0.0)
//                     m_zoomAccum += std::log(ratio);

//                 while (m_zoomAccum > ZOOM_STEP_TRIGGER)
//                 {
//                     emit zoomInRequested();
//                     m_zoomAccum -= ZOOM_STEP_TRIGGER;
//                 }
//                 while (m_zoomAccum < -ZOOM_STEP_TRIGGER)
//                 {
//                     emit zoomOutRequested();
//                     m_zoomAccum += ZOOM_STEP_TRIGGER;
//                 }
//             }

//             ge->accept(pinch);
//             return true;
//         }

//         // Swipe gesture (usually touch). Map it to page nav if you like.
//         if (QGesture *g = ge->gesture(Qt::SwipeGesture))
//         {
//             auto *swipe = static_cast<QSwipeGesture *>(g);
//             if (swipe->state() == Qt::GestureFinished)
//             {
//                 // You can choose Up/Down or Left/Right for pages.
//                 // This example uses vertical swipe for pages:
//                 // Left or right swipe could be used for horizontal page nav.
//                 if (swipe->horizontalDirection() == QSwipeGesture::Left)
//                     emit scrollHorizontalRequested(swipe->swipeAngle() > 0 ?
//                     -1
//                                                                            :
//                                                                            1);
//                 else if (swipe->horizontalDirection() ==
//                 QSwipeGesture::Right)
//                     emit scrollHorizontalRequested(
//                         swipe->swipeAngle() > 0 ? 1 : -1);

//                 if (swipe->verticalDirection() == QSwipeGesture::Up)
//                     emit scrollVerticalRequested(swipe->swipeAngle() > 0 ? -1
//                                                                          :
//                                                                          1);
//                 else if (swipe->verticalDirection() == QSwipeGesture::Down)
//                     emit scrollVerticalRequested(swipe->swipeAngle() > 0 ? 1
//                                                                          :
//                                                                          -1);
//             }

//             ge->accept(swipe);
//             return true;
//         }

//         return QGraphicsView::viewportEvent(event);
//     }

//     // 2) Native gestures (trackpads: macOS; sometimes Wayland/X11 depending
//     on
//     // Qt/platform)
//     if (event->type() == QEvent::NativeGesture)
//     {
//         auto *ng = static_cast<QNativeGestureEvent *>(event);

//         switch (ng->gestureType())
//         {
//                 // case QNativeGestureEvent::Type::Ge
//                 // {
//                 //     // Qt provides a value per event; treat it as small
//                 //     deltas and
//                 //     // accumulate. On many systems: positive => zoom in,
//                 //     negative =>
//                 //     // zoom out.
//                 //     m_zoomAccum += ng->value();

//                 //     while (m_zoomAccum > ZOOM_STEP_TRIGGER)
//                 //     {
//                 //         emit zoomInRequested();
//                 //         m_zoomAccum -= ZOOM_STEP_TRIGGER;
//                 //     }
//                 //     while (m_zoomAccum < -ZOOM_STEP_TRIGGER)
//                 //     {
//                 //         emit zoomOutRequested();
//                 //         m_zoomAccum += ZOOM_STEP_TRIGGER;
//                 //     }

//                 //     event->accept();
//                 //     return true;
//                 // }

//                 // case QNativeGestureEvent::Swipe:
//                 // {
//                 //     // Some platforms provide swipe as native gesture.
//                 //     // ng->value() is typically +/-1-ish; direction
//                 mapping
//                 //     varies. const qreal v = ng->value(); if (v > 0)
//                 //         emit nextPageRequested();
//                 //     else if (v < 0)
//                 //         emit prevPageRequested();

//                 //     event->accept();
//                 //     return true;
//                 // }

//             default:
//                 break;
//         }

//         return QGraphicsView::viewportEvent(event);
//     }

//     return QGraphicsView::viewportEvent(event);
// }

void
GraphicsView::setAutoHideScrollbars(bool enabled)
{
    m_autoHide = enabled;
    if (enabled)
    {
        hideScrollbars();
        m_scrollbar_hide_timer.start();
    }
    else
    {
        m_scrollbar_hide_timer.stop();
        showScrollbars();
    }
}

bool
GraphicsView::viewportEvent(QEvent *event)
{
    if (m_autoHide)
    {
        switch (event->type())
        {
            case QEvent::Wheel:
            case QEvent::MouseMove:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
                showScrollbars();
                restartHideTimer();
                break;
            default:
                break;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void
GraphicsView::enterEvent(QEnterEvent *event)
{
    if (m_autoHide)
    {
        showScrollbars();
        restartHideTimer();
    }
    setActive(true);
    QGraphicsView::enterEvent(event);
}

void
GraphicsView::leaveEvent(QEvent *event)
{
    if (m_autoHide)
        restartHideTimer();
    setActive(false);
    QGraphicsView::leaveEvent(event);
}

void
GraphicsView::updateScrollbars()
{
    QScrollBar *vbar   = verticalScrollBar();
    QScrollBar *hbar   = horizontalScrollBar();
    const bool vNeeded = vbar && vbar->maximum() > vbar->minimum();
    const bool hNeeded = hbar && hbar->maximum() > hbar->minimum();
    const bool showV   = m_scrollbarsVisible && m_vbarEnabled && vNeeded;
    const bool showH   = m_scrollbarsVisible && m_hbarEnabled && hNeeded;

    if (vbar)
        vbar->setVisible(showV);
    if (hbar)
        hbar->setVisible(showH);

    if (showV || showH)
        layoutScrollbars();
}

void
GraphicsView::layoutScrollbars()
{
    QWidget *vp = viewport();
    if (!vp || vp->width() <= 0 || vp->height() <= 0)
        return;

    const int w      = vp->width();
    const int h      = vp->height();
    QScrollBar *vbar = verticalScrollBar();
    QScrollBar *hbar = horizontalScrollBar();
    const bool showV = vbar->isVisible();
    const bool showH = hbar->isVisible();

    // Position scrollbars as overlays on the viewport
    // Don't change parent - let Qt manage the scrollbar internally
    if (showV)
    {
        const int bottom
            = showH ? m_scrollbarSize + SCROLLBAR_MARGIN : SCROLLBAR_MARGIN;
        // Position relative to viewport
        vbar->setGeometry(w - m_scrollbarSize - SCROLLBAR_MARGIN,
                          SCROLLBAR_MARGIN, m_scrollbarSize,
                          h - SCROLLBAR_MARGIN - bottom);
        vbar->raise();
    }
    if (showH)
    {
        const int right
            = showV ? m_scrollbarSize + SCROLLBAR_MARGIN : SCROLLBAR_MARGIN;
        // Position relative to viewport
        hbar->setGeometry(SCROLLBAR_MARGIN,
                          h - m_scrollbarSize - SCROLLBAR_MARGIN,
                          w - SCROLLBAR_MARGIN - right, m_scrollbarSize);
        hbar->raise();
    }
}

void
GraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_scrollbarsVisible)
        layoutScrollbars();
}

void
GraphicsView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (m_scrollbarsVisible)
        layoutScrollbars();
}

QScrollBar *
GraphicsView::scrollbarAt(const QPoint &pos) const noexcept
{
    QScrollBar *vbar = verticalScrollBar();
    if (vbar && vbar->isVisible() && vbar->geometry().contains(pos))
        return vbar;
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && hbar->isVisible() && hbar->geometry().contains(pos))
        return hbar;
    return nullptr;
}

void
GraphicsView::forwardMouseEvent(QScrollBar *bar, QMouseEvent *event)
{
    QMouseEvent e(event->type(), bar->mapFromParent(event->pos()),
                  event->globalPosition(), event->button(), event->buttons(),
                  event->modifiers());
    QApplication::sendEvent(bar, &e);
    event->accept();
}

void
GraphicsView::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);

    // If not active, draw a subtle overlay to indicate inactive state. This is
    // especially useful when multiple views are open, so you can easily see
    // which one is active.
    if (m_config.split.dim_inactive && !m_active
        && m_config.split.dim_inactive_opacity > 0)
    {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, false);

        // Construct color once (or better yet, store this as a member variable)
        static QColor dimColor(
            0, 0, 0,
            static_cast<int>(m_config.split.dim_inactive_opacity * 255));

        // Use the event's rect instead of the whole viewport rect
        // to stay within the current update region (Performance optimization)
        painter.fillRect(event->rect(), dimColor);
    }
}
