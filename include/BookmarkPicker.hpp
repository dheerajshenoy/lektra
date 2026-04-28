#pragma once

#include "Config.hpp"
#include "Picker.hpp"

class BookmarkManager;

class BookmarkPicker : public Picker
{
    Q_OBJECT
public:
    explicit BookmarkPicker(const Config::Picker &config,
                            BookmarkManager *bookmarkManager,
                            QWidget *parent = nullptr);

    QList<Item> collectItems() override;
    void onItemAccepted(const Item &item) override;

signals:
    void fileOpenRequested(const QString &path);

private:
    BookmarkManager *m_bookmark_manager = nullptr;
};
