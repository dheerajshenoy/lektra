#pragma once
#include "Picker.hpp"

class RecentFilesPicker : public Picker
{
    Q_OBJECT
public:
    explicit RecentFilesPicker(QWidget *parent = nullptr);

    QList<Item> collectItems() override;
    void onItemAccepted(const Item &item) override;
    inline void setRecentFiles(const QStringList &files) noexcept
    {
        m_recentFiles = files;
    }

signals:
    void fileRequested(const QString &path);

private:
    QStringList m_recentFiles; // Inject or set from app settings
};
