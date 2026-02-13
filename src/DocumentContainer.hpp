#pragma once

#include "DocumentView.hpp"

#include <QEvent>
#include <QHash>
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
    using Id = uint32_t;
    /**
     * Create a container with an initial DocumentView.
     *
     * @param initialView The first view (must not be null)
     * @param parent Parent widget
     */
    explicit DocumentContainer(DocumentView *initialView,
                               QWidget *parent = nullptr);

    ~DocumentContainer();

    inline Id id() const noexcept
    {
        return m_id;
    }

    /**
     * Split the container at the given view with the specified orientation.
     *
     * Creates a new DocumentView that mirrors the settings and document of the
     * specified view, placing it adjacent based on the orientation.
     *
     * @param view The view to split from (must be a view in this container)
     * @param orientation Qt::Horizontal for left/right, Qt::Vertical for
     * top/bottom
     */
    void split(DocumentView *view, Qt::Orientation orientation
                                   = Qt::Orientation::Horizontal) noexcept;

    void split(DocumentView *view, Qt::Orientation orientation,
               const QString &filePath) noexcept;

    /**
     * Close a specific view.
     *
     * If it's the last view, the close operation is prevented.
     * Automatically cleans up empty splitters and refocuses appropriately.
     *
     * @param view The view to close
     */
    void closeView(DocumentView *view) noexcept;

    /**
     * Get all DocumentView instances in the container.
     *
     * Views are returned in tree-traversal order (depth-first).
     *
     * @return List of all DocumentView instances
     */
    QList<DocumentView *> getAllViews() const noexcept;

    /**
     * Get the currently focused view.
     *
     * @return The current DocumentView or nullptr if none
     */
    DocumentView *getCurrentView() const noexcept;

    /**
     * Focus the next view in cyclic order.
     *
     * Does nothing if there's only one view.
     */
    void focusNextView() noexcept;

    /**
     * Focus the previous view in cyclic order.
     *
     * Does nothing if there's only one view.
     */
    void focusPrevView() noexcept;

    /**
     * Focus a specific view.
     *
     * @param view The view to focus (must be in this container)
     */
    void focusView(DocumentView *view) noexcept;

    /**
     * Get the number of views in this container.
     *
     * @return Number of DocumentView instances
     */
    int getViewCount() const noexcept;

    /**
     * Synchronize settings from one view to another.
     *
     * Only syncs if both views are showing the same document.
     * Syncs: zoom, fit mode, page number, color inversion.
     *
     * @param source View to copy settings from
     * @param target View to copy settings to
     */
    void syncViewSettings(DocumentView *source, DocumentView *target) noexcept;

signals:
    /**
     * Emitted when a new view is created via splitting.
     *
     * @param view The newly created view
     */
    void viewCreated(DocumentView *view);

    /**
     * Emitted when a view is closed.
     *
     * @param view The view that was closed (will be deleted shortly)
     */
    void viewClosed(DocumentView *view);

    /**
     * Emitted when the active/focused view changes.
     *
     * @param view The newly focused view
     */
    void currentViewChanged(DocumentView *view);

protected:
    /**
     * Event filter to track focus changes.
     */
    bool eventFilter(QObject *watched, QEvent *event) noexcept override;

private:
    /**
     * Helper to split within an existing splitter.
     *
     * Handles both same-orientation and cross-orientation splits.
     */
    void splitInSplitter(QSplitter *splitter, DocumentView *view,
                         DocumentView *newView,
                         Qt::Orientation orientation) noexcept;

    /**
     * Check if a widget tree contains the specified view.
     *
     * Recursively searches through splitter hierarchy.
     */
    bool containsView(QWidget *widget, DocumentView *view) const noexcept;

    /**
     * Recursively collect all DocumentView instances from a widget tree.
     */
    void collectViews(QWidget *widget,
                      QList<DocumentView *> &views) const noexcept;

    inline static void equalizeStretch(QSplitter *splitter) noexcept
    {
        if (!splitter || splitter->count() == 0)
            return;

        // 1. Get the current total size of the splitter
        int totalSize = 0;
        for (int size : splitter->sizes())
        {
            totalSize += size;
        }

        // 2. If total size is 0 (not yet rendered), use a fallback
        // to ensure they aren't initialized to 0px
        if (totalSize <= 0)
        {
            totalSize = 1000;
        }

        // 3. Create a list where every widget gets an equal share
        int share = totalSize / splitter->count();
        QList<int> newSizes;
        for (int i = 0; i < splitter->count(); ++i)
        {
            newSizes << share;
        }

        // 4. Force the splitter to apply these sizes immediately
        splitter->setSizes(newSizes);

        // 5. Keep the stretch factors so they stay equal when the window is
        // resized
        for (int i = 0; i < splitter->count(); ++i)
        {
            splitter->setStretchFactor(i, 1);
        }
    }

    /**
     * Create a new DocumentView based on a template view.
     *
     * Copies the document, settings, and navigation state.
     *
     * @param templateView View to use as template
     * @return Newly created view
     */

    DocumentView *createViewFromTemplate(DocumentView *templateView) noexcept;

    QVBoxLayout *m_layout{nullptr};
    DocumentView *m_current_view{nullptr};
    Id m_id{0};
};
