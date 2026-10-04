#include "LuaHighlight.hpp"

#include <QSet>

namespace
{
struct Theme
{
    QString keyword, string, comment, number, builtin, function;
};

Theme
themeFor(const QPalette &palette)
{
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    if (dark)
        return {"#c586c0", "#ce9178", "#6a9955", "#b5cea8", "#4ec9b0", "#dcdcaa"};
    return {"#af00db", "#a31515", "#008000", "#098658", "#267f99", "#795e26"};
}

const QSet<QString> &
keywords()
{
    static const QSet<QString> k = {
        "and", "break", "do",   "else", "elseif", "end",    "false",
        "for", "function", "goto", "if",   "in",     "local",  "nil",
        "not", "or",    "repeat", "return", "then",  "true",   "until",
        "while"};
    return k;
}

const QSet<QString> &
builtins()
{
    static const QSet<QString> b = {
        "lektra", "print",    "pairs",  "ipairs", "next",   "select",
        "type",   "tostring", "tonumber", "pcall", "xpcall", "error",
        "assert", "unpack",   "string", "table",  "math",   "bit",
        "os",     "setmetatable", "rawget", "rawset", "rawequal"};
    return b;
}

QString
escape(const QString &s)
{
    return s.toHtmlEscaped();
}

QString
span(const QString &color, const QString &text, bool italic = false)
{
    return QStringLiteral("<span style=\"color:%1;%2\">%3</span>")
        .arg(color, italic ? QStringLiteral("font-style:italic;") : QString(),
             escape(text));
}

// If a long bracket "[=*[" starts at `i`, the index just past its closing
// bracket (or the end of the text when it is not closed); -1 if there is none.
int
longBracketEnd(const QString &s, int i)
{
    if (i >= s.size() || s[i] != QLatin1Char('['))
        return -1;
    int j = i + 1;
    while (j < s.size() && s[j] == QLatin1Char('='))
        ++j;
    if (j >= s.size() || s[j] != QLatin1Char('['))
        return -1;
    const QString close
        = QLatin1Char(']') + QString(j - i - 1, QLatin1Char('=')) + QLatin1Char(']');
    const int end = s.indexOf(close, j + 1);
    return end < 0 ? s.size() : end + close.size();
}

constexpr const char *kPreStyle
    = "white-space: pre-wrap; margin: 0; font-family: monospace;";
} // namespace

QString
plainCodeToHtml(const QString &code)
{
    return QStringLiteral("<pre style=\"%1\">%2</pre>")
        .arg(QLatin1String(kPreStyle), escape(code));
}

QString
luaToHtml(const QString &code, const QPalette &palette)
{
    const Theme theme = themeFor(palette);
    QString out;
    const int n = code.size();
    int i = 0;

    while (i < n)
    {
        const QChar c = code[i];

        // comments
        if (c == QLatin1Char('-') && i + 1 < n && code[i + 1] == QLatin1Char('-'))
        {
            int end = longBracketEnd(code, i + 2);
            if (end < 0)
            {
                end = code.indexOf(QLatin1Char('\n'), i);
                if (end < 0)
                    end = n;
            }
            out += span(theme.comment, code.mid(i, end - i), true);
            i = end;
            continue;
        }

        // strings
        if (c == QLatin1Char('"') || c == QLatin1Char('\''))
        {
            int j = i + 1;
            while (j < n && code[j] != c && code[j] != QLatin1Char('\n'))
                j += (code[j] == QLatin1Char('\\') && j + 1 < n) ? 2 : 1;
            if (j < n && code[j] == c)
                ++j;
            out += span(theme.string, code.mid(i, j - i));
            i = j;
            continue;
        }
        if (c == QLatin1Char('['))
        {
            const int end = longBracketEnd(code, i);
            if (end >= 0)
            {
                out += span(theme.string, code.mid(i, end - i));
                i = end;
                continue;
            }
        }

        // numbers
        if (c.isDigit()
            || (c == QLatin1Char('.') && i + 1 < n && code[i + 1].isDigit()))
        {
            int j = i + 1;
            const bool hex = c == QLatin1Char('0') && j < n
                             && (code[j] == QLatin1Char('x') || code[j] == QLatin1Char('X'));
            while (j < n)
            {
                const QChar d = code[j];
                const bool exp = !hex && (code[j - 1] == QLatin1Char('e')
                                          || code[j - 1] == QLatin1Char('E'))
                                 && (d == QLatin1Char('+') || d == QLatin1Char('-'));
                if (d.isLetterOrNumber() || d == QLatin1Char('.') || exp)
                    ++j;
                else
                    break;
            }
            out += span(theme.number, code.mid(i, j - i));
            i = j;
            continue;
        }

        // identifiers, keywords
        if (c.isLetter() || c == QLatin1Char('_'))
        {
            int j = i + 1;
            while (j < n && (code[j].isLetterOrNumber() || code[j] == QLatin1Char('_')))
                ++j;
            const QString word = code.mid(i, j - i);
            if (keywords().contains(word))
                out += span(theme.keyword, word);
            else if (builtins().contains(word))
                out += span(theme.builtin, word);
            else
            {
                int k = j;
                while (k < n && (code[k] == QLatin1Char(' ') || code[k] == QLatin1Char('\t')))
                    ++k;
                if (k < n && (code[k] == QLatin1Char('(') || code[k] == QLatin1Char('"')
                              || code[k] == QLatin1Char('{')))
                    out += span(theme.function, word);
                else
                    out += escape(word);
            }
            i = j;
            continue;
        }

        out += escape(QString(c));
        ++i;
    }

    return QStringLiteral("<pre style=\"%1\">%2</pre>")
        .arg(QLatin1String(kPreStyle), out);
}
