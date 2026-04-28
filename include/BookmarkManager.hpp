#pragma once

#include "Bookmark.hpp"

#include <QString>

class BookmarkManager
{
public:
    void addBookmark(const Bookmark &bookmark);
    void removeBookmark(const QString &file_path);
    QString getBookmark(const QString &file_path) const;

    inline std::vector<Bookmark> bookmarks() const
    {
        return m_bookmarks;
    }

    inline void setBookmarks(const std::vector<Bookmark> &bookmarks)
    {
        m_bookmarks = bookmarks;
    }

    void saveBookmarks(const QString &file_path) const;

private:
    std::vector<Bookmark> m_bookmarks;
};
