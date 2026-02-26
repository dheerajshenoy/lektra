#pragma once
#include <QLineEdit>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QWidget>

// ---------------------------------------------------------------------------
// PickerFilterProxy — replaces QSortFilterProxyModel inside Picker.
// Supports three search modes that can be combined via flags.
// ---------------------------------------------------------------------------
class PickerFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum SearchMode
    {
        Fixed     = 0,      // original substring behaviour (default)
        Orderless = 1 << 0, // split on whitespace; all tokens must match
        Regex     = 1 << 1, // treat the filter string as a regex pattern
    };
    Q_DECLARE_FLAGS(SearchModes, SearchMode)

    explicit PickerFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
    }

    void setSearchModes(SearchModes modes) noexcept
    {
        if (m_modes == modes)
            return;
        m_modes = modes;
        endFilterChange();
    }

    SearchModes searchModes() const noexcept
    {
        return m_modes;
    }

    // Call this instead of setFilterFixedString / setFilterRegularExpression.
    // It recompiles the internal regex / token list so filterAcceptsRow works.
    void setFilterText(const QString &text, Qt::CaseSensitivity cs)
    {
        m_raw = text;
        m_cs  = cs;

        if (m_modes & Regex)
        {
            QRegularExpression::PatternOptions opts
                = (cs == Qt::CaseInsensitive)
                      ? QRegularExpression::CaseInsensitiveOption
                      : QRegularExpression::NoPatternOption;
            m_regex = QRegularExpression(text, opts);
            // Fall back silently to fixed if the pattern is invalid
            if (!m_regex.isValid())
                m_regex = QRegularExpression(QRegularExpression::escape(text),
                                             opts);
        }
        else if (m_modes & Orderless)
        {
            // Pre-split so we don't redo it per row
            m_tokens = text.split(' ', Qt::SkipEmptyParts);
        }

        endFilterChange();
    }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override
    {
        if (m_raw.isEmpty())
            return true;

        const QModelIndex idx
            = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString haystack = idx.data(filterRole()).toString();

        if (m_modes & Regex)
            return m_regex.match(haystack).hasMatch();

        if (m_modes & Orderless)
        {
            for (const QString &token : m_tokens)
                if (!haystack.contains(token, m_cs))
                    return false;
            return true;
        }

        // Fixed (default)
        return haystack.contains(m_raw, m_cs);
    }

private:
    SearchModes m_modes{Orderless};
    QString m_raw;
    Qt::CaseSensitivity m_cs{Qt::CaseInsensitive};
    QRegularExpression m_regex;
    QStringList m_tokens;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PickerFilterProxy::SearchModes)

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

    struct Column
    {
        QString header;
        int role{Qt::DisplayRole}; // which data role to display
        int stretch{1};            // relative width weight
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
        QList<QString> columns;
        QVariant data;
    };

    inline void setSearchModes(PickerFilterProxy::SearchModes modes) noexcept
    {
        m_proxy->setSearchModes(modes);
    }

    inline PickerFilterProxy::SearchModes searchModes() const noexcept
    {
        return m_proxy->searchModes();
    }

    inline void setKeybindings(const Keybindings &keys) noexcept
    {
        m_keys = keys;
    }

    inline const Keybindings &keybindings() const noexcept
    {
        return m_keys;
    }

    inline void setColumns(const QList<Column> &columns) noexcept
    {
        m_columns = columns;
    }

    virtual QList<Item> collectItems()            = 0;
    virtual void onItemAccepted(const Item &item) = 0;

    void launch() noexcept;

    void repopulate() noexcept;

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
    PickerFilterProxy *m_proxy{nullptr};

private slots:
    void onSearchChanged(const QString &text);
    void onItemActivated(const QModelIndex &index);
    virtual void onFilterChanged(int visibleCount)
    {
        Q_UNUSED(visibleCount)
    }

private:
    void populate(const QList<Item> &items); // , bool hierarchical = false);
    void reposition();
    void applyFrameStyle() noexcept;
    Item itemAtProxyIndex(const QModelIndex &index) const;

    QFrame *m_frame{nullptr};
    QLineEdit *m_searchBox;
    QTreeView *m_listView;
    QStandardItemModel *m_model;
    Keybindings m_keys;
    FrameStyle m_frame_style{};
    QVector<Column> m_columns{};
    class QGraphicsDropShadowEffect *m_shadow_effect{nullptr};
};
