#include "GraphicsView.hpp"

#include "Config.hpp"

#include <QApplication>
#include <QGraphicsItem>
#include <QGuiApplication>
#include <QLineF>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QScroller>
#include <qsurfaceformat.h>

GraphicsView::GraphicsView(const Config &config, QWidget *parent)
    : QGraphicsView(parent), m_config(config)
{
    applyBackend();
    setMouseTracking(true);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setAcceptDrops(false);
    setContentsMargins(0, 0, 0, 0);

    setRenderHint(QPainter::Antialiasing, m_config.rendering.antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform,
                  m_config.rendering.smooth_pixmap_transform);
    setRenderHint(QPainter::TextAntialiasing,
                  m_config.rendering.text_antialiasing);

    // Overlay scrollbars - no reserved space
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Hide timer setup
    m_scrollbar_hide_timer.setSingleShot(true);
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

        case Mode::VisualLine:
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
    // Scrollbar
    // TODO: Sometimes the contents are overlay by the scrollbar (e.g., when
    // zoomed in) and texts becomes unclickable.
    if (QScrollBar *bar = scrollbarAt(event->pos()))
    {
        m_activeScrollbar = bar;
        m_scrollbar_hide_timer.stop();
        forwardMouseEvent(bar, event);
        return;
    }

    // Necessary to check mode before button, otherwise right-click context menu
    // won’t work
    // if (event->button() != Qt::LeftButton)
    // {
    //     QGraphicsView::mousePressEvent(event);
    //     return;
    // }

    if (m_mode == Mode::None)
    {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    const MouseAction action
        = resolveMouseAction(event->button(), event->modifiers());

    // Unbound button — let the base class handle it (e.g. right-click context
    // menu).
    if (action == MouseAction::None && event->button() != Qt::LeftButton)
    {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    const QPointF scenePos = mapToScene(event->pos());

    // --- SynctexJump (only valid in TextSelection mode) ---
#ifdef HAS_SYNCTEX
    if (action == MouseAction::SynctexJump && m_mode == Mode::TextSelection)
    {
        emit synctexJumpRequested(scenePos);
        m_ignore_next_release = true;
        event->accept();
        return;
    }
#endif

    // --- Link actions (TextSelection / TextHighlight modes) ---
    if (m_mode == Mode::TextSelection || m_mode == Mode::TextHighlight)
    {
        if (QGraphicsItem *item = itemAt(event->pos()))
        {
            if (item->data(0).toString() == "link")
            {
                if (action == MouseAction::Portal)
                    emit linkCtrlClickRequested(scenePos);
                else if (action == MouseAction::Preview)
                {
                    emit linkPreviewRequested(scenePos);
                }
                else
                    QGraphicsView::mousePressEvent(event);
                event->accept();
                return;
            }
        }
    }

    // --- Select action dispatched per mode ---
    switch (m_mode)
    {
        case Mode::TextSelection:
        {
            // Multi-click tracking
            const bool timerOk
                = m_clickTimer.isValid()
                  && m_clickTimer.elapsed() < MULTI_CLICK_INTERVAL;
            const QPointF d    = event->pos() - m_lastClickPos;
            const double dist2 = d.x() * d.x() + d.y() * d.y();
            m_clickCount       = (timerOk
                            && dist2 < CLICK_DISTANCE_THRESHOLD
                                           * CLICK_DISTANCE_THRESHOLD)
                                     ? qMin(m_clickCount + 1, 4)
                                     : 1;
            m_lastClickPos     = event->pos();
            m_clickTimer.restart();
            emit clickRequested(m_clickCount, scenePos);

            [[fallthrough]];
        }

        case Mode::TextHighlight:
        {
            m_selecting       = true;
            m_mousePressPos   = scenePos;
            m_selection_start = scenePos;
            m_lastMovePos     = event->pos();
            updateCursorForMode();
            event->accept();
            return;
        }

        case Mode::VisualLine:
        {
            emit clickRequested(1, scenePos);
            event->accept();
            return;
        }

        case Mode::AnnotPopup:
        {
            emit annotPopupRequested(scenePos);
            event->accept();
            return;
        }

        case Mode::RegionSelection:
        case Mode::AnnotRect:
        case Mode::AnnotSelect:
            m_start     = event->pos();
            m_rect      = QRect();
            m_dragging  = false;
            m_selecting = true;
            m_rubberBand->setGeometry(QRect(m_start, QSize()));
            m_rubberBand->show();
            event->accept();
            return;

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

    if (m_mode == Mode::None)
    {
        QGraphicsView::mouseMoveEvent(event);
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

        event->accept();
        return; // handled
    }

    // Rubber band modes: no mapToScene needed during drag
    // m_selecting is only set when a bound Select action started the drag,
    // so no need to re-check the button here.
    if ((m_mode == Mode::AnnotSelect || m_mode == Mode::RegionSelection
         || m_mode == Mode::AnnotRect)
        && m_selecting)
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
    if (m_activeScrollbar)
    {
        forwardMouseEvent(m_activeScrollbar, event);
        m_activeScrollbar = nullptr;
        restartHideTimer();
        return;
    }

    // Handle Ignored Release (e.g., after context menu)
    if (m_ignore_next_release)
    {
        m_ignore_next_release = false;
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    // Early exit if not selecting/dragging
    if (m_mode == Mode::None || !m_selecting)
    {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    // Reset state BEFORE emitting signals to prevent recursive triggers
    m_selecting      = false;
    bool wasDragging = m_dragging;
    m_dragging       = false;
    event->accept();

    const QPointF scenePos = mapToScene(event->pos());
    const int dist
        = (scenePos.toPoint() - m_mousePressPos.toPoint()).manhattanLength();
    const bool isDrag = dist > m_drag_threshold;

    // Handle Text Selection / Highlighting Modes
    if (m_mode == Mode::TextSelection || m_mode == Mode::TextHighlight)
    {
        updateCursorForMode();

        if (isDrag)
        {
            if (m_mode == Mode::TextSelection)
            {
                // Only emit if we actually moved from the start point
                // Accept the button that was bound to Select at press time.
                if (m_selection_start != scenePos)
                    emit textSelectionRequested(m_selection_start, scenePos);
            }
            else // Mode::TextHighlight
            {
                // Highlight usually implies a selection was made first
                emit textSelectionRequested(m_selection_start, scenePos);
                emit textHighlightRequested(m_selection_start, scenePos);
            }
        }
        return;
    }

    // Handle Region/Annotation Selection Modes
    if (m_mode == Mode::RegionSelection || m_mode == Mode::AnnotRect
        || m_mode == Mode::AnnotSelect)
    {
        const QRectF sceneRect  = mapToScene(m_rect).boundingRect();
        const bool hasSelection = wasDragging && !sceneRect.isEmpty();

        // Fix: Always clear rubber band if we aren't specifically keeping a
        // region selection visible
        if (m_mode != Mode::RegionSelection || !hasSelection)
            clearRubberBand();

        if (m_mode == Mode::AnnotSelect)
        {
            if (!wasDragging)
                emit annotSelectRequested(scenePos); // Single click selection
            else if (hasSelection)
                emit annotSelectRequested(sceneRect); // Area selection
        }
        else if (hasSelection)
        {
            if (m_mode == Mode::RegionSelection)
                emit regionSelectRequested(sceneRect);
            else if (m_mode == Mode::AnnotRect)
                emit annotRectRequested(sceneRect);
        }

        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void
GraphicsView::wheelEvent(QWheelEvent *event)
{
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
            case QEvent::TouchBegin:
            case QEvent::TouchUpdate:
            case QEvent::TouchEnd:
            case QEvent::NativeGesture:
            case QEvent::Gesture:
            case QEvent::GestureOverride:
            case QEvent::ScrollPrepare:
            case QEvent::Scroll:
                showScrollbars();
                restartHideTimer();
                break;
            default:
                break;
        }
    }

    if (event->type() == QEvent::NativeGesture)
        return handleTouchpadGesture(static_cast<QNativeGestureEvent *>(event));

    return QGraphicsView::viewportEvent(event);
}

void
GraphicsView::enterEvent(QEnterEvent *event)
{
    QGraphicsView::enterEvent(event);
    if (m_autoHide)
    {
        showScrollbars();
        restartHideTimer();
    }
}

void
GraphicsView::leaveEvent(QEvent *event)
{
    QGraphicsView::leaveEvent(event);
    if (m_autoHide)
        restartHideTimer();
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

    QPainter painter(viewport());

    // --- Dimming Logic ---
    bool shouldDim = m_config.split.dim_inactive && !m_is_active;

    // If it's a portal, we override the dimming based on the new config
    if (m_is_portal && !m_config.portal.dim_inactive)
    {
        shouldDim = false;
    }

    if (shouldDim && m_config.split.dim_inactive_opacity > 0)
    {
        QColor dimColor(
            0, 0, 0,
            static_cast<int>(m_config.split.dim_inactive_opacity * 255));
        painter.fillRect(event->rect(), dimColor);
    }

    if (m_mode == Mode::VisualLine)
    {
        if (m_visual_line_rect.isValid())
        {
            if (m_visual_line_rect.isValid())
            {
                QRect viewRect
                    = mapFromScene(m_visual_line_rect).boundingRect();
                viewRect.adjust(-4, -2, 4,
                                2); // left, top, right, bottom padding
                QPainterPath path;
                path.setFillRule(Qt::OddEvenFill);
                path.addRect(viewport()->rect());
                path.addRect(viewRect);
                painter.fillPath(path, QColor(0, 0, 0, 120));
            }
        }
    }

    if (m_config.portal.border_width > 0 && m_config.portal.enabled
        && m_is_portal)
    {
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(m_config.portal.border_color),
                 m_config.portal.border_width);
        painter.setPen(pen);
        painter.drawRect(viewport()->rect().adjusted(1, 1, -1, -1));
    }
}

bool
GraphicsView::handleTouchpadGesture(QNativeGestureEvent *e)
{
    switch (e->gestureType())
    {
        case Qt::ZoomNativeGesture:
        {
            emit zoomRequested(1 + e->value());
            return true;
        }
        break;

        default:
            break;
    }

    return QGraphicsView::viewportEvent(e);
}

void
GraphicsView::applyBackend() noexcept
{
    auto backend_opengl = [this]()
    {
        QSurfaceFormat format;
        format.setSamples(
            m_config.rendering.antialiasing ? 4 : 0); // tie MSAA to your config
        QOpenGLWidget *glWidget = new QOpenGLWidget(this);
        glWidget->setFormat(format);
        setViewport(glWidget);
        setViewportUpdateMode(
            QGraphicsView::FullViewportUpdate); // ← change from Minimal
        setCacheMode(QGraphicsView::CacheBackground);
    };

    auto backend_raster = [this]()
    {
        // setViewport(new QWidget(this)); // Default raster widget
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
        setCacheMode(QGraphicsView::CacheNone);
    };

    switch (m_config.rendering.backend)
    {
        case Config::Rendering::Backend::Raster:
            backend_raster();
            break;

        case Config::Rendering::Backend::OpenGL:
            backend_opengl();
            break;

        case Config::Rendering::Backend::Auto:
        {
            if (QOpenGLContext::supportsThreadedOpenGL())
            {
                backend_opengl();
            }
            else
            {
                backend_raster();
            }
        }
        break;
    }

    // format.setAlphaBufferSize(8); // Enable alpha buffer for transparency
    // setCacheMode(QGraphicsView::CacheBackground);
    // setOptimizationFlags(QGraphicsView::DontAdjustForAntialiasing
    //                      | QGraphicsView::DontSavePainterState);
}

GraphicsView::MouseAction
GraphicsView::resolveMouseAction(Qt::MouseButton button,
                                 Qt::KeyboardModifiers mods) const noexcept
{
    for (const auto &binding : m_config.mousebinds)
    {
        if (binding.isValid() && binding.button == button
            && binding.modifiers == mods)
            return binding.action;
    }
    return MouseAction::None;
}
