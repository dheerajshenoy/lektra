#include "ScrollBar.hpp"

ScrollBar::ScrollBar(Qt::Orientation o, QWidget *parent) noexcept
    : QScrollBar(o, parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setContextMenuPolicy(Qt::NoContextMenu);
    // applyStyle();
}

void
ScrollBar::setSize(int size) noexcept
{
    if (m_size != size)
    {
        m_size = size;
        // applyStyle();
    }
}

void
ScrollBar::setSearchMarkers(std::vector<double> markers) noexcept
{
    if (markers.empty())
    {
        if (m_markers.empty())
            return;
        m_markers.clear();
    }
    else
    {
        if (markers == m_markers)
            return;
        m_markers = std::move(markers);
    }
    update();
}

void
ScrollBar::paintEvent(QPaintEvent *event)
{
    QScrollBar::paintEvent(event);

    if (m_markers.empty())
        return;

    QStyleOptionSlider opt;
    initStyleOption(&opt);

    const QRect groove = style()->subControlRect(
        QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarGroove, this);
    if (!groove.isValid())
        return;

    const int range = maximum() - minimum();
    if (range <= 0)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(255, 200, 0, 200), 2));

    const int minv = minimum();
    const int maxv = maximum();

    if (orientation() == Qt::Vertical)
    {
        const int top = groove.top();
        const int h   = groove.height() - 1;
        for (const double mv : m_markers)
        {
            if (mv < minv || mv > maxv)
                continue;
            const int y = top + static_cast<int>((mv - minv) / range * h);
            p.drawLine(groove.left(), y, groove.right(), y);
        }
    }
    else
    {
        const int left = groove.left();
        const int w    = groove.width() - 1;
        for (const double mv : m_markers)
        {
            if (mv < minv || mv > maxv)
                continue;
            const int x = left + static_cast<int>((mv - minv) / range * w);
            p.drawLine(x, groove.top(), x, groove.bottom());
        }
    }
}
