#include "MathText.hpp"

#include "MathRender.hpp"

#include <QBuffer>
#include <algorithm>

namespace mathtext
{
qsizetype
backtickRun(const QString &text, qsizetype i)
{
    qsizetype n = 0;
    while (i + n < text.size() && text[i + n] == QLatin1Char('`'))
        ++n;
    return n;
}

qsizetype
skipCodeSpan(const QString &text, qsizetype i)
{
    const qsizetype run = backtickRun(text, i);
    for (qsizetype j = i + run; j < text.size(); ++j)
        if (text[j] == QLatin1Char('`') && backtickRun(text, j) == run)
            return j + run;
        else if (text[j] == QLatin1Char('`'))
            j += backtickRun(text, j) - 1;
    return i;
}

bool
escaped(const QString &text, qsizetype i)
{
    return i > 0 && text[i - 1] == QLatin1Char('\\');
}

QList<Span>
findDisplay(const QString &text, bool skipCode)
{
    QList<Span> found;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (skipCode && text[i] == QLatin1Char('`'))
        {
            const qsizetype end = skipCodeSpan(text, i);
            i = end > i ? end - 1 : i + backtickRun(text, i) - 1;
            continue;
        }
        if (escaped(text, i))
            continue;

        QString close;
        if (text.mid(i, 2) == QLatin1String("$$"))
            close = QStringLiteral("$$");
        else if (text.mid(i, 2) == QLatin1String("\\["))
            close = QStringLiteral("\\]");
        else
            continue;
        const qsizetype end = text.indexOf(close, i + 2);
        if (end < 0)
            continue; // not closed (yet): stays text
        const QString body = text.mid(i + 2, end - i - 2).trimmed();
        if (body.isEmpty())
            continue;
        found.append({i, end + close.size(), body, true});
        i = end + close.size() - 1;
    }
    return found;
}

QList<Span>
findInline(const QString &text, bool skipCode)
{
    QList<Span> found;
    const QString &t = text;
    for (qsizetype i = 0; i < t.size(); ++i)
    {
        if (skipCode && t[i] == QLatin1Char('`'))
        {
            const qsizetype end = skipCodeSpan(t, i);
            i                   = end > i ? end - 1 : i + backtickRun(t, i) - 1;
            continue;
        }
        if (escaped(t, i))
            continue;

        if (t.mid(i, 2) == QLatin1String("\\("))
        {
            const qsizetype end = t.indexOf(QLatin1String("\\)"), i + 2);
            if (end > i + 2 && end - i < 400)
            {
                found.append(
                    {i, end + 2, t.mid(i + 2, end - i - 2).trimmed(), false});
                i = end + 1;
            }
        }
        else if (t[i] == QLatin1Char('$') && i + 1 < t.size()
                 && t[i + 1] != QLatin1Char('$') && !t[i + 1].isSpace())
        {
            for (qsizetype j = i + 1; j < t.size() && j - i < 400; ++j)
            {
                if (t[j] == QLatin1Char('\n'))
                    break;
                if (t[j] != QLatin1Char('$') || escaped(t, j))
                    continue;
                const bool valid
                    = !t[j - 1].isSpace()
                      && (j + 1 >= t.size() || !t[j + 1].isDigit());
                if (valid)
                {
                    found.append({i, j + 1, t.mid(i + 1, j - i - 1), false});
                    i = j;
                }
                break;
            }
        }
    }
    return found;
}

QString
imageHtml(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"%2\" "
                          "height=\"%3\" style=\"vertical-align: middle;\"/>")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(qRound(image.width() / image.devicePixelRatio()))
        .arg(qRound(image.height() / image.devicePixelRatio()));
}

namespace
{
// Plain text as HTML: escaped, with its line breaks kept.
QString
plainToHtml(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'),
                                        QLatin1String("<br>"));
}
} // namespace

QString
commentToHtml(const QString &text, int pixelSize, const QColor &color,
              int maxWidth, qreal dpr)
{
    // Display formulas first, then the inline ones in the text between them.
    QList<Span> spans = findDisplay(text, false);
    qsizetype last    = 0;
    QList<Span> inlineSpans;
    auto addInline = [&](qsizetype from, qsizetype to)
    {
        const QString part = text.mid(from, to - from);
        for (Span s : findInline(part, false))
        {
            s.start += from;
            s.end += from;
            inlineSpans.append(s);
        }
    };
    for (const Span &d : spans)
    {
        addInline(last, d.start);
        last = d.end;
    }
    addInline(last, text.size());

    spans += inlineSpans;
    if (spans.isEmpty())
        return {};
    std::sort(spans.begin(), spans.end(),
              [](const Span &a, const Span &b) { return a.start < b.start; });

    QString html;
    qsizetype pos = 0;
    for (const Span &s : spans)
    {
        html += plainToHtml(text.mid(pos, s.start - pos));
        pos = s.end;

        const QImage image
            = renderMath(s.latex, pixelSize, color, maxWidth, dpr);
        if (image.isNull())
        {
            // not valid LaTeX: keep what was written
            html += plainToHtml(text.mid(s.start, s.end - s.start));
        }
        else if (s.display)
        {
            html += QStringLiteral("<div align=\"center\">") + imageHtml(image)
                    + QStringLiteral("</div>");
            // The formula sits on its own line, so a line break right after
            // it would only add an empty one.
            if (text.mid(pos, 1) == QLatin1String("\n"))
                ++pos;
        }
        else
        {
            html += imageHtml(image);
        }
    }
    html += plainToHtml(text.mid(pos));
    return html;
}
} // namespace mathtext
