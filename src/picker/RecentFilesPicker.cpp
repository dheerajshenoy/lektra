#include "RecentFilesPicker.hpp"

#include <QFileInfo>

RecentFilesPicker::RecentFilesPicker(const Config::Picker &config,
                                     QWidget *parent)
    : Picker(config, parent)
{
    setColumns(
        {{.header = "Name", .stretch = 2}, {.header = "Path", .stretch = 0}});
    setStructureMode(StructureMode::Flat);
    setPrompt("Recent Files");
}

QList<Picker::Item>
RecentFilesPicker::collectItems()
{
    QList<Item> items;
    items.reserve(m_recentFiles.size());
    for (const auto &path : m_recentFiles)
    {
        QFileInfo fi(path);
        items.push_back({
            .columns  = {fi.fileName(), fi.absolutePath()},
            .data     = path,
            .children = {},
        });
    }
    return items;
}

void
RecentFilesPicker::onItemAccepted(const Item &item)
{
    emit fileRequested(item.data.toString());
}
