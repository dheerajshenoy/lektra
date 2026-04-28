#include "Bookmark.hpp"

Bookmark::Bookmark(const QString &file_path, const PageLocation &location,
                   const QDateTime &created_at) noexcept
    : m_file_path(file_path), m_location(location), m_created_at(created_at)
{
}
