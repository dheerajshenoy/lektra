#pragma once

#include "DocumentView.hpp"

#include <QEvent>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

/**
 * DocumentContainer manages a tree of split DocumentView instances within a
 * single tab.
 *
 * This class provides Vim-style split functionality, allowing users to view the
 * same or different documents side-by-side or top-to-bottom within a single
 * tab.
 *
 * Architecture:
 * - Uses nested QSplitter widgets for efficient layout management
 * - Lazy splitter creation (only created when actually splitting)
 * - Automatic cleanup of empty splitters when views are closed
 * - Maintains focus tracking across all views
 */
class DocumentContainer : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentContainer(DocumentView *initialView,
                               QWidget *parent = nullptr);
    ~DocumentContainer();
    enum class Direction
    {
        Up = 0,
        Down,
        Left,
        Right
    };

    inline DocumentView *view() const noexcept
    {
        return m_current_view;
    }

    inline bool has_portal() const noexcept
    {
        return m_portal_view != nullptr;
    }

    inline DocumentView *portal() const noexcept
    {
        return m_portal_view;
    }

    inline void set_portal(DocumentView *portal) noexcept
    {
        m_portal_view = portal;
    }

    inline void clear_portal() noexcept
    {
        m_portal_view = nullptr;
        // TODO: Maybe notify views that the portal was cleared
    }

    void set_portal(DocumentView::Id id) noexcept;
    DocumentView *split(DocumentView *view,
                        Qt::Orientation orientation
                        = Qt::Orientation::Horizontal) noexcept;
    DocumentView *split(DocumentView *view, Qt::Orientation orientation,
                        const QString &filePath) noexcept;
    void closeView(DocumentView *view) noexcept;
    QList<DocumentView *> getAllViews() const noexcept;
    void focusSplit(Direction direction) noexcept;
    void focusView(DocumentView *view) noexcept;
    int getViewCount() const noexcept;
    void syncViewSettings(DocumentView *source, DocumentView *target) noexcept;
    QJsonObject serializeSplits() const noexcept;
    DocumentView *splitEmpty(DocumentView *view,
                             Qt::Orientation orientation) noexcept;
    DocumentView *get_child_view_by_id(DocumentView::Id id) const noexcept;
    void close_other_views(DocumentView *view) noexcept;

signals:
    void viewCreated(DocumentView *view);
    void viewClosed(DocumentView *view);
    void currentViewChanged(DocumentView *view);

protected:
    bool eventFilter(QObject *watched, QEvent *event) noexcept override;

private:
    void splitInSplitter(QSplitter *splitter, DocumentView *view,
                         DocumentView *newView,
                         Qt::Orientation orientation) noexcept;
    bool containsView(QWidget *widget, DocumentView *view) const noexcept;
    void collectViews(QWidget *widget,
                      QList<DocumentView *> &views) const noexcept;

    static void equalizeStretch(QSplitter *splitter) noexcept;
    DocumentView *createViewFromTemplate(DocumentView *templateView) noexcept;
    QVBoxLayout *m_layout{nullptr};
    DocumentView *m_current_view{nullptr};
    DocumentView *m_portal_view{nullptr};
};
