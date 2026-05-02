#include "LuaPicker.hpp"

LuaPicker::LuaPicker(const Config::Picker &config, QWidget *parent) noexcept
    : Picker(config, parent)
{
    setPrompt(config.prompt);
}

void
LuaPicker::setItems(const QList<LuaItem> &items) noexcept
{
    m_items = items;
    m_flat.clear();

    // DFS to build flat lookup table, assigning flat_index in-place
    std::function<void(QList<LuaItem> &)> flatten = [&](QList<LuaItem> &nodes)
    {
        for (auto &node : nodes)
        {
            node.flat_index = m_flat.size();
            m_flat.append(node);
            if (!node.children.isEmpty())
                flatten(node.children);
        }
    };
    flatten(m_items);
}

Picker::Item
LuaPicker::toPickerItem(const LuaItem &e)
{
    Picker::Item node;
    node.columns = e.columns;
    node.data    = static_cast<qulonglong>(e.flat_index);
    for (const auto &child : e.children)
        node.children.append(toPickerItem(child));
    return node;
}

QList<Picker::Item>
LuaPicker::collectItems()
{
    QList<Picker::Item> out;
    out.reserve(m_items.size());
    for (const auto &e : m_items)
        out.append(toPickerItem(e));
    return out;
}

void
LuaPicker::onItemAccepted(const Picker::Item &item)
{
    const int i = static_cast<int>(item.data.toULongLong());
    if (i >= 0 && i < m_flat.size())
        emit itemAccepted(m_flat[i].columns.value(0));
}
