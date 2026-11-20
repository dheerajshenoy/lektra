#pragma once

// The messages Lektra has shown or reported (message bar, config.toml
// problems, Lua errors), kept so they can be read later in the messages tab
// (the `messages` command) or from Lua (lektra.ui.messages()).
//
// One log per process; use it from the GUI thread only.

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

class MessageLog : public QObject
{
    Q_OBJECT
public:
    enum class Level
    {
        Info,
        Warning,
        Error,
    };

    struct Entry
    {
        QDateTime time;
        Level level;
        QString text;
    };

    static MessageLog &instance() noexcept;

    void add(Level level, const QString &text) noexcept;
    void clear() noexcept;

    // How many entries are kept; the oldest are dropped first.
    void setLimit(int limit) noexcept;

    const QList<Entry> &entries() const noexcept
    {
        return m_entries;
    }

    static const char *levelName(Level level) noexcept;

signals:
    // Emitted after the entry is added and the log trimmed to its limit.
    void added(const MessageLog::Entry &entry);
    void cleared();

private:
    MessageLog() = default;
    void trim() noexcept;

    QList<Entry> m_entries;
    int m_limit = 1000;
};

// Shorthands for MessageLog::instance().add(...)
inline void
logInfo(const QString &text) noexcept
{
    MessageLog::instance().add(MessageLog::Level::Info, text);
}

inline void
logWarning(const QString &text) noexcept
{
    MessageLog::instance().add(MessageLog::Level::Warning, text);
}

inline void
logError(const QString &text) noexcept
{
    MessageLog::instance().add(MessageLog::Level::Error, text);
}
