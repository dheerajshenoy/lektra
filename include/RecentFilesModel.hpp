#pragma once

#include "RecentFilesStore.hpp"

#include <QAbstractTableModel>
#include <QString>

class RecentFilesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum ColumnType
    {
        FilePath = 0,
        LastAccessed,
        COUNT
    };

    explicit RecentFilesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count,
                    const QModelIndex &parent = QModelIndex()) override;

    void setEntries(std::vector<RecentFileEntry> entries,
                    bool markClean = true);
    const std::vector<RecentFileEntry> &entries() const noexcept;
    RecentFileEntry entryAt(int row) const;
    bool isDirty() const noexcept;
    void revertAll();
    void markClean();
    void setDisplayHomePath(bool enabled) noexcept;

private:
    QString displayPath(const QString &path) const;
    bool parseDateTime(const QVariant &value, QDateTime *out) const;

    std::vector<RecentFileEntry> m_entries;
    std::vector<RecentFileEntry> m_original_entries;
    QString m_home_path;
    bool m_use_tilde{false};
};
