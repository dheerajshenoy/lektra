#pragma once

#include "StatusbarLayout.hpp"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

// Reads the `[statusbar].layout` option into rows of items for StatusbarLayout.
//
// The option is a list. A list of items is one row, a list of lists is several
// rows. An item is one of:
//
//   "name"                 a module (see statusbarModules())
//   "|"                    a flexible gap: takes the free space (stretch 1)
//   { module = "name", ... }   a module with options
//   { text = "...", ... }      a fixed piece of text, e.g. a separator
//   { spacer = 12 }            a gap of 12 px
//   { stretch = 2 }            a flexible gap that takes twice the share
//
// Options of a module or text: stretch, min_width, max_width, margin (a number
// or {left, right}), align ("left" | "center" | "right"), and for absolute
// placement `at` (0 to 1: the position along the bar) with `anchor` ("left" |
// "center" | "right": which edge of the item is put there).
namespace statusbar_layout
{
struct Item
{
    enum class Kind
    {
        Module,
        Text,
        Gap
    };
    Kind kind = Kind::Gap;
    QString name; // the module, or the text
    StatusbarLayout::Spec spec;
};
using Rows = QList<QList<Item>>;

inline const QStringList &
modules()
{
    static const QStringList names
        = {"session",  "filename", "page",   "zoom",
           "progress", "mode",     "portal", "narrow"};
    return names;
}

// A module name looks like an identifier. Names other than the built-in
// modules are the custom segments a script adds (lektra.statusbar.register);
// they may be named before the script has run, so they are not checked here.
inline bool
isModuleName(const QString &name)
{
    if (name.isEmpty()
        || !(name.at(0).isLetter() || name.at(0) == QLatin1Char('_')))
        return false;
    for (const QChar c : name)
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_')
              || c == QLatin1Char('-') || c == QLatin1Char('.')))
            return false;
    return true;
}

inline QString
canonicalModule(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("pagenumber") || n == QLatin1String("page_number"))
        return QStringLiteral("page");
    return n;
}

inline bool
isList(const QVariant &v)
{
    return v.typeId() == QMetaType::QVariantList
           || v.typeId() == QMetaType::QStringList;
}

inline bool
asNumber(const QVariant &v, double &out)
{
    if (v.typeId() == QMetaType::QString || v.typeId() == QMetaType::Bool
        || !v.canConvert<double>())
        return false;
    out = v.toDouble();
    return true;
}

inline Qt::Alignment
alignmentOf(const QString &text, bool &ok)
{
    const QString t = text.trimmed().toLower();
    ok              = true;
    if (t == QLatin1String("left"))
        return Qt::AlignLeft;
    if (t == QLatin1String("center") || t == QLatin1String("centre"))
        return Qt::AlignHCenter;
    if (t == QLatin1String("right"))
        return Qt::AlignRight;
    ok = false;
    return Qt::AlignLeft;
}

