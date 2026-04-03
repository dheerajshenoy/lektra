#pragma once

#include <QBrush>
#include <QFont>
#include <QGraphicsItem>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QString>
#include <algorithm>

class LinkHint : public QGraphicsItem
{
public:
    LinkHint(const QRectF &rect, const QColor &bg, const QColor &fg, int hint,
             const float &fontSize)
        : m_rect(rect), m_bg(bg), m_fg(fg), m_hint(hint), m_fontSize(fontSize)
    {
        setData(0, "kb_link_overlay");
        m_hint_text = QString::number(m_hint);
    }

    enum
    {
        Type = QGraphicsItem::UserType + 1
    };

    int type() const override
    {
        return Type;
    }

    QRectF boundingRect() const override
    {
        return m_rect;
    }

    void setInputPrefix(const QString &input) noexcept
    {
        if (input.isEmpty())
        {
            m_prefix_len   = 0;
            m_prefix_match = false;
            setVisible(true);
        }
        else if (m_hint_text.startsWith(input))
        {
            m_prefix_len   = input.size();
            m_prefix_match = (m_prefix_len > 0);
            setVisible(true);
        }
        else
        {
            m_prefix_len   = 0;
            m_prefix_match = false;
            setVisible(false);
        }

        update();
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        painter->save();
        painter->setPen(Qt::PenStyle::SolidLine);
        painter->setBrush(m_bg);
        painter->drawRect(m_rect);

        QFont font;
        font.setPointSizeF(m_fontSize);
        font.setBold(true);
        painter->setFont(font);
        QFontMetricsF metrics(font);

        if (!m_prefix_match || m_prefix_len <= 0)
        {
            painter->setPen(m_fg);
            painter->drawText(m_rect, Qt::AlignCenter, m_hint_text);
            painter->restore();
            return;
        }

        const QString prefix = m_hint_text.left(m_prefix_len);
        const QString suffix = m_hint_text.mid(m_prefix_len);

        const qreal textWidth = metrics.horizontalAdvance(m_hint_text);
        const qreal x         = m_rect.center().x() - textWidth / 2.0;
        const qreal y         = m_rect.center().y()
                        + (metrics.ascent() - metrics.descent()) / 2.0;

        QColor dim = m_fg;
        dim.setAlphaF(std::max(0.2, m_fg.alphaF() * 0.35));

        painter->setPen(dim);
        painter->drawText(QPointF(x, y), prefix);

        const qreal prefixWidth = metrics.horizontalAdvance(prefix);
        painter->setPen(m_fg);
        painter->drawText(QPointF(x + prefixWidth, y), suffix);

        painter->restore();
    }

private:
    QRectF m_rect;
    QColor m_bg;
    QColor m_fg;
    int m_hint;
    float m_fontSize;
    QString m_hint_text;
    int m_prefix_len{0};
    bool m_prefix_match{false};
};
