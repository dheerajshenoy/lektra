#pragma once
#include "Config.hpp"
#include "HighlightDelegate.hpp"

#include <QLineEdit>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>
#include <QWidget>

class QGraphicsDropShadowEffect;

class PickerFilterProxy : public QSortFilterProxyModel, public MatchHighlighter
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

    // MatchHighlighter
    bool isActive() const override
    {
        return !m_raw.isEmpty();
    }
    QVector<QPair<int, int>> ranges(const QString &text) const override
    {
        QVector<QPair<int, int>> out;
        if (text.isEmpty() || m_raw.isEmpty())
            return out;

        if (m_modes & Regex)
        {
            if (!m_regex.isValid())
                return out;
            auto it = m_regex.globalMatch(text);
            while (it.hasNext())
            {
                auto m = it.next();
                // Skip zero-length matches to avoid infinite loops / no-op
                // boxes
                if (m.capturedLength() > 0)
                    out.append({m.capturedStart(), m.capturedLength()});
            }
            return out;
        }

        if (m_modes & Orderless)
        {
            for (const QString &tok : m_tokens)
            {
                if (tok.isEmpty())
                    continue;
                int from = 0;
                while (true)
                {
                    int i = text.indexOf(tok, from, m_cs);
                    if (i < 0)
                        break;
                    out.append({i, tok.size()});
                    from = i + tok.size();
                }
            }
            // Ranges were appended per-token, so they're not globally sorted
            // and may overlap (e.g. tokens "ab" and "bc" in "abc"). Merge them.
            return mergeRanges(out);
        }

        // Fixed
        {
            int from = 0;
            while (true)
            {
                int i = text.indexOf(m_raw, from, m_cs);
                if (i < 0)
                    break;
                out.append({i, m_raw.size()});
                from = i + m_raw.size();
            }
        }
        return out;
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
    static QVector<QPair<int, int>> mergeRanges(QVector<QPair<int, int>> in)
    {
        if (in.size() < 2)
            return in;
        std::sort(in.begin(), in.end(),
                  [](auto &a, auto &b) { return a.first < b.first; });
        QVector<QPair<int, int>> out;
        out.append(in[0]);
        for (int i = 1; i < in.size(); ++i)
        {
            auto &last        = out.last();
            const int lastEnd = last.first + last.second;
            if (in[i].first <= lastEnd)
            {
                last.second = std::max(lastEnd, in[i].first + in[i].second)
                              - last.first;
            }
            else
            {
                out.append(in[i]);
            }
        }
        return out;
    }

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

    using KeyCombs = QList<QKeyCombination>;

    struct Keybindings
    {
        KeyCombs moveUp   = {Qt::Key_Up, Qt::ControlModifier | Qt::Key_K};
        KeyCombs moveDown = {Qt::Key_Down, Qt::ControlModifier | Qt::Key_J};
        KeyCombs pageUp   = {Qt::Key_PageUp};
        KeyCombs pageDown = {Qt::Key_PageDown};
        KeyCombs sectionPrev
            = {Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_Up,
               Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_K};
        KeyCombs sectionNext
            = {Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_Down,
               Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_J};
        KeyCombs accept              = {Qt::Key_Return};
        KeyCombs expand              = {Qt::Key_Tab};
        KeyCombs collapse            = {Qt::Key_Tab};
        KeyCombs dismiss             = {Qt::Key_Escape};
        KeyCombs toggleStructureMode = {Qt::ControlModifier | Qt::Key_Space};
        KeyCombs historyPrev         = {Qt::ControlModifier | Qt::Key_Up};
        KeyCombs historyNext         = {Qt::ControlModifier | Qt::Key_Down};
    };

    struct Column
    {
        QString header;
        int role                = Qt::DisplayRole;
        int stretch             = 1;
        Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;
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

    inline QVector<QString> history() const noexcept
    {
        return m_history;
    }

    virtual QList<Item> collectItems()            = 0;
    virtual void onItemAccepted(const Item &item) = 0;
    virtual void launch() noexcept;

    void historyPrev() noexcept;
    void historyNext() noexcept;
    void repopulate() noexcept;
    void setStructureMode(StructureMode mode) noexcept;
    void setPrompt(const QString &prompt) noexcept;
    void releaseInputGrab() noexcept;

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
    QVector<QString> m_history;
    QLabel *m_promptLabel                      = nullptr;
    QFrame *m_frame                            = nullptr;
    QStandardItemModel *m_model                = nullptr;
    StructureMode m_structureMode              = StructureMode::Hierarchical;
    QGraphicsDropShadowEffect *m_shadow_effect = nullptr;
    bool isPickerKey(const QKeyCombination &key) const noexcept;
    Keybindings m_keys;
    QVector<Column> m_columns;
    HighlightDelegate *m_highlight_delegate = nullptr;
};
