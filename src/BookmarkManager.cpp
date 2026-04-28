#include "BookmarkManager.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

void
BookmarkManager::addBookmark(const Bookmark &bookmark)
{
    m_bookmarks.push_back(bookmark);
}

void
BookmarkManager::removeBookmark(const QString &file_path)
{
    m_bookmarks.erase(std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
                                     [&file_path](const Bookmark &bookmark)
    { return bookmark.filePath() == file_path; }),
                      m_bookmarks.end());
}

QString
BookmarkManager::getBookmark(const QString &file_path) const
{
    for (const auto &bookmark : m_bookmarks)
    {
        if (bookmark.filePath() == file_path)
        {
            return bookmark.label();
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
