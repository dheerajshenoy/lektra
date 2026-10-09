#pragma once

#include "Annotation.hpp"
#include "Config.hpp"

#include <QAction>
#include <QGraphicsItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QObject>
#include <QPainter>
#include <QPainterPath>

// The overlay of an ellipse, a polygon or an inline note. The shape itself is
// in the page's appearance; this item only gives it hover, comment and
// selection. Uses the options of the rectangle annotation.
class ShapeAnnotation : public Annotation
{
    Q_OBJECT

public:
    ShapeAnnotation(const Config::Annotations::Rect &config, Type type,
                    const QRectF &rect, int index, const QString &comment,
                    const QColor &color, QGraphicsItem *parent = nullptr)
        : Annotation(index, color, parent), m_type(type), m_rect(rect)
    {
        m_comment = comment;
        setGlowEnabled(config.hover_glow);
        setGlowWidth(config.glow_width);
        setGlowColor(config.glow_color);
        setFlags(flags() | QGraphicsItem::ItemIsFocusable);
        setTooltipFontSize(config.comment_font_size);

        // An inline note's text is already on the page.
        if (m_type == Type::Note)
        {
            m_tooltip_enabled = false;
            setCommentMarkerVisible(false);
        }
        else
        {
            setCommentMarkerVisible(config.comment_marker);
        }
        updateCommentMarker();
    }

    inline Type atype() const noexcept override
    {
        return m_type;
    }

    bool canTransform() const noexcept override
    {
        return true;
    }

    bool canResize() const noexcept override
    {
        return true;
    }

    QRectF geometryRect() const override
    {
        return m_rect;
    }

    QRectF boundingRect() const override
    {
        const qreal margin = std::max<qreal>(m_glow_width + 2.0, 12.0);
        const QRectF r = m_preview_rect.isValid()
                             ? m_rect.united(m_preview_rect)
                             : m_rect;
        return r.adjusted(-margin, -margin, margin, margin);
    }

    QPainterPath shape() const override
    {
        QPainterPath path;
        if (m_type == Type::Ellipse)
            path.addEllipse(m_rect);
        else
            path.addRect(m_rect);
        return path;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override
    {
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

        paintManipulation(painter, option);

        Q_UNUSED(widget);
    }

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override
    {
        QMenu menu;

        QAction *deleteAction = menu.addAction(tr("Delete"));
        QAction *changeColorAction
            = menu.addAction(m_type == Type::Note ? tr("Change Background")
                                                  : tr("Change Color"));
        QAction *commentAction
            = menu.addAction(m_type == Type::Note ? tr("Edit Text")
                                                  : tr("Comment"));

        connect(deleteAction, &QAction::triggered, this,
                [this] { emit annotDeleteRequested(); });
        connect(changeColorAction, &QAction::triggered, this,
                [this] { emit annotColorChangeRequested(); });
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
    Type m_type;
    QRectF m_rect;
};
