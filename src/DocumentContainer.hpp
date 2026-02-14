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
    DocumentView *getCurrentView() const noexcept;

    inline DocumentView *view() const noexcept
    {
        return m_current_view;
    }

    void focusView(DocumentView *view) noexcept;

    int getViewCount() const noexcept;

    void syncViewSettings(DocumentView *source, DocumentView *target) noexcept;

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
    Id m_id{0};
};
