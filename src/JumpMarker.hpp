#pragma once

#include <QGraphicsObject>
#include <QPainter>
#include <QPropertyAnimation>
#include <qabstractanimation.h>
#include <qgraphicsitem.h>

class JumpMarker : public QGraphicsObject
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit JumpMarker(const QColor &color, QGraphicsItem *parent = nullptr)
        : QGraphicsObject(parent), m_color(color)
    {
        setOpacity(1.0);
        hide();

        m_fade_animation = new QPropertyAnimation(this, "opacity", this);
        m_fade_animation->setDuration(1000); // 1 second
        m_fade_animation->setStartValue(1.0);
        m_fade_animation->setEndValue(0.0);
        m_fade_animation->setEasingCurve(QEasingCurve::OutQuad);

        connect(m_fade_animation, &QPropertyAnimation::finished, [this]()
        {
            hide();
            setOpacity(1.0);
        });
    }

    inline QRectF boundingRect() const override
    {
        return QRectF(-10, -10, 20, 20);
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        p->setPen(Qt::NoPen);
        p->setBrush(m_color);
        p->drawRect(boundingRect());
    }

    void showAt(float x, float y) noexcept
    {
        if (m_fade_animation->state() == QAbstractAnimation::Running)
        {
            m_fade_animation->stop();
        }

        setPos(x, y);
        setOpacity(1.0);
        show();
        m_fade_animation->start();
    }

    void showAt(QPointF p) noexcept
    {
        if (m_fade_animation->state() == QAbstractAnimation::Running)
        {
            m_fade_animation->stop();
        }

        setPos(p);
        setOpacity(1.0);
        show();
        m_fade_animation->start();
    }

private:
    QColor m_color;
    QPropertyAnimation *m_fade_animation{nullptr};
};
