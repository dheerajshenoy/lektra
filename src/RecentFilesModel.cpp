#include "RecentFilesModel.hpp"

#include <QDateTime>
#include <QMetaType>

RecentFilesModel::RecentFilesModel(QObject *parent)
    : QAbstractTableModel(parent), m_home_path(QString(getenv("HOME")))
{
}

int
RecentFilesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

int
RecentFilesModel::columnCount(const QModelIndex &parent) const
{
    (void)parent;
    return 2;
}

QVariant
RecentFilesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const RecentFileEntry &entry = m_entries.at(index.row());

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
            case ColumnType::FilePath:
                return displayPath(entry.file_path);
            case ColumnType::LastAccessed:
                return QLocale().toString(entry.last_accessed,
                                          QLocale::ShortFormat);
            default:
                return {};
        }
    }

    if (role == Qt::UserRole)
    {
        switch (index.column())
        {
            case ColumnType::FilePath:
                return entry.file_path;
            case ColumnType::LastAccessed:
                return entry.last_accessed;
            default:
                return {};
        }
    }

    if (role == Qt::EditRole)
    {
        switch (index.column())
        {
            case ColumnType::FilePath:
                return entry.file_path;
            case ColumnType::LastAccessed:
                return entry.last_accessed;
            default:
                return {};
        }
    }

    return {};
}

bool
RecentFilesModel::setData(const QModelIndex &index, const QVariant &value,
                          int role)
{
    if (!index.isValid() || role != Qt::EditRole)
        return false;

    if (index.row() < 0 || index.row() >= m_entries.size())
        return false;

    RecentFileEntry &entry = m_entries[index.row()];

    if (index.column() == LastAccessed)
    {
        QDateTime parsed;
        if (!parseDateTime(value, &parsed))
            return false;
        entry.last_accessed = parsed;
    }
    else
        return false;

    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant
RecentFilesModel::headerData(int section, Qt::Orientation orientation,
                             int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};

    switch (section)
    {
        case FilePath:
            return tr("File Path");
        case LastAccessed:
            return tr("Last Visited");
        default:
            return {};
    }
}

Qt::ItemFlags
RecentFilesModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (index.column() != FilePath)
        flags |= Qt::ItemIsEditable;
    return flags;
}

bool
RecentFilesModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (row < 0 || count <= 0 || row + count > m_entries.size())
        return false;

    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i)
        m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
    return true;
}

void
RecentFilesModel::setEntries(std::vector<RecentFileEntry> entries, bool markClean)
{
    beginResetModel();
    m_entries = std::move(entries);
    if (markClean)
        m_original_entries = m_entries;
    endResetModel();
}

const std::vector<RecentFileEntry> &
RecentFilesModel::entries() const noexcept
{
    return m_entries;
}

RecentFileEntry
RecentFilesModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return m_entries.at(row);
}

bool
RecentFilesModel::isDirty() const noexcept
{
    return m_entries != m_original_entries;
}

void
RecentFilesModel::revertAll()
{
    setEntries(m_original_entries, true);
}

void
RecentFilesModel::markClean()
{
    m_original_entries = m_entries;
}

void
RecentFilesModel::setDisplayHomePath(bool enabled) noexcept
{
    m_use_tilde = enabled;
}

QString
RecentFilesModel::displayPath(const QString &path) const
{
    if (!m_use_tilde || m_home_path.isEmpty())
        return path;

    if (path.startsWith(m_home_path))
    {
        QString display = path;
        display.replace(m_home_path, "~/");
        return display;
    }

    return path;
}

bool
RecentFilesModel::parseDateTime(const QVariant &value, QDateTime *out) const
{
    if (!out)
        return false;

    if (value.canConvert<QDateTime>())
    {
        QDateTime dt = value.toDateTime();
        if (dt.isValid())
        {
            *out = dt;
            return true;
        }
    }

    if (value.typeId() == QMetaType::Type::QString)
    {
        const QString text = value.toString();
        QDateTime dt       = QDateTime::fromString(text, Qt::ISODate);
        if (dt.isValid())
        {
            *out = dt;
            return true;
        }
    }

    return false;
}
