#pragma once
#include "Config.hpp"

#include <QLineEdit>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QWidget>

class QGraphicsDropShadowEffect;

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

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange();
#else
        invalidateFilter();
#endif
    }

    SearchModes searchModes() const noexcept
    {
        return m_modes;
    }

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

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange();
#else
        invalidateFilter();
#endif
    }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override
    {
        if (m_raw.isEmpty())
            return true;

        QModelIndex idx  = sourceModel()->index(sourceRow, 0, sourceParent);
        QString haystack = idx.data(filterRole()).toString();

        if (matches(haystack))
            return true;

        int childCount = sourceModel()->rowCount(idx);

        for (int i = 0; i < childCount; ++i)
            if (filterAcceptsRow(i, idx))
                return true;

        return false;
    }

private:
    bool matches(const QString &haystack) const
    {
        if (m_modes & Regex)
            return m_regex.match(haystack).hasMatch();

        if (m_modes & Orderless)
        {
            for (const QString &token : m_tokens)
                if (!haystack.contains(token, m_cs))
                    return false;
            return true;
        }

        return haystack.contains(m_raw, m_cs);
    }

private:
    SearchModes m_modes      = SearchMode::Orderless;
    Qt::CaseSensitivity m_cs = Qt::CaseInsensitive;
    QRegularExpression m_regex;
    QStringList m_tokens;
    QString m_raw;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PickerFilterProxy::SearchModes)

class Picker : public QWidget
{
    Q_OBJECT
public:
    explicit Picker(const Config::Picker &config, QWidget *parent) noexcept;

    struct Keybindings
    {
        QList<QKeyCombination> moveUp
            = {Qt::Key_Up, Qt::ControlModifier | Qt::Key_K};
        QList<QKeyCombination> moveDown
            = {Qt::Key_Down, Qt::ControlModifier | Qt::Key_J};
        QList<QKeyCombination> pageUp   = {Qt::Key_PageUp};
        QList<QKeyCombination> pageDown = {Qt::Key_PageDown};
        QList<QKeyCombination> sectionPrev{
            Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_Up,
            Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_K};
        QList<QKeyCombination> sectionNext{
            Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_Down,
            Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_J};
        QList<QKeyCombination> accept   = {Qt::Key_Return};
        QList<QKeyCombination> expand   = {Qt::Key_Tab};
        QList<QKeyCombination> collapse = {Qt::Key_Tab};
        QList<QKeyCombination> dismiss  = {Qt::Key_Escape};
    };

    struct Column
    {
        QString header;
        int role{Qt::DisplayRole};
        int stretch{1};
        Qt::Alignment alignment{Qt::AlignLeft | Qt::AlignVCenter};
    };

    struct Item
    {
        QList<QString> columns;
        QVariant data;
        QList<Item> children;
    };

    enum class StructureMode
    {
        Flat = 0,
        Hierarchical,
    };

    inline StructureMode structureMode() const noexcept
    {
        return m_structureMode;
    }

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

    virtual Qt::CaseSensitivity caseSensitivity(const QString &term) const
    {
        for (QChar c : term)
            if (c.isUpper())
                return Qt::CaseSensitive;
        return Qt::CaseInsensitive;
    }

    inline void setScrollbarEnabled(bool enabled) noexcept
    {
        m_listView->setVerticalScrollBarPolicy(
            enabled ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    }

    inline void setAlternatingRowColors(bool enabled) noexcept
    {
        m_listView->setAlternatingRowColors(enabled);
    }

    virtual QList<Item> collectItems()            = 0;
    virtual void onItemAccepted(const Item &item) = 0;
    virtual void launch() noexcept;

    void repopulate() noexcept;
    void setStructureMode(StructureMode mode) noexcept;
    void setPrompt(const QString &prompt) noexcept;

signals:
    void itemSelected(const Item &item);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void populate(const QList<Item> &items);
    void reposition();

protected:
    PickerFilterProxy *m_proxy = nullptr;
    QTreeView *m_listView      = nullptr;
    QLineEdit *m_searchBox     = nullptr;
    const Config::Picker &m_config;

private slots:
    void onSearchChanged(const QString &text);
    void onItemClicked(const QModelIndex &index);
    void onItemActivated(const QModelIndex &index);
    virtual void onFilterChanged(int visibleCount)
    {
        Q_UNUSED(visibleCount)
    }

private:
    void applyFrameStyle() noexcept;
    Item itemAtProxyIndex(const QModelIndex &index) const;

private:
    QLabel *m_promptLabel                      = nullptr;
    QFrame *m_frame                            = nullptr;
    QStandardItemModel *m_model                = nullptr;
    StructureMode m_structureMode              = StructureMode::Hierarchical;
    QGraphicsDropShadowEffect *m_shadow_effect = nullptr;
    Keybindings m_keys;
    QVector<Column> m_columns;
};
