#include "MessageLog.hpp"

#include <algorithm>

MessageLog &
MessageLog::instance() noexcept
{
    static MessageLog log;
    return log;
}

void
MessageLog::add(Level level, const QString &text) noexcept
{
    if (m_limit <= 0)
        return;
    m_entries.append({QDateTime::currentDateTime(), level, text});
    trim();
    emit added(m_entries.constLast());
}

void
MessageLog::clear() noexcept
{
    m_entries.clear();
    emit cleared();
}

void
MessageLog::setLimit(int limit) noexcept
{
    m_limit = std::max(0, limit);
    if (m_entries.size() > m_limit)
    {
        trim();
        emit cleared(); // views rebuild from entries()
    }
}

void
MessageLog::trim() noexcept
{
    if (m_entries.size() > m_limit)
        m_entries.remove(0, m_entries.size() - m_limit);
}

const char *
MessageLog::levelName(Level level) noexcept
{
    switch (level)
    {
        case Level::Info:
            return "info";
        case Level::Warning:
            return "warning";
        case Level::Error:
            return "error";
    }
    return "info";
}
