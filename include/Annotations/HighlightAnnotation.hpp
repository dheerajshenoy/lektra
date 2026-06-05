#pragma once

#include "Annotation.hpp"
#include "CommentPopupButton.hpp"
#include "Config.hpp"

#include <vector>

#include <QAction>
#include <QGraphicsItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QObject>
#include <QPainter>

class HighlightAnnotation : public Annotation
{
    Q_OBJECT

public:
    HighlightAnnotation(const Config::Annotations::Highlight &config,
                        const QRectF &rect, int index,
                        const QString &comment = {},
                        const std::vector<QRectF> &rects = {},
                        QGraphicsItem *parent  = nullptr)
        : Annotation(index, QColor(Qt::transparent), parent), m_rect(rect),
          m_rects(rects), m_config(config)
    {
        m_comment = comment;
        setGlowEnabled(m_config.hover_glow);
        setGlowWidth(m_config.glow_width);
        setGlowColor(m_config.glow_color);
        setFlags(flags() | QGraphicsItem::ItemIsFocusable);
        setTooltipFontSize(m_config.comment_font_size);
        setCommentMarkerVisible(m_config.comment_marker);
        updateCommentMarker();
    }

    inline Type atype() const noexcept override
    {
        return Type::Highlight;
    }

    QRectF boundingRect() const override
    {
        const qreal margin = m_glow_width + 2.0;
        return m_rect.adjusted(-margin, -margin, margin, margin);
    }

    QPainterPath shape() const override
    {
        QPainterPath path;
        if (m_rects.empty())
            path.addRect(m_rect);
        else
            for (const QRectF &r : m_rects)
                path.addRect(r);
        return path;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override
    {
        Q_UNUSED(widget);

        auto paintRects = [&](auto fn) {
            if (m_rects.empty())
                fn(m_rect);
            else
                for (const QRectF &r : m_rects)
                    fn(r);
        };

        if (m_hovered && isGlowEnabled())
        {
            painter->save();
            paintRects([&](const QRectF &r) { drawGlow(painter, r, m_glow_width); });
            painter->restore();
        }

        painter->setPen(m_pen);
        painter->setBrush(m_brush);
        paintRects([&](const QRectF &r) { painter->drawRect(r); });

        if (option->state & QStyle::State_Selected)
        {
            painter->save();
            QPen selPen(Qt::SolidLine);
            selPen.setColor(Qt::black);
            selPen.setCosmetic(true);
            painter->setPen(selPen);
            painter->setBrush(Qt::NoBrush);
            paintRects([&](const QRectF &r) { painter->drawRect(r); });
            painter->restore();
        }
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
        {
            setSelected(true);
            emit annotSelectRequested();
        }
        QGraphicsItem::mousePressEvent(e);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override
    {
        QMenu menu;

        QAction *copyTextAction    = menu.addAction(tr("Copy Text"));
        QAction *deleteAction      = menu.addAction(tr("Delete"));
        QAction *changeColorAction = menu.addAction(tr("Change Color"));
        QAction *commentAction     = menu.addAction(tr("Comment"));

        connect(copyTextAction, &QAction::triggered, this,
                [this] { emit annotCopyTextRequested(); });
        connect(deleteAction, &QAction::triggered, this,
                [this] { emit annotDeleteRequested(); });
        connect(changeColorAction, &QAction::triggered, this,
                [this] { emit annotColorChangeRequested(); });
        connect(commentAction, &QAction::triggered, this,
                [this] { emit annotCommentRequested(); });

        // Bracket exec() with the flag so hoverLeaveEvent (fired by Qt when
        // the menu window grabs the mouse) does not clear m_hovered and
        // extinguish the glow while the user reads the menu.
        m_context_menu_open = true;
        menu.exec(e->screenPos());
        m_context_menu_open = false;

        // If the cursor genuinely left the item while the menu was open,
        // honour that now that we are back in control.
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
    QRectF m_rect;
    std::vector<QRectF> m_rects;
    const Config::Annotations::Highlight &m_config;
};
