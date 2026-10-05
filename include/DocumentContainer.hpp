#pragma once

#include "Config.hpp"
#include "DocumentView.hpp"
#include "ThumbnailView.hpp"

#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

class ViewPickOverlay;

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

    inline void toggleThumbnailView() noexcept
    {
        if (m_thumbnail_view)
            m_thumbnail_view->setVisible(!m_thumbnail_view->isVisible());
    }

    inline ThumbnailView *thumbnailView() const noexcept
    {
        return m_thumbnail_view;
    }

    inline void focusThumbnailView() noexcept
    {
        if (m_thumbnail_view)
            m_thumbnail_view->setFocus();
    }

    inline bool isMaximized() const noexcept
    {
        return m_maximized;
    }

    void closeThumbnailView() noexcept;

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

    void toggleMaximizeSplit() noexcept;

    // Thumbnail view management
    void createThumbnailView(DocumentView *view) noexcept;
    void resizeThumbnailView(float relWidth) noexcept;

    // Linking views: while linked, zooming or scrolling one of them does the
    // same in the others. Fewer than two views means no link.
    void sync_views() noexcept; // all views of this container
    void sync_views(const QList<DocumentView *> &views) noexcept;
    void stop_sync() noexcept;
    bool isSynced() const noexcept;
    // Lets the user pick the views to link (numbers drawn on the views).
    void select_views() noexcept;

signals:
    void viewCreated(DocumentView *view);
    void viewClosed(DocumentView *view);
    void currentViewChanged(DocumentView *view);

private:
    void splitInSplitter(QSplitter *splitter, DocumentView *view,
                         DocumentView *newView,
                         Qt::Orientation orientation) noexcept;

    void equalizeAll(QWidget *widget) noexcept;
    bool containsView(QWidget *widget, DocumentView *view) const noexcept;
    void collectViews(QWidget *widget,
                      QList<DocumentView *> &views) const noexcept;

    void equalizeStretch(QSplitter *splitter) noexcept;
    DocumentView *createViewFromTemplate(DocumentView *templateView) noexcept;

private:
    QVBoxLayout *m_layout{nullptr};
    DocumentView *m_current_view{nullptr};
    ThumbnailView *m_thumbnail_view{nullptr};
    bool m_maximized{false};
        enum SyncWhat
    {
        SyncZoom     = 1,
        SyncFit      = 2,
        SyncRotation = 4,
    };
    void scheduleSync(DocumentView *source, int what) noexcept;
    void flushSync() noexcept;

    QList<QPointer<DocumentView>> m_synced_views;
    QList<QMetaObject::Connection> m_sync_connections;
    QPointer<ViewPickOverlay> m_pick_overlay;
    bool m_syncing = false;
    // The view whose change is being copied to the others, and when that was
    // last done. The changes the others go through meanwhile (their pages
    // are laid out again) are echoes and must not be copied back.
    QPointer<DocumentView> m_sync_source;
    QElapsedTimer m_sync_clock;
    bool m_sync_pending    = false;
    int m_sync_dirty       = 0;
};
