#pragma once

#include "Config.hpp"
#include "Picker.hpp"

#include <functional>
#include <vector>

class CommandPicker : public Picker
{
    Q_OBJECT
public:
    using ActionMap = QHash<QString, std::function<void(const QStringList &)>>;
    using ShortcutMap = QHash<QString, QString>;
    explicit CommandPicker(const Config::Command_palette &config,
                           const ActionMap &actions,
                           const ShortcutMap &shortcuts,
                           QWidget *parent) noexcept;

    void registerCommand(const QString &name, const QString &shortcut,
                         std::function<void()> action);

    // Picker interface
    QList<Item> collectItems() override;
    void onItemAccepted(const Item &item) override;

signals:
    // Preserve your original signal for compatibility
    void commandSelected(const QString &name, const QStringList &args = {});

private:
    struct Entry
    {
        QString name;
        QString shortcut;
        std::function<void(const QStringList &)> action;
    };
    const Config::Command_palette &m_config;
    std::vector<Entry> m_entries; // sorted, stable
};
