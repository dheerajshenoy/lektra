#include "BookmarkManager.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

void
BookmarkManager::addBookmark(const Bookmark &bookmark)
{
    m_bookmarks.push_back(bookmark);
}

void
BookmarkManager::removeBookmark(Bookmark::BookmarkId id)
{
    m_bookmarks.erase(std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
                                     [&id](const Bookmark &bookmark)
    { return bookmark.id() == id; }),
                      m_bookmarks.end());
}

QString
BookmarkManager::getBookmark(const Bookmark::BookmarkId &id) const
{
    for (const auto &bookmark : m_bookmarks)
    {
        if (bookmark.id() == id)
        {
            return bookmark.filePath();
        }
    }

    return QString();
}

void
BookmarkManager::saveBookmarks(const QString &file_path) const
{
    QJsonArray json_array;
    for (const auto &bookmark : m_bookmarks)
    {
        QJsonObject json_object;
        json_object["id"]        = bookmark.id();
        json_object["file_path"] = bookmark.filePath();
        json_object["location"]  = bookmark.location().toJson();
        json_object["added_on"]  = bookmark.createdAt().toString(Qt::ISODate);
        json_array.append(json_object);
    }

    QJsonDocument json_doc(json_array);
    QFile file(file_path);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(json_doc.toJson());
        file.close();
    }
}
