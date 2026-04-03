#pragma once

#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPainterPath>

/**
 * @brief A small speech-bubble button that floats near the top-right corner
 *        of a parent annotation when hovered.
 *
 * Usage:
 *   auto *btn = new CommentPopupButton(parentAnnotationItem);
 *   btn->setAnnotationRect(m_rect);   // call whenever m_rect changes
 *   connect(btn, &CommentPopupButton::clicked, this,
 * &MyClass::onCommentClicked);
 *
 * Visibility is controlled by the parent — call show()/hide() from the
 * annotation's hoverEnterEvent / hoverLeaveEvent.
 */
class CommentPopupButton : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    explicit CommentPopupButton(QGraphicsItem *parent)
        : QObject(), QGraphicsItem(parent)
    {
        setAcceptHoverEvents(true);
        hide(); // invisible until hover
    }

    /** Reposition the button relative to the annotation rect. */
    void setAnnotationRect(const QRectF &annotRect)
    {
        // Anchor to top-right corner, shifted outward by a small gap.
        constexpr qreal gap = 3.0;
        setPos(annotRect.right() + gap, annotRect.top());
    }

    QRectF boundingRect() const override
    {
        // 1px margin for the drop-shadow / AA bleed.
        return QRectF(-1, -1, SIZE + 2, SIZE + 2);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/,
               QWidget * /*widget*/) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QRectF body(0, 0, SIZE, SIZE * 0.75);
        constexpr qreal R = 3.0;

        // Bubble body
        QPainterPath path;
        path.addRoundedRect(body, R, R);

        // Drop shadow
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 40));
        painter->drawPath(path.translated(1, 1));

        // Fill
        const QColor fill = m_pressed   ? QColor(40, 100, 210, 230)
                            : m_hovered ? QColor(80, 150, 255, 230)
                                        : QColor(60, 120, 240, 210);
        painter->setBrush(fill);
        painter->setPen(QPen(QColor(30, 80, 180, 200), 1.0));
        painter->drawPath(path);

        // Three dots (ellipsis) to hint "there is text inside"
        painter->setPen(Qt::NoPen);
        painter->setBrush(Qt::white);
        const qreal cy   = body.center().y() - 1.0;
        const qreal dotR = 1.5;
        const qreal cx   = body.center().x();

        painter->drawEllipse(QPointF(cx - 4, cy), dotR, dotR);
        painter->drawEllipse(QPointF(cx, cy), dotR, dotR);
        painter->drawEllipse(QPointF(cx + 4, cy), dotR, dotR);

        painter->restore();
    }

signals:
    void clicked();

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *e) override
    {
        m_hovered = true;
        update();
        QGraphicsItem::hoverEnterEvent(e);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *e) override
    {
        m_hovered = false;
        m_pressed = false;
        update();
        QGraphicsItem::hoverLeaveEvent(e);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
        {
            m_pressed = true;
            update();
            e->accept();
        }
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_pressed)
        {
            m_pressed = false;
            update();
            if (boundingRect().contains(e->pos()))
                emit clicked();
            e->accept();
        }
    }

private:
    static constexpr qreal SIZE
        = 20.0; // button width; height = SIZE * 0.75 body + tail

    bool m_hovered{false};
    bool m_pressed{false};
};
