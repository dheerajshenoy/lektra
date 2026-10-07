#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <optional>

// Saves LLM chats as one JSON file each in a folder, so earlier conversations
// can be listed and reopened.
//
// A chat keeps two things apart on purpose:
//   transcript  what the panel shows: [{kind: user|assistant|result|error,
//   text}] messages    what is sent to the model: [{role, content}] -- these
//   include
//               things the transcript does not show (script results folded
//               into the next question) and leave out failed requests
class ChatStore
{
public:
    struct Chat
    {
        QString id;
        QString title;
        QDateTime created;
        QDateTime updated;
        QJsonArray messages;
        QJsonArray transcript;
    };

    struct Summary
    {
        QString id;
        QString title;
        QDateTime updated;
    };

    // An empty folder disables the store (nothing is read or written).
    explicit ChatStore(const QString &folder = {}) : m_folder(folder) {}

    bool isEnabled() const noexcept
    {
        return !m_folder.isEmpty();
    }

    // Most recently updated first.
    QList<Summary> list(int limit = 50) const;
    bool save(const Chat &chat) const;
    std::optional<Chat> load(const QString &id) const;
    bool remove(const QString &id) const;
    // Removes every saved chat. Returns how many were deleted.
    int removeAll() const;

    static QString newId();
    // A short one-line title from the first question.
    static QString titleFrom(const QString &firstUserText);

private:
    QString pathFor(const QString &id) const;
    static bool validId(const QString &id);

    QString m_folder;
};
