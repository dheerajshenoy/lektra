#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <functional>

class Lektra;

struct Command
{
    QString name;
    QString description;
    std::function<void(const QStringList &args)> action;
};

class CommandManager
{
public:
    using Commands = std::unordered_map<
        QString, std::pair<QString, std::function<void(const QStringList &)>>>;

    inline void unreg(const QString &name) noexcept
    {
        m_commands.erase(name);
    }

    inline void
    reg(const QString &name, const QString &description,
        std::function<void(const QStringList &args)> action) noexcept
    {
        // Every registered command remembers itself as "the last command" so
        // run_last_command works no matter how it was triggered (keybinding,
        // palette, Lua, command line). Palette/repeat commands are skipped:
        // repeating those would be useless.
        m_commands[name]
            = {description,
               [this, name, action = std::move(action)](const QStringList &args)
        {
            if (name != "run_last_command" && name != "command_palette")
            {
                m_last_name = name;
                m_last_args = args;
            }
            action(args);
        }};
    }

    // Runs the command that was run most recently again, with the same
    // arguments. Returns false if nothing has been run yet.
    inline bool runLast() const noexcept
    {
        if (m_last_name.isEmpty())
            return false;
        // Copy: the command may record itself again while running.
        const QString name       = m_last_name;
        const QStringList args   = m_last_args;
        return execute(name, args);
    }

    inline bool execute(const QString &name,
                        const QStringList &args = {}) const noexcept
    {
        auto it = m_commands.find(name);
        if (it != m_commands.end())
        {
            it->second.second(args);
            return true;
        }

        return false;
    }

    inline const std::vector<Command> &const_commands() const noexcept
    {
        static std::vector<Command> cmds;
        cmds.clear();
        for (const auto &[name, pair] : m_commands)
            cmds.push_back({name, pair.first, pair.second});
        return cmds;
    }

    inline const std::vector<Command> commands() const noexcept
    {
        std::vector<Command> cmds;
        for (const auto &[name, pair] : m_commands)
            cmds.push_back({name, pair.first, pair.second});
        return cmds;
    }

    inline const QStringList commandNames() const noexcept
    {
        QStringList names;
        for (const auto &[name, _] : m_commands)
            names << name;
        return names;
    }

    inline bool hasCommand(const QString &name) const noexcept
    {
        return m_commands.find(name) != m_commands.end();
    }

    inline const Command find(const QString &name) const noexcept
    {
        auto it = m_commands.find(name);
        if (it != m_commands.end())
            return Command{name, it->second.first, it->second.second};
        return Command{};
    }

    inline void alias(const QString &existingName,
                      const QString &aliasName) noexcept
    {
        auto it = m_commands.find(existingName);
        if (it != m_commands.end())
            m_commands[aliasName] = it->second;
    }

    // Per-command usage counts, for smex-style "most frequently used first"
    // sorting in CommandPicker (Config::CommandPalette::sort_by_frequency).
    // Recorded only for commands picked through the command palette itself
    // (CommandPicker::onItemAccepted) — not every invocation regardless of
    // source — so the ranking reflects what you look up via the palette,
    // not commands you already have a keybinding for.
    inline void recordUsage(const QString &name) noexcept
    {
        ++m_usage_counts[name];
    }

    inline int usageCount(const QString &name) const noexcept
    {
        auto it = m_usage_counts.find(name);
        return it != m_usage_counts.end() ? it->second : 0;
    }

    // Persist/restore usage counts across sessions (JSON: {"name": count}).
    // Load failures (missing file, bad JSON) are silently treated as "no
    // history yet" — there is nothing to recover, every count just starts
    // at 0.
    inline void loadUsageCounts(const QString &path) noexcept
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            return;
        const QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            m_usage_counts[it.key()] = it.value().toInt();
    }

    inline void saveUsageCounts(const QString &path) const noexcept
    {
        QJsonObject obj;
        for (const auto &[name, count] : m_usage_counts)
            obj[name] = count;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

private:
    Commands m_commands;
    QString m_last_name;
    QStringList m_last_args;
    std::unordered_map<QString, int> m_usage_counts;
};
