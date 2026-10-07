#include "CommandPicker.hpp"

#include "CommandManager.hpp"
#include "Picker.hpp"

#include <algorithm>
#include <numeric>

CommandPicker::CommandPicker(const Config::CommandPalette &config,
                             const std::vector<Command> &commands,
                             const ShortcutMap &shortcuts,
                             CommandManager *command_manager,
                             QWidget *parent) noexcept
    : Picker(config, parent), m_config(config), m_commands(commands),
      m_shortcuts(shortcuts), m_command_manager(command_manager)
{
    QList<Column> cols;
    cols.append({.header    = "Command",
                 .stretch   = 1,
                 .alignment = Qt::AlignLeft | Qt::AlignVCenter});
    if (m_config.description)
        cols.append({.header = "Description", .stretch = 2});
    if (m_config.show_shortcuts)
        cols.append({.header    = "Shortcut",
                     .stretch   = 0,
                     .alignment = Qt::AlignRight | Qt::AlignVCenter});
    setScrollbarEnabled(m_config.vscrollbar);
    setColumns(cols);

    setStructureMode(StructureMode::Flat);

    setPrompt(config.prompt);
}

QList<Picker::Item>
CommandPicker::collectItems()
{
    // Order in which m_commands is walked — identity (registration order)
    // by default, or most-used-first when sort_by_frequency is on. Sorting
    // indices rather than the Command vector itself keeps `.data` (used by
    // onItemAccepted to index back into m_commands) trivial: it's always
    // the *original* index, regardless of display order.
    std::vector<size_t> order(m_commands.size());
    std::iota(order.begin(), order.end(), 0);

    if (m_config.sort_by_frequency && m_command_manager)
    {
        std::stable_sort(order.begin(), order.end(),
                         [this](size_t a, size_t b)
        {
            return m_command_manager->usageCount(m_commands[a].name)
                 > m_command_manager->usageCount(m_commands[b].name);
        });
    }

    QList<Item> items;
    items.reserve(static_cast<int>(order.size()));

    for (size_t i : order)
    {
        const Command &cmd = m_commands[i];
        QList<QString> cols;
        cols.append(cmd.name);
        if (m_config.description)
            cols.append(cmd.description);
        if (m_config.show_shortcuts)
            cols.append(m_shortcuts.value(cmd.name).join(", "));
        items.push_back(
            {.columns = cols, .data = static_cast<quint64>(i), .children = {}});
    }
    return items;
}

void
CommandPicker::onItemAccepted(const Item &item)
{
    const size_t i = item.data.toULongLong();
    if (i >= m_commands.size())
        return;
    const Command &cmd = m_commands[i];
    if (m_command_manager)
        m_command_manager->recordUsage(cmd.name);
    if (cmd.action)
        cmd.action({});
}
