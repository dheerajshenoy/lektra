#pragma once

#include <QElapsedTimer>
#include <QGestureEvent>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QRubberBand>
#include <QScrollBar>
#include <QTimer>

struct Config;

class GraphicsPixmapItem;

class GraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    enum class Mode
    {
        None = 0,
        VisualLine,
        RegionSelection,
        TextSelection,
        TextHighlight,
        AnnotSelect,
        AnnotRect,
        AnnotPopup,
        COUNT
    };

    enum class MouseAction
    {
        None = 0,
#ifdef WITH_SYNCTEX
        SynctexJump,
#endif
        Portal,
        Preview,
        Pan,
    };

    explicit GraphicsView(const Config &config, QWidget *parent = nullptr);

    inline void set_visual_line_rect(const QRectF &sceneRect)
    {
        m_visual_line_rect = sceneRect;
        viewport()->update();
    }

    inline void setNarrowRect(const QRectF &sceneRect) noexcept
    {
        m_narrow_scene_rect = sceneRect;
        m_is_narrow_clip    = true;
        viewport()->update();
    }

    inline void clearNarrowRect() noexcept
    {
        m_narrow_scene_rect = {};
        m_is_narrow_clip    = false;
        viewport()->update();
    }

    inline bool isNarrowClip() const noexcept
    {
        return m_is_narrow_clip;
    }

    inline void setPortal(bool state) noexcept
    {
        m_is_portal = state;
    }

    inline bool isPortal() const noexcept
    {
        return m_is_portal;
    }

    inline const Config &config() const noexcept
    {
        return m_config;
    }

    inline QPointF selectionStart() const noexcept
    {
        return m_selection_start;
    }

    inline Mode mode() const noexcept
    {
        return m_mode;
    }

    inline void setSelectionDragThreshold(int value) noexcept
    {
        m_drag_threshold = value;
    }

    inline QPointF getCursorPos() const noexcept
    {
        return mapToScene(mapFromGlobal(QCursor::pos()));
    }

    inline Mode getNextMode() noexcept
    {
        return static_cast<Mode>((static_cast<int>(m_mode) + 1)
                                 % static_cast<int>(Mode::COUNT));
    }

    inline void setDefaultMode(Mode mode) noexcept
    {
        m_default_mode = mode;
    }

    inline Mode getDefaultMode() const noexcept
    {
        return m_default_mode;
    }

    inline void setScrollbarIdleTimeout(int ms)
    {
        m_scrollbar_hide_timer.setInterval(ms);
    }

    inline void setScrollbarSize(int size) noexcept
    {
        m_scrollbarSize = size;
    }

    inline void flashScrollbars()
    {
        showScrollbars();
        // Force viewport update to recalculate scrollbar ranges
        if (viewport())
            viewport()->update();
        // Delay layout to let Qt process the update and recalculate ranges
        QTimer::singleShot(0, this, [this]()
        {
            updateScrollbars();
            restartHideTimer();
        });
    }

    inline void setVerticalScrollbarEnabled(bool enabled) noexcept
    {
        if (m_vbarEnabled != enabled)
        {
            m_vbarEnabled = enabled;
            updateScrollbars();
        }
    }

    inline void setHorizontalScrollbarEnabled(bool enabled) noexcept
    {
        if (m_hbarEnabled != enabled)
        {
            m_hbarEnabled = enabled;
            updateScrollbars();
        }
    }

    void setMode(Mode mode) noexcept;
    void setAutoHideScrollbars(bool enabled);

    void bindScrollbarActivity(QScrollBar *vertical,
                               QScrollBar *horizontal) noexcept;
    void clearRubberBand() noexcept;
    void applyBackend() noexcept;

signals:
    void textSelectionRequested(QPointF a, QPointF b);
    void textHighlightRequested(QPointF a, QPointF b);
    void linkCtrlClickRequested(QPointF scenePos);
    void linkPreviewRequested(QPointF scenePos);
    void annotRectRequested(QRectF sceneRect);
    void annotPopupRequested(QPointF scenePos);
    void regionSelectRequested(QRectF sceneRect);
    void annotSelectRequested(QRectF sceneRect);
    void annotSelectRequested(QPointF scenePos);
    void annotSelectClearRequested();
    void zoomInRequested();
    void zoomRequested(float factor, QPointF anchorScenePos);
    void textSelectionDeletionRequested();
    void zoomOutRequested();
    void contextMenuRequested(QPoint globalPos, bool *handled);
    void clickRequested(int count, QPointF scenePos);
    void rightClickRequested(QPointF scenePos);
    void smartJumpRequested(QPointF scenePos);

