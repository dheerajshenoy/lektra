#include "RecentFilesPicker.hpp"

#include <QFileInfo>

RecentFilesPicker::RecentFilesPicker(QWidget *parent) : Picker(parent) {}

QList<Picker::Item>
RecentFilesPicker::collectItems()
{
    QList<Item> items;
    for (const auto &path : m_recentFiles)
    {
        QFileInfo fi(path);
        items.push_back({.title    = fi.fileName(),
                         .subtitle = fi.absolutePath(),
                         .data     = path});
    }
    return items;
}

void
RecentFilesPicker::onItemAccepted(const Item &item)
{
    emit fileRequested(item.data.toString());
}
