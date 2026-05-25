#pragma once

#include "Picker.hpp"

class FilePicker : public Picker
{
    Q_OBJECT
public:
    explicit FilePicker(const Config::Picker &config,
                        QWidget *parent = nullptr) noexcept;

    QList<Item> collectItems() override;
    void onItemAccepted(const Item &item) override;
    void launch() noexcept override;

signals:
    void fileRequested(const QString &path);

protected:
    void onFilterChanged(int visibleCount) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void navigateTo(const QString &dir, const QString &filter) noexcept;
    void completeFromBestMatch() noexcept;

    QString m_currentDir;
};
