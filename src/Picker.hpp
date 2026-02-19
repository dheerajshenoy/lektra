#pragma once
#include <QLineEdit>
#include <QListView>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QWidget>

class Picker : public QWidget
{
    Q_OBJECT
public:
    explicit Picker(
        QWidget *parent) noexcept; // parent is REQUIRED — must be the main
                                   // window's central widget

    // Keep PickerKeyBindings in the header since Config needs to produce one
    struct Keybindings
    {
        QKeyCombination moveDown{Qt::Key_Down};
        QKeyCombination pageDown{Qt::Key_PageDown};
        QKeyCombination moveUp{Qt::Key_Up};
        QKeyCombination pageUp{Qt::Key_PageUp};
        QKeyCombination accept{Qt::Key_Return};
        QKeyCombination dismiss{Qt::Key_Escape};
    };

    struct FrameStyle
    {
        bool border{true};
        bool shadow{true};
        int shadow_blur_radius{18};
        int shadow_offset_x{0};
        int shadow_offset_y{6};
        int shadow_opacity{120};
    };

    struct Item
    {
        QString title;
        QString subtitle;
        QVariant data;
    };

    virtual QList<Item> collectItems()            = 0;
    virtual void onItemAccepted(const Item &item) = 0;

    void launch();
    void repopulate() noexcept;

    inline void setKeybindings(const Keybindings &keys) noexcept
    {
        m_keys = keys;
    }

    inline const Keybindings &keybindings() const noexcept
    {
        return m_keys;
    }

    virtual Qt::CaseSensitivity caseSensitivity(const QString &term) const
    {
        for (QChar c : term)
            if (c.isUpper())
                return Qt::CaseSensitive;
        return Qt::CaseInsensitive;
    }

signals:
    void itemSelected(const Item &item);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSearchChanged(const QString &text);
    void onItemActivated(const QModelIndex &index);
    virtual void onFilterChanged(int visibleCount)
    {
        Q_UNUSED(visibleCount)
    }

private:
    void populate(const QList<Item> &items);
    void reposition();
    void applyFrameStyle() noexcept;
    Item itemAtProxyIndex(const QModelIndex &index) const;

    QFrame *m_frame{nullptr};
    QLineEdit *m_searchBox;
    QListView *m_listView;
    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    Keybindings m_keys;
    FrameStyle m_frame_style{};
    class QGraphicsDropShadowEffect *m_shadow_effect{nullptr};
};
