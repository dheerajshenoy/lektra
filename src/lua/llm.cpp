#include "Lektra.hpp"

#ifdef WITH_LLM_SUPPORT

    #include "LuaSandbox.hpp"

    #include <QDir>
    #include <QFile>
    #include <QRegularExpression>
    #include <algorithm>

namespace
{
QString
readResource(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll())
                                          : QString();
}

// The system prompt file has parts for each mode, between
// `<!--if tools-->` / `<!--if classic-->` and `<!--end-->` lines. Keeps the
// parts for `mode` and drops the others.
QString
promptForMode(QString text, const QString &mode)
{
    static const QRegularExpression part(
        QStringLiteral("<!--if (\\w+)-->\\n(.*?)<!--end-->\\n"),
        QRegularExpression::DotMatchesEverythingOption);
    QString out;
    qsizetype last = 0;
    auto it        = part.globalMatch(text);
    while (it.hasNext())
    {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        if (m.captured(1) == mode)
            out += m.captured(2);
        last = m.capturedEnd();
    }
    return out + text.mid(last);
}

// One documented thing in the Lua API: the lines of a stub between blank
// lines (its doc comments and its declaration).
struct ApiEntry
{
    QString module; // the stub file, e.g. "view"
    QString text;
    QString declaration; // its first line of code, for the module outline
};

// The API entries of every stub file. The stubs are compiled in, so this is
// read once.
const QList<ApiEntry> &
apiEntries()
{
    static const QList<ApiEntry> entries = []
    {
        QList<ApiEntry> list;
        const QStringList files = QDir(QStringLiteral(":/llm/stubs/lua"))
                                      .entryList({QStringLiteral("*.lua")},
                                                 QDir::Files, QDir::Name);
        for (const QString &file : files)
        {
            const QString module = file.left(file.size() - 4);
            const QString text
                = readResource(QStringLiteral(":/llm/stubs/lua/") + file);
            QStringList chunk;
            auto flush = [&]
            {
                if (chunk.isEmpty())
                    return;
                ApiEntry entry;
                entry.module = module;
                entry.text   = chunk.join(QLatin1Char('\n'));
                for (const QString &line : std::as_const(chunk))
                {
                    if (line.trimmed().startsWith(QLatin1String("--")))
                    {
                        // a class or enum is declared in a comment
                        if (entry.declaration.isEmpty()
                            && (line.contains(QLatin1String("---@class"))
                                || line.contains(QLatin1String("---@enum"))))
                            entry.declaration = line.trimmed();
                        continue;
                    }
                    entry.declaration = line.trimmed();
                    break;
                }
                list.append(entry);
                chunk.clear();
            };
            for (const QString &line : text.split(QLatin1Char('\n')))
            {
                if (line.trimmed().isEmpty())
                    flush();
                else if (line.trimmed() != QLatin1String("---@meta"))
                    chunk << line;
            }
            flush();
        }
        return list;
    }();
    return entries;
}
} // namespace

// Everything the model needs to know about Lektra: how to answer and how
// scripts run (system_prompt.md) and the commands that exist right now,
// including any registered from Lua. With llm_view.tools the Lua API is not
// included (the model reads it through lookup_api); without, it is the LuaLS
// stubs, which are exactly the documentation of every function.
QString
Lektra::llmSystemPrompt() const noexcept
{
    const bool tools = m_config.llm_view.tools;
    QString prompt   = promptForMode(
        readResource(QStringLiteral(":/llm/src/llm/system_prompt.md")),
        tools ? QStringLiteral("tools") : QStringLiteral("classic"));

    if (!tools)
    {
        QString last;
        for (const ApiEntry &entry : apiEntries())
        {
            if (entry.module != last)
            {
                prompt += QLatin1Char('\n');
                last = entry.module;
            }
            prompt += entry.text + QLatin1Char('\n');
        }
    }

    auto commands = m_command_manager->commands();
    std::sort(commands.begin(), commands.end(),
              [](const Command &a, const Command &b)
    { return a.name < b.name; });
    prompt
        += QStringLiteral("\n# Commands (for %1)\n\n")
               .arg(tools ? QStringLiteral("run_command and lektra.cmd.execute")
                          : QStringLiteral("lektra.cmd.execute"));
    for (const Command &command : commands)
        prompt += QStringLiteral("- `%1`: %2\n")
                      .arg(command.name, command.description);

    return prompt;
}

