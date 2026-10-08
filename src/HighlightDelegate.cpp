#include "HighlightDelegate.hpp"

#include <QApplication>
#include <QPainter>

// HighlightDelegate.cpp (paint)
void
HighlightDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    QString text = opt.text;
    opt.text.clear();

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    QVector<QPair<int, int>> ranges;
    if (m_highlighter && m_highlighter->isActive())
        ranges = m_highlighter->ranges(text);

    if (ranges.isEmpty())
    {
        style->drawItemText(painter, opt.rect, opt.displayAlignment,
                            opt.palette, true, text, QPalette::Text);
        return;
    }

    // --- same per-piece drawing as before ---
    painter->save();
    QFontMetrics fm(opt.font);
    int x = opt.rect.left() + 4;
    int y = opt.rect.top() + (opt.rect.height() - fm.height()) / 2;

    int pos = 0;
    for (const auto &r : ranges)
    {
        if (r.first > pos)
        {
            const QString pre = text.mid(pos, r.first - pos);
            painter->setPen(opt.palette.color(QPalette::Text));
            painter->drawText(x, y + fm.ascent(), pre);
            x += fm.horizontalAdvance(pre);
        }
        const QString hit = text.mid(r.first, r.second);
        painter->fillRect(QRect(x, y, fm.horizontalAdvance(hit), fm.height()),
                          m_color);
        painter->setPen(opt.palette.color(QPalette::Text));
        painter->drawText(x, y + fm.ascent(), hit);
        x += fm.horizontalAdvance(hit);
        pos = r.first + r.second;
    }
    if (pos < text.size())
    {
        painter->setPen(opt.palette.color(QPalette::Text));
        painter->drawText(x, y + fm.ascent(), text.mid(pos));
    }
    painter->restore();
}
