#pragma once
#include "Picker.hpp"

class LuaPicker : public Picker
{
    Q_OBJECT
public:
    struct LuaItem
    {
        QStringList columns;
        int flat_index = -1;
        QList<LuaItem> children;
    };

    explicit LuaPicker(const Config::Picker &config,
                       QWidget *parent = nullptr) noexcept;

    void setItems(const QList<LuaItem> &items) noexcept;

signals:
    void itemAccepted(QString text); // first column of accepted item

protected:
    QList<Picker::Item> collectItems() override;
    void onItemAccepted(const Picker::Item &item) override;

private:
    QList<LuaItem> m_items; // hierarchical source
    QList<LuaItem> m_flat;  // flat index → LuaItem, built by setItems()

    static Picker::Item toPickerItem(const LuaItem &e);
};