QString
Lektra::llmLookupApi(const QString &query) const noexcept
{
    constexpr int kMaxEntries = 8;
    constexpr int kMaxChars   = 8000;

    const QList<ApiEntry> &entries = apiEntries();
    QStringList modules;
    for (const ApiEntry &entry : entries)
        if (!modules.contains(entry.module))
            modules << entry.module;
    const QString modulesLine
        = QStringLiteral("Modules: ") + modules.join(QStringLiteral(", "));

    QString q = query.trimmed().toLower();
    if (q.startsWith(QLatin1String("lektra.")))
        q = q.mid(7);
    if (q.endsWith(QLatin1String(".lua")))
        q.chop(4);

    if (q.isEmpty() || q == QLatin1String("index")
        || q == QLatin1String("modules"))
        return modulesLine
               + QStringLiteral("\nGive a module name to list its functions, "
                                "or search words to "
                                "get the matching documentation.");

    // A module name: the list of what it contains, one line each.
    if (modules.contains(q))
    {
        QString out = QStringLiteral("Module `%1`:\n").arg(q);
        for (const ApiEntry &entry : entries)
            if (entry.module == q && !entry.declaration.isEmpty())
                out += entry.declaration + QLatin1Char('\n');
        return out
               + QStringLiteral("\nLook up a name from this list to read its "
                                "documentation.");
    }

    // Otherwise: the entries that mention the most of the words.
    static const QStringList ignored = {
        QStringLiteral("the"), QStringLiteral("a"),   QStringLiteral("an"),
        QStringLiteral("to"),  QStringLiteral("of"),  QStringLiteral("in"),
        QStringLiteral("how"), QStringLiteral("for"), QStringLiteral("lektra")};
    QStringList words;
    for (const QString &w :
         q.split(QRegularExpression(QStringLiteral("[^a-z0-9_]+")),
                 Qt::SkipEmptyParts))
        if (!ignored.contains(w))
            words << w;
    if (words.isEmpty())
        return modulesLine;

    struct Hit
    {
        int score;
        int order;
    };
    QList<Hit> hits;
    for (int i = 0; i < entries.size(); ++i)
    {
        const ApiEntry &entry = entries.at(i);
        const QString text    = entry.text.toLower();
        // "goto page" should find goto_page
        const QString plain   = QString(text).remove(QLatin1Char('_'));
        const QString decl    = entry.declaration.toLower();
        int score             = 0;
        for (int w = 0; w < words.size(); ++w)
        {
            const QString &word = words.at(w);
            if (text.contains(word) || plain.contains(word))
                score += 1;
            if (decl.contains(word))
                score += 2;
            if (w + 1 < words.size() && plain.contains(word + words.at(w + 1)))
                score += 2;
        }
        if (score > 0)
            hits.append({score, i});
    }
    if (hits.isEmpty())
        return QStringLiteral("Nothing in the API matches \"%1\". %2")
            .arg(query, modulesLine);

    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b)
    { return a.score > b.score; });

    QString out;
    int shown = 0;
    for (const Hit &hit : std::as_const(hits))
    {
        const ApiEntry &entry = entries.at(hit.order);
        const QString block   = QStringLiteral("-- module: %1\n%2\n\n")
                                    .arg(entry.module, entry.text);
        if (shown >= kMaxEntries
            || (shown > 0 && out.size() + block.size() > kMaxChars))
            break;
        out += block;
        ++shown;
    }
    if (shown < hits.size())
        out += QStringLiteral("(%1 more matches; use more specific words to "
                              "narrow it down.)")
                   .arg(hits.size() - shown);
    return out;
}

LLMScriptResult
Lektra::runLLMCommand(const QString &name, const QStringList &args) noexcept
{
    LLMScriptResult out;
    out.ok = m_command_manager->execute(name, args);
    if (!out.ok)
        out.error = tr("There is no command named \"%1\"").arg(name);
    return out;
}

LLMScriptResult
Lektra::runLLMScript(const QString &code) noexcept
{
    LLMScriptResult out;
    if (!m_L)
    {
        out.error = tr("Lua is not available");
        return out;
    }
    const LuaScriptResult r = runSandboxedLua(m_L, code);
    out.ok                  = r.ok;
    out.output              = r.output;
    out.value               = r.value;
    out.error               = r.error;
    return out;
}

#endif // WITH_LLM_SUPPORT
