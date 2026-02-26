#pragma once

#include <QStringList>
#include <functional>
#include <vector>

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
    inline void reg(Command cmd) noexcept
    {
        m_commands.push_back(std::move(cmd));
    }

    inline void
    reg(const QString &name, const QString &description,
        std::function<void(const QStringList &args)> action) noexcept
    {
        m_commands.push_back({name, description, std::move(action)});
    }

    const std::vector<Command> &commands() const noexcept
    {
        return m_commands;
    }

    const QStringList commandNames() const noexcept
    {
        QStringList names;
        for (const auto &cmd : m_commands)
        {
            names.push_back(cmd.name);
        }
        return names;
    }

    const Command *find(const QString &name) const noexcept
    {
        for (const auto &cmd : m_commands)
        {
            if (cmd.name == name)
            {
                return &cmd;
            }
        }
        return nullptr;
    }

private:
    std::vector<Command> m_commands;
};
