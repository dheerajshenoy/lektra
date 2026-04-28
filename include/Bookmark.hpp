#pragma once

#include "PageLocation.hpp"

#include <QDateTime>
#include <QString>

class Bookmark
{
public:
    Bookmark(const QString &file_path, const PageLocation &location,
             const QDateTime &created_at) noexcept;

    inline QString label() const noexcept
    {
        return m_label;
    }

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

private:
    QString m_label;
    QString m_file_path;
    PageLocation m_location;
    QDateTime m_created_at;
};
