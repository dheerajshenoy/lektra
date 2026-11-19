#include "BookmarkPicker.hpp"

#include "BookmarkManager.hpp"

#include <QFileInfo>

BookmarkPicker::BookmarkPicker(const Config::Picker &config,
                               BookmarkManager *manager, QWidget *parent)
    : Picker(config, parent), m_bookmark_manager(manager)
{
    setColumns({{.header = "ID", .stretch = 1},
                {.header = "Bookmarks", .stretch = 1}});
    setStructureMode(StructureMode::Flat);
    setPrompt("Bookmarks");
}

void
BookmarkPicker::setMode(Mode mode) noexcept
{
    m_mode = mode;
    if (mode == Mode::Remove)
    {
        setColumns({{.header = "Bookmark", .stretch = 1},
                    {.header = "Path", .stretch = 2}});
        setPrompt("Remove bookmark");
    }
    else
    {
        setColumns({{.header = "ID", .stretch = 1},
                    {.header = "Bookmarks", .stretch = 1}});
        setPrompt("Bookmarks");
    }
}

QList<Picker::Item>
BookmarkPicker::collectItems()
{
    QList<Item> items;
    const auto bookmarks = m_bookmark_manager->bookmarks();
    items.reserve(bookmarks.size());
    for (const auto &bookmark : bookmarks)
    {
        if (m_mode == Mode::Remove)
        {
            // The UUID means nothing to the user, so show file and page.
            items.push_back({
                .columns  = {QStringLiteral("%1 (p. %2)")
                                 .arg(QFileInfo(bookmark.filePath()).fileName())
                                 .arg(bookmark.location().pageno + 1),
                             bookmark.filePath()},
                .data     = bookmark.id(),
                .children = {},
            });
            continue;
        }

        items.push_back({
            .columns  = {bookmark.id(), bookmark.filePath()},
            .data     = bookmark.filePath(),
            .children = {},
        });
    }

    return items;
}

void
BookmarkPicker::onItemAccepted(const Item &item)
{
    if (m_mode == Mode::Remove)
        emit bookmarkRemoveRequested(item.data.toString());
    else
        emit fileOpenRequested(item.data.toString());
}
