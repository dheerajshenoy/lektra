#pragma once

#include "PageLocation.hpp"

#include <QDateTime>
#include <QString>

struct Bookmark
{
    QString label;
    QString file_path;
    PageLocation location;
    QDateTime created_at;

    bool operator==(const Bookmark &rhs) const noexcept
    {
        return file_path == rhs.file_path && location == rhs.location
               && label == rhs.label;
    }
};
