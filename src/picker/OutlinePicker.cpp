#include "OutlinePicker.hpp"

#include "Model.hpp"

OutlinePicker::OutlinePicker(const Config::Outline &config,
                             QWidget *parent) noexcept
    : Picker(config, parent), m_config(config)
{

    setStructureMode(config.flat_menu ? StructureMode::Flat
                                      : StructureMode::Hierarchical);
    // setStructureMode(StructureMode::Hierarchical);

    // Configure the columns for the new populate() logic
    if (config.show_page_number)
    {
        setColumns({{.header = tr("Title"), .stretch = 1},
                    {.header = tr("Page"), .stretch = 0}});
    }
    else
    {
        setColumns({{.header = tr("Title"), .stretch = 1}});
    }

    setPrompt(config.prompt);
}

void
OutlinePicker::setOutline(fz_outline *outline, Model *model) noexcept
{
    m_entries.clear();
    if (outline)
        harvest(outline, 0, model);
}

void
OutlinePicker::clearOutline() noexcept
{
    m_entries.clear();
}

void
OutlinePicker::harvest(fz_outline *node, int depth, Model *model) noexcept
{
    for (fz_outline *n = node; n; n = n->next)
    {
        const QString title
            = QString::fromUtf8(n->title ? n->title : "<no title>")
                  .remove(QChar::Null)
                  .remove(QChar::ParagraphSeparator)
                  .remove(QChar::LineSeparator)
                  .remove(QChar(0xFFFD))
                  .trimmed();

        // n->page.page alone is only the LOCAL page-within-chapter number
        // for chaptered formats (EPUB) — must resolve via the Model to get
        // the document-wide page index. EPUB nodes additionally leave
        // n->page/x/y unresolved (sentinel {-1,-1}) and only carry a
        // uri, which resolveOutlineNode() also handles.
        float     x = n->x, y = n->y;
        const int pageno
            = model ? model->resolveOutlineNode(n, &x, &y) : n->page.page;

        m_entries.push_back({
            .title     = title,
            .depth     = depth,
            .page      = pageno,
            .location  = QPointF(x, y),
            .isHeading = (n->down != nullptr),
        });
        if (n->down)
            harvest(n->down, depth + 1, model);
    }
}

QList<Picker::Item>
OutlinePicker::collectItems()
{
    QList<Item> items;
    items.reserve(static_cast<int>(m_entries.size()));

    if (structureMode() == StructureMode::Flat)
    {
        if (m_config.show_page_number)
        {
            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                const auto &e = m_entries[i];
                items.push_back({
                    .columns
                    = {QString(e.depth * m_config.indent_width, ' ') + e.title,
                       QString::number(e.page + 1)},
                    .data     = static_cast<qulonglong>(i),
                    .children = {},
                });
            }
        }
        else
        {
            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                const auto &e = m_entries[i];
                items.push_back(
                    {.columns  = {QString(e.depth * m_config.indent_width, ' ')
                                  + e.title},
                     .data     = static_cast<qulonglong>(i),
                     .children = {}});
            }
        }

        return items;
    }

    QVector<int> path;

    auto listForDepth = [&items, &path](int depth) -> QList<Item> *
    {
        QList<Item> *list = &items;
        for (int d = 0; d < depth; ++d)
            list = &((*list)[path[d]].children);
        return list;
    };

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const auto &e = m_entries[i];
        int depth     = qMax(0, e.depth);

        if (depth > path.size())
            depth = path.size();
        while (path.size() > depth)
            path.removeLast();

        Item node;
        if (m_config.show_page_number)
            node.columns = {e.title, QString::number(e.page + 1)};
        else
            node.columns = {e.title};
        node.data = static_cast<qulonglong>(i);

        QList<Item> *target = listForDepth(depth);
        target->append(node);
        path.append(target->size() - 1);
    }

    return items;
}

void
OutlinePicker::onItemAccepted(const Item &item)
{
    const size_t i = item.data.toULongLong();
    if (i < m_entries.size())
    {
        emit jumpToLocationRequested(m_entries[i].page, m_entries[i].location);
    }
}

void
OutlinePicker::selectCurrentPage() noexcept
{
    if (m_entries.empty() || m_current_page < 0)
        return;

    int best_idx = 0;

    // Find the entry with the largest page number that is <= current page
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].page < m_current_page)
        {
            best_idx = static_cast<int>(i);
        }
        else
        {
            break; // Since entries are in order, we can stop here
        }
    }

    QModelIndex source_idx = m_proxy->sourceModel()->index(best_idx, 0);
    QModelIndex proxy_idx  = m_proxy->mapFromSource(source_idx);
    m_listView->setCurrentIndex(proxy_idx);
    m_listView->scrollTo(proxy_idx, QAbstractItemView::PositionAtCenter);
}
