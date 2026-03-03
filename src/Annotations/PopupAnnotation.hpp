#pragma once

#include "../Config.hpp"
#include "Annotation.hpp"

#include <QAction>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QObject>
#include <QPainter>
#include <QPainterPath>

class PopupAnnotation : public Annotation
{
    Q_OBJECT

public:
    PopupAnnotation(const Config::Annotations::Popup &config,
                    const QRectF &rect, int index, const QColor &color,
                    const QString &text, QGraphicsItem *parent = nullptr)
        : Annotation(index, color, parent), m_rect(rect), m_config(config)
    {
        m_comment = text;

        setGlowColor(m_config.glow_color);
        setGlowEnabled(m_config.hover_glow);
        setGlowWidth(m_config.glow_width);
        setFlags(flags() | QGraphicsItem::ItemIsFocusable);
        setTooltipFontSize(m_config.comment_font_size);
        if (!m_comment.isEmpty())
        {
            setTooltipFontSize(m_config.comment_font_size);
        }
        // setGlowColor(m_config.glow_color);
    }

    // ------------------------------------------------------------------
    // geometry
    QRectF boundingRect() const override
    {
        const qreal margin = m_glow_width + 2.0;
        return m_iconRect().adjusted(-margin, -margin, margin, margin);
    }

    // ------------------------------------------------------------------
    // painting
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override
    {
        Q_UNUSED(widget);

        painter->setRenderHint(QPainter::Antialiasing);

        const QRectF iconR = m_iconRect();

        // Outer glow — drawn first, underneath everything.
        if (m_hovered && isGlowEnabled())
        {
            painter->save();
            drawGlow(painter, m_rect, m_glow_width);
            painter->restore();
        }

        // 2. Note icon body.
        constexpr qreal foldSize = 6.0;

        QPainterPath notePath;
        notePath.moveTo(iconR.topLeft());
        notePath.lineTo(iconR.topRight() - QPointF(foldSize, 0));
        notePath.lineTo(iconR.topRight() + QPointF(0, foldSize));
        notePath.lineTo(iconR.bottomRight());
        notePath.lineTo(iconR.bottomLeft());
        notePath.closeSubpath();

        QColor fillColor = m_brush.color();
        if (!fillColor.isValid() || fillColor.alpha() == 0)
            fillColor = QColor(255, 255, 0, 200);
        if (m_hovered)
            fillColor = fillColor.lighter(115);

        painter->fillPath(notePath, fillColor);

        // 3. Folded corner.
        QPainterPath foldPath;
        foldPath.moveTo(iconR.topRight() - QPointF(foldSize, 0));
        foldPath.lineTo(iconR.topRight() - QPointF(foldSize, -foldSize));
        foldPath.lineTo(iconR.topRight() + QPointF(0, foldSize));
        foldPath.closeSubpath();
        painter->fillPath(foldPath, fillColor.darker(120));

        // Selection indicator.
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
    }

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override
    {
        QMenu menu;

        QAction *deleteAction = menu.addAction(tr("Delete"));
        QAction *editAction   = menu.addAction(tr("Comment"));

        connect(editAction, &QAction::triggered, this,
                [this] { emit annotCommentRequested(); });
        connect(deleteAction, &QAction::triggered, this,
                [this] { emit annotDeleteRequested(); });

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

private:
    QRectF m_iconRect() const
    {
        constexpr qreal iconSize = 24.0;
        return QRectF(m_rect.topLeft(), QSizeF(iconSize, iconSize));
    }

    QRectF m_rect;
    const Config::Annotations::Popup &m_config;
};
