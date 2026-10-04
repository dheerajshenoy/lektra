#include "Lektra.hpp"

#ifdef WITH_LLM_SUPPORT

#include "LuaSandbox.hpp"

#include <QDir>
#include <QFile>
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
} // namespace

// Everything the model needs to know about Lektra: how to answer and how
// scripts run (system_prompt.md), the Lua API (the LuaLS stubs, which are
// exactly the documentation of every function), and the commands that exist
// right now, including any registered from Lua.
QString
Lektra::llmSystemPrompt() const noexcept
{
    QString prompt = readResource(QStringLiteral(":/llm/src/llm/system_prompt.md"));

    QStringList stubs
        = QDir(QStringLiteral(":/llm/stubs/lua"))
              .entryList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
    for (const QString &name : std::as_const(stubs))
    {
        const QString text = readResource(QStringLiteral(":/llm/stubs/lua/") + name);
        QStringList kept;
        for (const QString &line : text.split(QLatin1Char('\n')))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed == QStringLiteral("---@meta"))
                continue;
            kept << line;
        }
        prompt += QLatin1Char('\n') + kept.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    auto commands = m_command_manager->commands();
    std::sort(commands.begin(), commands.end(),
              [](const Command &a, const Command &b) { return a.name < b.name; });
    prompt += QStringLiteral("\n# Commands (for lektra.cmd.execute)\n\n");
    for (const Command &command : commands)
        prompt += QStringLiteral("- `%1`: %2\n").arg(command.name, command.description);

    return prompt;
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
    out.ok     = r.ok;
    out.output = r.output;
    out.value  = r.value;
    out.error  = r.error;
    return out;
}

#endif // WITH_LLM_SUPPORT
