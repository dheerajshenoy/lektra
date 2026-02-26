#pragma once

#include "CommandManager.hpp"
#include "Config.hpp"
#include "Picker.hpp"

#include <functional>
#include <vector>

class CommandPicker : public Picker
{
    Q_OBJECT
public:
    using ShortcutMap = QHash<QString, QString>;
    explicit CommandPicker(const Config::Command_palette &config,
                           const std::vector<Command> &commands,
                           const ShortcutMap &shortcuts,
                           QWidget *parent) noexcept;

    void registerCommand(const QString &name, const QString &shortcut,
                         std::function<void()> action);

    // Picker interface
    QList<Item> collectItems() override;
    void onItemAccepted(const Item &item) override;

private:
    const Config::Command_palette &m_config;
    const std::vector<Command> &m_commands; // sorted, stable
    const ShortcutMap &m_shortcuts;
};
