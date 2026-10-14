#pragma once

#include "Annotation.hpp"
#include "Config.hpp"

#include <QAction>
#include <QGraphicsItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

// The overlay of an image on the page. The image itself is in the page's
// appearance; this item only gives it hover, comment, selection and the handles
// to move, resize and turn it. Uses the options of the rectangle annotation.
class ImageAnnotation : public Annotation
{
    Q_OBJECT

public:
    // `rect` is the image as it is before it is turned by `angle` (degrees,
    // clockwise) around its centre.
    ImageAnnotation(const Config::Annotations::Rect &config, const QRectF &rect,
                    qreal angle, int index, const QString &comment,
                    QGraphicsItem *parent = nullptr)
        : Annotation(index, Qt::transparent, parent), m_rect(rect),
          m_angle(angle)
    {
        m_comment = comment;
        setGlowEnabled(config.hover_glow);
        setGlowWidth(config.glow_width);
        setGlowColor(config.glow_color);
        setFlags(flags() | QGraphicsItem::ItemIsFocusable);
        setTooltipFontSize(config.comment_font_size);
        setCommentMarkerVisible(config.comment_marker);
        updateCommentMarker();
    }

    inline Type atype() const noexcept override
    {
        return Type::Image;
    }

    bool canTransform() const noexcept override
    {
        return true;
    }

    bool canResize() const noexcept override
    {
        return true;
    }

    bool canRotate() const noexcept override
    {
        return true;
    }

    qreal rotation() const noexcept override
    {
        return m_angle;
    }

    QRectF geometryRect() const override
    {
        return m_rect;
    }

    QRectF boundingRect() const override
    {
        // The handle that turns it sticks out above.
        const qreal margin = std::max<qreal>(m_glow_width + 2.0, 12.0) + 40.0;
        QRectF r           = turned(m_rect, m_angle).boundingRect();
        if (m_preview_rect.isValid())
            r = r.united(
                turned(m_preview_rect, m_preview_angle).boundingRect());
        return r.adjusted(-margin, -margin, margin, margin);
    }

    QPainterPath shape() const override
    {
        QPainterPath path;
        path.addPolygon(turned(m_rect, m_angle));
        path.closeSubpath();
        return path;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override
    {
        painter->save();
        painter->translate(m_rect.center());
        painter->rotate(m_angle);
        painter->translate(-m_rect.center());

        if (m_hovered && isGlowEnabled())
        {
            painter->save();
            drawGlow(painter, m_rect, m_glow_width);
            painter->restore();
        }

        if (option->state & QStyle::State_Selected)
        {
            painter->save();
            QPen selPen(Qt::SolidLine);
            selPen.setColor(Qt::black);
            selPen.setCosmetic(true);
            painter->setPen(selPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(m_rect);
            painter->restore();
        }
        painter->restore();

        paintManipulation(painter, option);

        Q_UNUSED(widget);
    }

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override
    {
        QMenu menu;

        QAction *deleteAction  = menu.addAction(tr("Delete"));
        QAction *commentAction = menu.addAction(tr("Comment"));

        connect(deleteAction, &QAction::triggered, this,
                [this] { emit annotDeleteRequested(); });
        connect(commentAction, &QAction::triggered, this,
                [this] { emit annotCommentRequested(); });

        // See RectAnnotation::contextMenuEvent.
        m_context_menu_open = true;
        menu.exec(e->screenPos());
        m_context_menu_open = false;

        if (!isUnderMouse())
        {
            m_hovered = false;
            hideTooltip();
            update();
        }

        e->accept();
    }

    void setComment(const QString &comment) override
    {
        Annotation::setComment(comment);
        updateCommentMarker();
    }

private:
    // `rect` turned by `angle` around its centre.
    static QPolygonF turned(const QRectF &rect, qreal angle)
    {
        QTransform t;
        t.translate(rect.center().x(), rect.center().y());
        t.rotate(angle);
        t.translate(-rect.center().x(), -rect.center().y());
        return t.map(QPolygonF(rect));
    }

    QRectF m_rect;
    qreal m_angle;
};
