#pragma once

#include "DocumentView.hpp"

#include <QHash>
#include <QObject>

class MarkManager : public QObject
{
    Q_OBJECT

public:
    explicit MarkManager(QObject *parent = nullptr) : QObject(parent) {}

    struct LocalMark
    {
        DocumentView::PageLocation plocation;
    };

    struct GlobalMark
    {
        DocumentView::Id docId;
        DocumentView::PageLocation plocation;
    };

    // Lowercase — per document
    bool hasLocalMark(const QString &key, DocumentView::Id id) const noexcept
    {
        return m_local_marks.contains({id, key});
    }

    bool removeLocalMark(const QString &key, DocumentView::Id id) noexcept
    {
        if (!m_local_marks.contains({id, key}))
            return false;
        m_local_marks.remove({id, key});
        emit markRemoved(key);
        return true;
    }

    void addLocalMark(const QString &key, DocumentView::Id id,
                      DocumentView::PageLocation location) noexcept
    {
        m_local_marks[{id, key}] = LocalMark{location};
        emit markAdded(key);
    }

    const LocalMark *getLocalMark(const QString &key,
                                  DocumentView::Id id) const noexcept
    {
        auto it = m_local_marks.find({id, key});
        return it != m_local_marks.end() ? &it.value() : nullptr;
    }

    // Uppercase — global across documents
    bool hasGlobalMark(const QString &key) const noexcept
    {
        return m_global_marks.contains(key);
    }

    bool removeGlobalMark(const QString &key) noexcept
    {
        if (!m_global_marks.contains(key))
            return false;
        m_global_marks.remove(key);
        emit markRemoved(key);
        return true;
    }

    void addGlobalMark(const QString &key, DocumentView::Id id,
                       DocumentView::PageLocation location) noexcept
    {
        m_global_marks[key] = GlobalMark{id, location};
        emit markAdded(key);
    }

    const GlobalMark *getGlobalMark(const QString &key) const noexcept
    {
        auto it = m_global_marks.find(key);
        return it != m_global_marks.end() ? &it.value() : nullptr;
    }

    // Convenience — dispatch based on case
    bool isGlobalKey(const QString &key) const noexcept
    {
        return !key.isEmpty() && key[0].isUpper();
    }

    QStringList localKeys(DocumentView::Id id) const noexcept
    {
        QStringList result;
        for (auto it = m_local_marks.cbegin(); it != m_local_marks.cend(); ++it)
            if (it.key().first == id)
                result << it.key().second;
        return result;
    }

    QStringList globalKeys() const noexcept
    {
        return m_global_marks.keys();
    }

    // All keys (local for this doc + all globals)
    QStringList allKeys(DocumentView::Id id) const noexcept
    {
        return localKeys(id) + globalKeys();
    }

signals:
    void markAdded(const QString &key);
    void markRemoved(const QString &key);

private:
    // Key = {docId, markKey} for local marks
    QHash<QPair<DocumentView::Id, QString>, LocalMark> m_local_marks;
    // Key = markKey for global marks
    QHash<QString, GlobalMark> m_global_marks;
};
