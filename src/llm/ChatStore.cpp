#include "ChatStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

QString
ChatStore::newId()
{
    // sortable by time, with a random tail so two chats in the same
    // millisecond cannot collide
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
           + QLatin1Char('-')
           + QString::number(QRandomGenerator::global()->bounded(0x10000), 16)
                 .rightJustified(4, QLatin1Char('0'));
}

QString
ChatStore::titleFrom(const QString &firstUserText)
{
    QString title = firstUserText.simplified();
    constexpr int kMax = 60;
    if (title.size() > kMax)
        title = title.left(kMax - 1).trimmed() + QStringLiteral("…");
    return title.isEmpty() ? QStringLiteral("(untitled)") : title;
}

bool
ChatStore::validId(const QString &id)
{
    // ids come from file names: only ever digits, hex letters and dashes, so
    // an id can never point outside the folder
    static const QRegularExpression ok(QStringLiteral("^[0-9a-f-]+$"));
    return !id.isEmpty() && id.size() < 64 && ok.match(id).hasMatch();
}

QString
ChatStore::pathFor(const QString &id) const
{
    return QDir(m_folder).filePath(id + QStringLiteral(".json"));
}

QList<ChatStore::Summary>
ChatStore::list(int limit) const
{
    QList<Summary> out;
    if (!isEnabled())
        return out;

    const QStringList files
        = QDir(m_folder).entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &file : files)
    {
        const QString id = file.chopped(5);
        if (!validId(id))
            continue;
        QFile f(pathFor(id));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.isEmpty())
            continue;
        out.append({id, o.value("title").toString(),
                    QDateTime::fromString(o.value("updated").toString(), Qt::ISODate)});
    }
    std::sort(out.begin(), out.end(), [](const Summary &a, const Summary &b)
    { return a.updated != b.updated ? a.updated > b.updated : a.id > b.id; });
    if (limit > 0 && out.size() > limit)
        out.resize(limit);
    return out;
}

bool
ChatStore::save(const Chat &chat) const
{
    if (!isEnabled() || !validId(chat.id))
        return false;
    if (!QDir().mkpath(m_folder))
        return false;

    const QJsonObject o{
        {"id", chat.id},
        {"title", chat.title},
        {"created", chat.created.toString(Qt::ISODate)},
        {"updated", chat.updated.toString(Qt::ISODate)},
        {"messages", chat.messages},
        {"transcript", chat.transcript},
    };
    QSaveFile file(pathFor(chat.id)); // written to a temp file, then renamed
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return file.commit();
}

std::optional<ChatStore::Chat>
ChatStore::load(const QString &id) const
{
    if (!isEnabled() || !validId(id))
        return std::nullopt;
    QFile f(pathFor(id));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty())
        return std::nullopt;

    Chat chat;
    chat.id         = id;
    chat.title      = o.value("title").toString();
    chat.created    = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
    chat.updated    = QDateTime::fromString(o.value("updated").toString(), Qt::ISODate);
    chat.messages   = o.value("messages").toArray();
    chat.transcript = o.value("transcript").toArray();
    return chat;
}

bool
ChatStore::remove(const QString &id) const
{
    return isEnabled() && validId(id) && QFile::remove(pathFor(id));
}

int
ChatStore::removeAll() const
{
    int n = 0;
    for (const Summary &s : list(0))
        n += remove(s.id) ? 1 : 0;
    return n;
}
