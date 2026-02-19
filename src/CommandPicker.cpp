#include "CommandPicker.hpp"

CommandPicker::CommandPicker(const Config &config, const ActionMap &actionMap,
                             const ShortcutMap &shortcuts,
                             QWidget *parent) noexcept
    : Picker(parent), m_config(config)
{
    m_entries.reserve(static_cast<size_t>(actionMap.size()));

    for (auto it = actionMap.constBegin(); it != actionMap.constEnd(); ++it)
        m_entries.push_back({it.key(), shortcuts.value(it.key()), it.value()});

    std::sort(m_entries.begin(), m_entries.end(),
              [](const Entry &a, const Entry &b)
    { return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0; });
}

QList<Picker::Item>
CommandPicker::collectItems()
{
    QList<Item> items;
    items.reserve(static_cast<int>(m_entries.size()));

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const auto &e = m_entries[i];
        items.push_back({.title    = e.name,
                         .subtitle = e.shortcut,
                         .data     = static_cast<qulonglong>(i)});
    }
    return items;
}

void
CommandPicker::onItemAccepted(const Item &item)
{
    const size_t i = item.data.toULongLong();
    if (i < m_entries.size())
    {
        emit commandSelected(m_entries[i].name);
        if (m_entries[i].action)
            m_entries[i].action(
                {}); // pass empty args, or extend PickerItem to carry them
    }
}