#ifdef WITH_SYNCTEX
    void synctexJumpRequested(QPointF scenePos);
#endif

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool viewportEvent(QEvent *event) override;

private slots:
    bool handleTouchpadGesture(QNativeGestureEvent *event);

private:
    inline void setActive(bool state) noexcept
    {
        m_is_active = state;
    }

    inline bool isActive() const noexcept
    {
        return m_is_active;
    }

    inline void showScrollbars()
    {
        if (!m_scrollbarsVisible)
        {
            m_scrollbarsVisible = true;
            updateScrollbars();
        }
    }

    inline void hideScrollbars()
    {
        if (m_scrollbarsVisible && !m_scrollbarsPinned)
        {
            m_scrollbarsVisible = false;
            updateScrollbars();
        }
    }

    inline void setScrollbarsPinned(bool pinned)
    {
        m_scrollbarsPinned = pinned;
        if (pinned)
            showScrollbars();
    }

    inline void restartHideTimer()
    {
        if (m_autoHide && !m_activeScrollbar)
            m_scrollbar_hide_timer.start();
    }

    // Utility
    inline QPointF scenePosFromEvent(QMouseEvent *event) const
    {
        return mapToScene(event->pos());
    }

    void updateCursorForMode() noexcept;
    void onScrollbarActivity() noexcept;
    void updateScrollbars();
    void layoutScrollbars();
    QScrollBar *scrollbarAt(const QPoint &pos) const noexcept;
    void forwardMouseEvent(QScrollBar *bar, QMouseEvent *event);
    MouseAction resolveMouseAction(Qt::MouseButton button,
                                   Qt::KeyboardModifiers mods) const noexcept;
    bool updateAutoScroll(const QPoint &pos) noexcept;
    void applyAutoScroll() noexcept;
    void stopAutoScroll() noexcept;

    QRect m_rect;
    QPoint m_start;
    QPointF m_mousePressPos;
    QPointF m_selection_start;
    QPoint m_last_mouse_pos;

    // Multi-click tracking
    QElapsedTimer m_clickTimer;
    QPointF m_lastClickPos;
    QPoint m_lastMovePos;

    QPoint m_lastPanPos;

    bool m_panning                                   = false;
    bool m_selecting                                 = false;
    bool m_dragging                                  = false;
    bool m_ignore_next_release                       = false;
    Mode m_mode                                      = Mode::TextSelection;
    Mode m_default_mode                              = Mode::None;
    QRubberBand *m_rubberBand                        = nullptr;
    int m_drag_threshold                             = 50;
    int m_clickCount                                 = 0;
    static constexpr int SCROLLBAR_MARGIN            = 2;
    static constexpr int MULTI_CLICK_INTERVAL        = 400;
    static constexpr double CLICK_DISTANCE_THRESHOLD = 5.0;
    static constexpr int MOVE_EMIT_THRESHOLD_PX      = 2;
    // ~12% “gesture energy” per step (tweak)
    static constexpr qreal ZOOM_STEP_TRIGGER         = 0.12;
    // pixels of trackpad scroll per page (tweak)
    static constexpr qreal PAGE_SCROLL_TRIGGER       = 900.0;
    // Gesture state
    qreal m_lastPinchScale                           = 1.0;
    // accumulate pinch/native zoom deltas to trigger steps
    qreal m_zoomAccum                                = 0.0;
    // accumulate trackpad scroll to trigger page flips
    qreal m_scrollAccumY                             = 0.0;
    // tracks if we're in a native gesture sequence
    bool m_inNativeGesture                           = false;
    QScrollBar *m_activeScrollbar                    = nullptr;
    int m_scrollbarSize                              = 12;
    bool m_autoHide                                  = false;
    bool m_scrollbarsVisible                         = false;
    bool m_scrollbarsPinned                          = false;
    bool m_vbarEnabled                               = true;
    bool m_hbarEnabled                               = true;
    bool m_is_active                                 = false;
    bool m_is_portal                                 = false;
    bool m_is_narrow_clip                            = false;
    QRectF m_narrow_scene_rect;
    const Config &m_config;
    QRectF m_visual_line_rect;
    QTimer m_scrollbar_hide_timer;
    QTimer m_autoscroll_timer;
    int m_autoscroll_dx = 0;
    int m_autoscroll_dy = 0;

    friend class DocumentView;
};
