#pragma once

#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <vector>

// Page ranges for exporting, and the names of the files made from them.
namespace page_range
{
// "1-5,8,10-", "all", "odd", "even", "current", "first", "last", "-3" ... as 0-based
// page numbers, in the order they are written, without repeats. Items are
// separated by commas, semicolons or spaces. `current` is 0-based. On a
// mistake returns nothing and says what is wrong in `error`.
inline std::vector<int>
parse(const QString &spec, int pageCount, int current, QString *error = nullptr)
{
    auto fail = [error](const QString &message) -> std::vector<int>
    {
        if (error)
            *error = message;
        return {};
    };
    if (pageCount <= 0)
        return fail(QStringLiteral("there are no pages"));

    // A single page: a number, or one of the words.
    auto page = [&](const QString &word, int &out, QString &problem) -> bool
    {
        if (word == QLatin1String("first"))
            out = 1;
        else if (word == QLatin1String("last"))
            out = pageCount;
        else if (word == QLatin1String("current") || word == QLatin1String("."))
            out = current + 1;
        else
        {
            bool ok = false;
            out     = word.toInt(&ok);
            if (!ok)
            {
                problem = QStringLiteral("\"%1\" is not a page number").arg(word);
                return false;
            }
        }
        if (out < 1 || out > pageCount)
        {
            problem = QStringLiteral("page %1 does not exist (the pages are 1 to %2)")
                          .arg(out)
                          .arg(pageCount);
            return false;
        }
        return true;
    };

    std::vector<int> pages;
    auto add = [&](int p)
    {
        if (std::find(pages.begin(), pages.end(), p) == pages.end())
            pages.push_back(p);
    };

    const QStringList items
        = spec.toLower().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                               Qt::SkipEmptyParts);
    if (items.isEmpty())
        return fail(QStringLiteral("no pages were given"));

    for (const QString &item : items)
    {
        if (item == QLatin1String("all") || item == QLatin1String("*"))
        {
            for (int p = 0; p < pageCount; ++p)
                add(p);
        }
        else if (item == QLatin1String("odd") || item == QLatin1String("even"))
        {
            for (int p = item == QLatin1String("odd") ? 0 : 1; p < pageCount; p += 2)
                add(p);
        }
        else if (const qsizetype dash = item.indexOf(QLatin1Char('-')); dash >= 0)
        {
            // "A-B", "A-" (to the end), "-B" (from the start)
            const QString left  = item.left(dash);
            const QString right = item.mid(dash + 1);
            int from = 1, to = pageCount;
            QString problem;
            if (!left.isEmpty() && !page(left, from, problem))
                return fail(problem);
            if (!right.isEmpty() && !page(right, to, problem))
                return fail(problem);
            if (left.isEmpty() && right.isEmpty())
                return fail(QStringLiteral("\"-\" is not a range"));
            if (from > to)
                return fail(QStringLiteral("\"%1\" goes backwards").arg(item));
            for (int p = from; p <= to; ++p)
                add(p - 1);
        }
        else
        {
            int p = 0;
            QString problem;
            if (!page(item, p, problem))
                return fail(problem);
            add(p - 1);
        }
    }
    return pages;
}

// Whether a name has a place for the page number (%d or %03d).
inline bool
hasPlaceholder(const QString &pattern)
{
    static const QRegularExpression placeholder(QStringLiteral("%0?\\d*d"));
    return placeholder.match(pattern).hasMatch();
}

// The name of the file for one page when several are written. A name with %d
// (or %03d for zero padding) gets the page number there; any other name gets
// "-<number>" before its extension, padded to `width` digits.
inline QString
nameForPage(const QString &pattern, int page, int width)
{
    static const QRegularExpression placeholder(QStringLiteral("%0?(\\d*)d"));
    const auto match = placeholder.match(pattern);
    if (match.hasMatch())
    {
        const bool zero = match.captured(0).startsWith(QLatin1String("%0"));
        const int w     = match.captured(1).toInt();
        QString number  = QString::number(page);
        if (zero && number.size() < w)
            number.prepend(QString(w - number.size(), QLatin1Char('0')));
        QString name = pattern;
        name.replace(match.capturedStart(), match.capturedLength(), number);
        return name;
    }
    const QFileInfo info(pattern);
    const QString suffix = info.suffix();
    QString number       = QString::number(page);
    if (number.size() < width)
        number.prepend(QString(width - number.size(), QLatin1Char('0')));
    const QString base = suffix.isEmpty() ? pattern : pattern.left(pattern.size() - suffix.size() - 1);
    return base + QLatin1Char('-') + number + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
}
} // namespace page_range
