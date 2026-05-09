#include "Bookmark.hpp"

#include <QUuid>

Bookmark::Bookmark(const QString &file_path, const PageLocation &location,
                   const QDateTime &created_at, BookmarkId id) noexcept
    : m_file_path(file_path), m_location(location), m_created_at(created_at),
      m_id(id)
{
    if (m_id.isEmpty())
    {
        m_id = QUuid::createUuid().toString();
    }
}
