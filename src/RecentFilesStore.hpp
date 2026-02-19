#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <vector>

struct RecentFileEntry
{
    QString file_path;
    int page_number{0};
    QDateTime last_accessed;
};

inline bool
operator==(const RecentFileEntry &a, const RecentFileEntry &b)
{
    return a.file_path == b.file_path && a.page_number == b.page_number
           && a.last_accessed == b.last_accessed;
}

class RecentFilesStore
{
public:
    explicit RecentFilesStore(const QString &filePath = QString());

    int pageNumber(const QString &filePath) const noexcept;
    void setFilePath(const QString &filePath) noexcept;
    bool load() noexcept;
    bool save() const noexcept;

    const std::vector<RecentFileEntry> &entries() const noexcept;
    QStringList files() noexcept;
    void setEntries(std::vector<RecentFileEntry> entries) noexcept;
    void upsert(const QString &filePath, int pageNumber,
                const QDateTime &accessed) noexcept;
    void trim(int maxEntries) noexcept;

private:
    void sortByAccessedDesc() noexcept;
    static RecentFileEntry parseEntry(const QJsonObject &obj) noexcept;
    static QJsonObject serializeEntry(const RecentFileEntry &entry);

    QString m_file_path;
    std::vector<RecentFileEntry> m_entries;
};
