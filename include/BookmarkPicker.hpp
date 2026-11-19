#pragma once

#include "Config.hpp"
#include "Picker.hpp"

class BookmarkManager;

class BookmarkPicker : public Picker
{
    Q_OBJECT
public:
    // Open: accepting an entry opens its file. Remove: accepting an entry
    // asks for that bookmark to be deleted.
    enum class Mode
    {
        Open,
        Remove,
    };

    explicit BookmarkPicker(const Config::Picker &config,
                            BookmarkManager *bookmarkManager,
                            QWidget *parent = nullptr);

    void setMode(Mode mode) noexcept;

    QList<Item> collectItems() override;
    void onItemAccepted(const Item &item) override;

signals:
    void fileOpenRequested(const QString &path);
    void bookmarkRemoveRequested(const QString &id);

private:
    BookmarkManager *m_bookmark_manager = nullptr;
    Mode m_mode                         = Mode::Open;
};
