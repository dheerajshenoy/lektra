#include "BookmarkPicker.hpp"

#include "BookmarkManager.hpp"

#include <QFileInfo>

BookmarkPicker::BookmarkPicker(const Config::Picker &config,
                               BookmarkManager *manager, QWidget *parent)
    : Picker(config, parent), m_bookmark_manager(manager)
{
    setColumns({{.header = "Bookmarks", .stretch = 1}});

    setStructureMode(StructureMode::Flat);
}

QList<Picker::Item>
BookmarkPicker::collectItems()
{
    QList<Item> items;
    items.reserve(m_bookmark_manager->bookmarks().size());
    for (const auto &bookmark : m_bookmark_manager->bookmarks())
    {
        items.push_back({
            .columns  = {bookmark.filePath()},
            .data     = bookmark.filePath(),
            .children = {},
        });
    }
    return items;
}

void
BookmarkPicker::onItemAccepted(const Item &item)
{
    emit fileOpenRequested(item.data.toString());
}
