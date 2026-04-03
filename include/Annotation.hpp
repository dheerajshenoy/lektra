#pragma once

#include "CommentPopupButton.hpp"

#include <QBrush>
#include <QGraphicsItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLabel>
#include <QObject>
#include <QPen>
#include <qstyleoption.h>

/**
 * @brief A selectable, hoverable rectangular annotation item for a
 * QGraphicsScene.
 *
 * Supports an optional outer glow on hover, and a floating tooltip when
 * the item has an associated comment string.
 *
 * ## Glow during context menu
 * Qt fires hoverLeaveEvent when a context menu grabs the mouse, which would
 * normally clear State_MouseOver and extinguish the glow mid-interaction.
 * To prevent this, the base class tracks hover state in m_hovered independently
 * of the style option. contextMenuEvent sets m_context_menu_open = true before
 * exec() blocks, so hoverLeaveEvent knows not to clear m_hovered. Once the menu
 * returns, m_context_menu_open is cleared and a repaint is scheduled.
 */
class Annotation : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)

public:
    explicit Annotation(int index, const QColor &color,
                        QGraphicsItem *parent = nullptr);

    ~Annotation() override;

    inline int index() const noexcept
    {
        return m_index;
    }

    enum class Type
    {
        Highlight = 0,
        Popup,
        Rect,
        COUNT
    };

    virtual Type atype() const noexcept = 0;

    /** Restore brush and pen to the values they had at construction time. */
    void restoreBrushPen() noexcept;

    /** Enable or disable the hover glow outline. */
    void setGlowEnabled(bool enable) noexcept;

    inline bool isGlowEnabled() const noexcept
    {
        return m_glow_enabled;
    }

    inline QColor color() const noexcept
    {
        return m_brush.color();
    }

    void setGlowWidth(int width) noexcept;

    inline int glowWidth() const noexcept
    {
        return m_glow_width;
    }

    void setGlowColor(uint32_t color) noexcept;

    inline const QColor &glowColor() const noexcept
    {
        return m_glow_color;
    }

    inline const QString &comment() const noexcept
    {
        return m_comment;
    }

    inline bool hasComment() const noexcept
    {
        return !m_comment.isEmpty();
    }

    void setCommentMarkerVisible(bool visible) noexcept;
    inline bool isCommentMarkerVisible() const noexcept
    {
        return m_comment_marker_visible;
    }

    inline void updateCommentMarker() noexcept
    {
        if (m_comment_marker)
        {
            m_comment_marker->setVisible(m_comment_marker_visible
                                         && hasComment());
            update();
        }
    }

    /** Set the annotation comment; also updates the tooltip text if visible. */
    virtual void setComment(const QString &comment);

    // QGraphicsItem interface
    /**
     * Returns a null rect. Concrete subclasses must override and expand
     * outward by at least (m_glow_width + 2) on each side so the outer
     * glow is not clipped by the scene.
     */
    QRectF boundingRect() const override;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

signals:
    void annotDeleteRequested();
    void annotColorChangeRequested();
    void annotSelectRequested();
    void annotCommentRequested();

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

    // ------------------------------------------------------------------
    // context menu Base implementation brackets menu.exec() with
    // m_context_menu_open so that hoverLeaveEvent (which Qt fires when the menu
    // steals the mouse) does not clear m_hovered or hide the glow. Subclasses
    // that override this MUST call Annotation::contextMenuEvent, or replicate
    // the m_context_menu_open bracketing themselves.
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void drawGlow(QPainter *painter, const QRectF &rect,
                  qreal width) const noexcept;
    void showTooltip(const QPoint &screenPos);
    void moveTooltip(const QPoint &screenPos);
    void setTooltipFontSize(int pointSize);
    void hideTooltip();

    CommentPopupButton *m_comment_marker{nullptr};
    int m_index{-1};
    QBrush m_brush{Qt::transparent};
    QPen m_pen{Qt::NoPen};
    QBrush m_originalBrush;
    QPen m_originalPen;

    QString m_comment;

    bool m_glow_enabled{false};
    int m_glow_width{6};

    // Independent hover flag — not derived from QStyle::State_MouseOver.
    // This is the authoritative source for whether the glow should be drawn.
    bool m_hovered{false};

    // Set to true for the duration of menu.exec() so hoverLeaveEvent
    // (fired by Qt when the menu grabs the mouse) does not clear m_hovered.
    bool m_context_menu_open{false};

    bool m_comment_marker_visible{true};

private:
    QColor m_glow_color{QColor::fromRgba(0xFFFFFF00)};
    QLabel *m_tooltip{nullptr};
    int m_tooltip_font_size{12};
    Type m_atype{Type::COUNT};
};
