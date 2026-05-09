#pragma once

#include "PageLocation.hpp"

#include <QDateTime>
#include <QString>

class Bookmark
{
public:
    using BookmarkId = QString;

    Bookmark(const QString &file_path, const PageLocation &location,
             const QDateTime &created_at, BookmarkId id = "") noexcept;

    inline QString filePath() const noexcept
    {
        return m_file_path;
    }

    inline PageLocation location() const noexcept
    {
        return m_location;
    }

    inline QDateTime createdAt() const noexcept
    {
        return m_created_at;
    }

    inline BookmarkId id() const noexcept
    {
        return m_id;
    }

private:
    QString m_file_path;
    PageLocation m_location;
    QDateTime m_created_at;
    BookmarkId m_id;
};
