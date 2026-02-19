#include "OutlinePicker.hpp"

OutlinePicker::OutlinePicker(QWidget *parent) noexcept
    : Picker(parent)
{
}

void
OutlinePicker::setOutline(fz_outline *outline) noexcept
{
    m_entries.clear();
    if (outline)
        harvest(outline, 0);
}

void
OutlinePicker::clearOutline() noexcept
{
    m_entries.clear();
}

void
OutlinePicker::harvest(fz_outline *node, int depth) noexcept
{
    for (fz_outline *n = node; n; n = n->next)
    {
        m_entries.push_back({
            .title     = QString::fromUtf8(n->title ? n->title : "<no title>"),
            .depth     = depth,
            .page      = n->page.page,
            .location  = QPointF(n->x, n->y),
            .isHeading = (n->down != nullptr),
        });
        if (n->down)
            harvest(n->down, depth + 1);
    }
}

QList<Picker::Item>
OutlinePicker::collectItems()
{
    QList<Item> items;
    items.reserve(static_cast<int>(m_entries.size()));

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const auto &e = m_entries[i];
        items.push_back({
            .title    = QString(e.depth * 2, ' ') + e.title,
            .subtitle = QString("p%1").arg(e.page + 1),
            .data     = static_cast<qulonglong>(i),
        });
    }
    return items;
}

void
OutlinePicker::onItemAccepted(const Item &item)
{
    const size_t i = item.data.toULongLong();
    if (i < m_entries.size())
        emit jumpToLocationRequested(m_entries[i].page, m_entries[i].location);
}
