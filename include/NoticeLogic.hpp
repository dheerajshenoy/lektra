#pragma once

#include <QDateTime>
#include <QList>
#include <QRegularExpression>
#include <QString>

// The rules behind the banners Lektra shows now and then (a newer release, what
// changed after an update, a reminder that it can be supported). Kept apart
// from the widgets so they can be tested on their own.
namespace notice
{
// "v0.7.10", "0.7.10-rc1" -> {0, 7, 10}; the parts that are not numbers are
// ignored.
inline QList<int>
parseVersion(const QString &version)
{
    QString v = version.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        v.remove(0, 1);
    QList<int> parts;
    for (const QString &part :
         v.section(QRegularExpression(QStringLiteral("[-+ ]")), 0, 0)
             .split(QLatin1Char('.')))
    {
        bool ok       = false;
        const int num = part.toInt(&ok);
        if (!ok)
            break;
        parts << num;
    }
    return parts;
}

// Whether `candidate` is a later version than `current`. A version that cannot
// be read is never newer.
inline bool
isNewerVersion(const QString &candidate, const QString &current)
{
    QList<int> a = parseVersion(candidate);
    QList<int> b = parseVersion(current);
    if (a.isEmpty() || b.isEmpty())
        return false;
    while (a.size() < b.size())
        a << 0;
    while (b.size() < a.size())
        b << 0;
    return a > b;
}

// The part of a changelog under "## <version>" (the heading itself is kept),
// up to the next "## " heading; empty if there is no such heading.
inline QString
changelogSection(const QString &changelog, const QString &version)
{
    const QRegularExpression heading(
        QStringLiteral("^## +v?%1(?:\\s.*)?$")
            .arg(QRegularExpression::escape(version.trimmed())),
        QRegularExpression::MultilineOption);
    const auto match = heading.match(changelog);
    if (!match.hasMatch())
        return {};
    const qsizetype start = match.capturedStart();
    const qsizetype next  = changelog.indexOf(QStringLiteral("\n## "), match.capturedEnd());
    return changelog.mid(start, next < 0 ? -1 : next - start).trimmed();
}

// What has been kept between runs about the reminders.
struct DonateState
{
    QDateTime firstRun;
    QDateTime lastShown; // invalid if never shown
    int launches    = 0;
    bool dismissed  = false; // "Don't ask again"
};

constexpr int kDonateMinDays     = 14;  // since the first run
constexpr int kDonateMinLaunches = 20;
constexpr int kDonateRepeatDays  = 182; // between two reminders

// Whether to show the support reminder now: only once the app has been used
// for a while, rarely after that, and never after "Don't ask again".
inline bool
donateReminderDue(const DonateState &state, const QDateTime &now, bool enabled)
{
    if (!enabled || state.dismissed || !state.firstRun.isValid())
        return false;
    if (state.launches < kDonateMinLaunches
        || state.firstRun.daysTo(now) < kDonateMinDays)
        return false;
    return !state.lastShown.isValid()
           || state.lastShown.daysTo(now) >= kDonateRepeatDays;
}
} // namespace notice