// One item. Returns false (with a message in `warnings`) if it is not valid.
inline bool
parseItem(const QVariant &value, Item &out, QStringList &warnings)
{
    out = Item{};

    if (value.typeId() == QMetaType::QString)
    {
        const QString text = value.toString().trimmed();
        if (text == QLatin1String("|"))
        {
            out.kind         = Item::Kind::Gap;
            out.spec.gap     = true;
            out.spec.stretch = 1;
            return true;
        }
        const QString name = canonicalModule(text);
        if (!isModuleName(name))
        {
            warnings << QStringLiteral("unknown statusbar module \"%1\" (the "
                                       "built-in ones are: %2)")
                            .arg(text, modules().join(QStringLiteral(", ")));
            return false;
        }
        out.kind = Item::Kind::Module;
        out.name = name;
        return true;
    }

    if (value.typeId() != QMetaType::QVariantMap)
    {
        warnings << QStringLiteral(
            "a statusbar layout item must be a string or a table");
        return false;
    }

    const QVariantMap map = value.toMap();
    static const QStringList known
        = {"module",    "text",   "spacer", "stretch", "min_width",
           "max_width", "margin", "align",  "at",      "anchor"};
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        if (!known.contains(it.key()))
            warnings << QStringLiteral(
                            "unknown key \"%1\" in a statusbar layout item")
                            .arg(it.key());

    const bool hasModule = map.contains("module");
    const bool hasText   = map.contains("text");
    const bool hasSpacer = map.contains("spacer");
    if (hasModule + hasText + hasSpacer > 1)
    {
        warnings << QStringLiteral(
            "a statusbar layout item has only one of module, text and spacer");
        return false;
    }

    double number               = 0;
    StatusbarLayout::Spec &spec = out.spec;

    if (hasModule)
    {
        const QString name = canonicalModule(map.value("module").toString());
        if (!isModuleName(name))
        {
            warnings << QStringLiteral("unknown statusbar module \"%1\" (the "
                                       "built-in ones are: %2)")
                            .arg(map.value("module").toString(),
                                 modules().join(QStringLiteral(", ")));
            return false;
        }
        out.kind = Item::Kind::Module;
        out.name = name;
    }
    else if (hasText)
    {
        out.kind = Item::Kind::Text;
        out.name = map.value("text").toString();
    }
    else
    {
        // A gap: { spacer = px } and/or { stretch = weight }.
        out.kind = Item::Kind::Gap;
        spec.gap = true;
        if (hasSpacer)
        {
            if (!asNumber(map.value("spacer"), number) || number < 0)
            {
                warnings << QStringLiteral(
                    "\"spacer\" is a width in pixels (a number)");
                return false;
            }
            spec.gapWidth = static_cast<int>(number);
        }
        else if (!map.contains("stretch"))
        {
            warnings << QStringLiteral("a statusbar layout item needs module, "
                                       "text, spacer or stretch");
            return false;
        }
    }

    if (map.contains("stretch"))
    {
        if (!asNumber(map.value("stretch"), number) || number < 0)
            warnings << QStringLiteral("\"stretch\" is a number, 0 or more");
        else
            spec.stretch = number;
    }

    if (out.kind == Item::Kind::Gap)
    {
        if (map.contains("at") || map.contains("anchor")
            || map.contains("align") || map.contains("margin")
            || map.contains("min_width") || map.contains("max_width"))
            warnings << QStringLiteral("a gap only takes spacer and stretch");
        return true;
    }

    if (map.contains("min_width"))
    {
        if (asNumber(map.value("min_width"), number) && number >= 0)
            spec.minWidth = static_cast<int>(number);
        else
            warnings << QStringLiteral("\"min_width\" is a width in pixels");
    }
    if (map.contains("max_width"))
    {
        if (asNumber(map.value("max_width"), number) && number >= 0)
            spec.maxWidth = static_cast<int>(number);
        else
            warnings << QStringLiteral("\"max_width\" is a width in pixels");
    }
    if (map.contains("margin"))
    {
        const QVariant m = map.value("margin");
        if (isList(m) && m.toList().size() == 2)
        {
            double l = 0, r = 0;
            if (asNumber(m.toList().at(0), l) && asNumber(m.toList().at(1), r))
            {
                spec.marginLeft  = static_cast<int>(l);
                spec.marginRight = static_cast<int>(r);
            }
            else
                warnings << QStringLiteral(
                    "\"margin\" is a number or {left, right}");
        }
        else if (asNumber(m, number))
            spec.marginLeft = spec.marginRight = static_cast<int>(number);
        else
            warnings << QStringLiteral(
                "\"margin\" is a number or {left, right}");
    }
    if (map.contains("align"))
    {
        bool ok    = false;
        spec.align = alignmentOf(map.value("align").toString(), ok);
        if (!ok)
            warnings << QStringLiteral("\"align\" is left, center or right");
    }
    if (map.contains("at"))
    {
        if (asNumber(map.value("at"), number))
        {
            spec.absolute = true;
            spec.at       = std::clamp(number, 0.0, 1.0);
        }
        else
            warnings << QStringLiteral("\"at\" is a position from 0 to 1");
    }
    if (map.contains("anchor"))
    {
        bool ok     = false;
        spec.anchor = alignmentOf(map.value("anchor").toString(), ok);
        if (!ok)
            warnings << QStringLiteral("\"anchor\" is left, center or right");
        if (!spec.absolute)
            warnings << QStringLiteral(
                "\"anchor\" only matters together with \"at\"");
    }
    return true;
}

// The whole option. Items that are not valid are left out (and reported in
// `warnings`), a module that is listed twice is only placed the first time.
inline Rows
parse(const QVariantList &layout, QStringList *warnings = nullptr)
{
    QStringList messages;
    Rows rows;

    const bool multiRow = !layout.isEmpty() && isList(layout.first());
    QList<QVariantList> sources;
    if (multiRow)
    {
        for (const QVariant &row : layout)
        {
            if (isList(row))
                sources.append(row.toList());
            else
                messages << QStringLiteral(
                    "when the layout has rows, every row must be a list");
        }
    }
    else
        sources.append(layout);

    QStringList seen;
    for (const QVariantList &source : std::as_const(sources))
    {
        QList<Item> row;
        for (const QVariant &value : source)
        {
            Item item;
            if (!parseItem(value, item, messages))
                continue;
            if (item.kind == Item::Kind::Module)
            {
                if (seen.contains(item.name))
                {
                    messages
                        << QStringLiteral("statusbar module \"%1\" is listed "
                                          "twice; only the first is used")
                               .arg(item.name);
                    continue;
                }
                seen << item.name;
            }
            row.append(item);
        }
        rows.append(row);
    }
    if (warnings)
        *warnings += messages;
    return rows;
}

// What the statusbar looked like before the layout could be set: the file
// information on the left, the page number in the middle of the bar, the
// progress and the mode on the right.
inline QVariantList
defaultLayout()
{
    return {QStringLiteral("session"),
            QStringLiteral("filename"),
            QStringLiteral("portal"),
            QStringLiteral("narrow"),
            QStringLiteral("|"),
            QStringLiteral("progress"),
            QStringLiteral("mode"),
            QVariantMap{{"module", "page"}, {"at", 0.5}, {"anchor", "center"}}};
}
} // namespace statusbar_layout
