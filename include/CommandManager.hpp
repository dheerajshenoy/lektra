#pragma once

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
        m_commands[name] = {description, std::move(action)};
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

private:
    Commands m_commands;
};
