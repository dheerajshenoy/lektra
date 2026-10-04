#include "StatusbarLayout.hpp"

#include <QWidget>
#include <algorithm>
#include <climits>
#include <set>

StatusbarLayout::StatusbarLayout(QWidget *parent) : QLayout(parent)
{
    setContentsMargins(0, 0, 0, 0);
}

StatusbarLayout::~StatusbarLayout()
{
    clear();
}

void
StatusbarLayout::addWidgetTo(int row, QWidget *widget, const Spec &spec)
{
    addChildWidget(widget);
    m_entries.append({new QWidgetItem(widget), row, spec});
    invalidate();
}

void
StatusbarLayout::addGapTo(int row, const Spec &spec)
{
    Spec s = spec;
    s.gap  = true;
    m_entries.append({new QSpacerItem(0, 0), row, s});
    invalidate();
}

void
StatusbarLayout::clear()
{
    for (const Entry &e : std::as_const(m_entries))
        delete e.item;
    m_entries.clear();
    invalidate();
}

void
StatusbarLayout::addItem(QLayoutItem *item)
{
    m_entries.append({item, 0, Spec{}});
    invalidate();
}

QLayoutItem *
StatusbarLayout::itemAt(int index) const
{
    return index >= 0 && index < m_entries.size() ? m_entries.at(index).item
                                                  : nullptr;
}

QLayoutItem *
StatusbarLayout::takeAt(int index)
{
    if (index < 0 || index >= m_entries.size())
        return nullptr;
    invalidate();
    return m_entries.takeAt(index).item;
}

int
StatusbarLayout::count() const
{
    return static_cast<int>(m_entries.size());
}

// An item that is hidden, or a widget that has no size, takes no room.
bool
StatusbarLayout::isEmpty(const Entry &e)
{
    if (e.spec.gap)
        return false;
    if (e.item->isEmpty())
        return true;
    const QSize hint = e.item->sizeHint();
    return hint.width() <= 0 && hint.height() <= 0;
}

int
StatusbarLayout::naturalWidth(const Entry &e)
{
    if (e.spec.gap)
        return e.spec.gapWidth;
    int w = e.item->sizeHint().width();
    w     = std::max(w, e.spec.minWidth);
    if (e.spec.maxWidth > 0)
        w = std::min(w, e.spec.maxWidth);
    return w + e.spec.marginLeft + e.spec.marginRight;
}

int
StatusbarLayout::minimumWidth(const Entry &e)
{
    if (e.spec.gap)
        return e.spec.gapWidth;
    int w = std::max(e.item->minimumSize().width(), e.spec.minWidth);
    if (e.spec.maxWidth > 0)
        w = std::min(w, e.spec.maxWidth);
    return std::min(w, naturalWidth(e) - e.spec.marginLeft - e.spec.marginRight)
           + e.spec.marginLeft + e.spec.marginRight;
}

QList<int>
StatusbarLayout::rowNumbers() const
{
    std::set<int> rows;
    for (const Entry &e : m_entries)
        rows.insert(e.row);
    return QList<int>(rows.begin(), rows.end());
}

int
StatusbarLayout::rowHeight(int row) const
{
    int h = 0;
    for (const Entry &e : m_entries)
        if (e.row == row && !e.spec.gap && !isEmpty(e))
            h = std::max(h, e.item->sizeHint().height());
    return h;
}

QSize
StatusbarLayout::sizeHint() const
{
    int width = 0, height = 0;
    for (int row : rowNumbers())
    {
        int flow = 0;
        for (const Entry &e : m_entries)
        {
            if (e.row != row || isEmpty(e))
                continue;
            if (e.spec.absolute)
                width = std::max(width, naturalWidth(e));
            else
                flow += naturalWidth(e);
        }
        width = std::max(width, flow);
        height += rowHeight(row);
    }
    const QMargins m = contentsMargins();
    return {width + m.left() + m.right(), height + m.top() + m.bottom()};
}

QSize
StatusbarLayout::minimumSize() const
{
    int width = 0, height = 0;
    for (int row : rowNumbers())
    {
        int flow = 0;
        for (const Entry &e : m_entries)
        {
            if (e.row != row || isEmpty(e) || e.spec.absolute)
                continue;
            flow += minimumWidth(e);
        }
        width = std::max(width, flow);
        height += rowHeight(row);
    }
    const QMargins m = contentsMargins();
    return {width + m.left() + m.right(), height + m.top() + m.bottom()};
}

