#include "OutlinePicker.hpp"

OutlinePicker::OutlinePicker(const Config::Outline &config,
                             QWidget *parent) noexcept
    : Picker(parent), m_config(config)
{
    // Configure the columns for the new populate() logic

    if (config.show_page_numbers)
    {
        setColumns({{.header = tr("Title"), .stretch = 1},
                    {.header = tr("Page"), .stretch = 0}});
    }
    else
    {
        setColumns({{.header = tr("Title"), .stretch = 1}});
    }
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

    if (m_config.show_page_numbers)
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const auto &e = m_entries[i];

            // The new populate() logic expects:
            // Column 0: item.title
            // Column 1: item.subtitle
            items.push_back(
                {.columns
                 = {QString(e.depth * m_config.indent_width, ' ') + e.title,
                    QString::number(e.page + 1)},
                 .data = static_cast<qulonglong>(i)});
        }
    }
    else
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const auto &e = m_entries[i];

            // The new populate() logic expects:
            // Column 0: item.title
            // Column 1: item.subtitle
            items.push_back(
                {.columns
                 = {QString(e.depth * m_config.indent_width, ' ') + e.title},
                 .data = static_cast<qulonglong>(i)});
        }
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