void
StatusbarLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    const QRect area = contentsRect();

    int y = area.y();
    for (int row : rowNumbers())
    {
        const int h = rowHeight(row);
        layoutRow(row, QRect(area.x(), y, area.width(), h));
        y += h;
    }
}

void
StatusbarLayout::layoutRow(int row, const QRect &rect)
{
    struct Slot
    {
        const Entry *entry;
        int width;
        int minimum;
    };
    QList<Slot> flow;
    QList<const Entry *> absolute;
    int total = 0;
    for (const Entry &e : std::as_const(m_entries))
    {
        if (e.row != row || isEmpty(e))
            continue;
        if (e.spec.absolute)
        {
            absolute.append(&e);
            continue;
        }
        flow.append({&e, naturalWidth(e), minimumWidth(e)});
        total += flow.last().width;
    }

    // Share out the free space, or take back what is missing.
    int extra = rect.width() - total;
    if (extra > 0)
    {
        // Items with a maximum width stop growing; the rest share what is left.
        QList<int> growing;
        for (int i = 0; i < flow.size(); ++i)
            if (flow[i].entry->spec.stretch > 0)
                growing.append(i);
        while (extra > 0 && !growing.isEmpty())
        {
            double weight = 0;
            for (int i : growing)
                weight += flow[i].entry->spec.stretch;

            QList<int> still;
            int used = 0;
            for (int i : growing)
            {
                Slot &s          = flow[i];
                const Spec &spec = s.entry->spec;
                const int give   = static_cast<int>(extra * spec.stretch / weight);
                int cap          = INT_MAX;
                if (!spec.gap && spec.maxWidth > 0)
                    cap = spec.maxWidth + spec.marginLeft + spec.marginRight
                          - s.width;
                const int take = std::min(give, cap);
                s.width += take;
                used += take;
                if (give <= cap)
                    still.append(i);
            }
            const bool capped = still.size() != growing.size();
            extra -= used;
            growing = still;
            if (!capped)
                break;
        }
    }
    else if (extra < 0)
    {
        int shrinkable = 0;
        for (const Slot &s : flow)
            shrinkable += std::max(0, s.width - s.minimum);
        if (shrinkable > 0)
        {
            const int deficit = std::min(-extra, shrinkable);
            int taken         = 0;
            for (int i = 0; i < flow.size(); ++i)
            {
                Slot &s        = flow[i];
                const int room = std::max(0, s.width - s.minimum);
                int cut        = static_cast<int>(
                    static_cast<double>(deficit) * room / shrinkable);
                s.width -= cut;
                taken += cut;
            }
            // Rounding: take the last pixels from whoever can still give them.
            for (int i = 0; taken < deficit && i < flow.size(); ++i)
                if (flow[i].width > flow[i].minimum)
                {
                    --flow[i].width;
                    ++taken;
                    --i; // same item again until it cannot
                }
        }
    }

    auto place = [&](const Entry &e, int x, int slotWidth)
    {
        const Spec &spec = e.spec;
        const QSize hint = e.item->sizeHint();
        int contentW     = slotWidth - spec.marginLeft - spec.marginRight;
        int maxW         = e.item->maximumSize().width();
        if (spec.maxWidth > 0)
            maxW = std::min(maxW, spec.maxWidth);
        int w = std::min({contentW, maxW});
        // A slot that was not stretched is exactly as wide as the widget wants.
        if (spec.stretch <= 0)
            w = std::min(contentW, std::max(hint.width(), spec.minWidth));
        w = std::max(0, w);

        int cx = x + spec.marginLeft;
        if (contentW > w)
        {
            if (spec.align & Qt::AlignHCenter)
                cx += (contentW - w) / 2;
            else if (spec.align & Qt::AlignRight)
                cx += contentW - w;
        }
        const int h  = std::min(rect.height(), hint.height());
        const int cy = rect.y() + (rect.height() - h) / 2;
        e.item->setGeometry(QRect(cx, cy, w, h));
    };

    int x = rect.x();
    for (const Slot &s : std::as_const(flow))
    {
        if (!s.entry->spec.gap)
            place(*s.entry, x, s.width);
        x += s.width;
    }

    for (const Entry *e : std::as_const(absolute))
    {
        const int w = std::min(rect.width(), naturalWidth(*e));
        int left    = rect.x() + static_cast<int>(e->spec.at * rect.width());
        if (e->spec.anchor & Qt::AlignHCenter)
            left -= w / 2;
        else if (e->spec.anchor & Qt::AlignRight)
            left -= w;
        left = std::clamp(left, rect.x(), rect.x() + std::max(0, rect.width() - w));
        place(*e, left, w);
    }
}
