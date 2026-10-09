#include "AboutDialog.hpp"
#include "AppPaths.hpp"
#include "DispatchType.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
#include "DonateDialog.hpp"
#include "EditLastPagesWidget.hpp"
#include "GraphicsView.hpp"
#include "Lektra.hpp"
#include "PageLocation.hpp"
#include "SaveSessionDialog.hpp"
#include "SearchBar.hpp"
#include "StartupWidget.hpp"
#include "TabBar.hpp"
#include "utils.hpp"

#include <QColorDialog>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QObject>
#include <QProcess>
#include <QSplitter>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWindow>
#include <variant>

void
Lektra::handleTabCloseRequested(int index) noexcept
{
    QWidget *widget = m_tab_widget->widget(index);
    if (!widget)
        return;

    const QString tabRole = widget->property("tabRole").toString();
    if (tabRole == "doc")
    {
        DocumentView *doc = qobject_cast<DocumentView *>(widget);

        if (doc)
        {
            // Set the outline to nullptr if the closed tab was the
            // current one
            if (m_doc == doc)
                m_outline_picker->clearOutline();
            doc->CloseFile();
        }
    }

    else if (tabRole == "lazy")
    {
        const QString filePath = widget->property("filePath").toString();
    }

    else if (tabRole == "startup")
    {
        if (m_startup_widget)
        {
            m_startup_widget->deleteLater();
            m_startup_widget = nullptr;
        }
    }

    // Save page numbers for all views in this tab before closing
    if (m_config.behavior.remember_last_visited)
    {
        DocumentContainer *container = m_tab_widget->container(index);
#ifndef NDEBUG
        qDebug() << "tabCloseRequested: remember_last_visited enabled, "
                    "container:"
                 << container;
#endif
        if (container)
        {
            const auto views = container->getAllViews();
#ifndef NDEBUG
            qDebug() << "tabCloseRequested: found" << views.size() << "views";
#endif
            for (DocumentView *view : views)
            {
#ifndef NDEBUG
                qDebug() << "tabCloseRequested: view:" << view
                         << "filePath:" << (view ? view->filePath() : "null")
                         << "is_portal:" << (view ? view->is_portal() : false);
#endif
                if (view && !view->filePath().isEmpty() && !view->is_portal()
                    && !view->noHistory())
                {
                    const int page = view->pageNo() + 1;
                    insertFileToDB(view->filePath(), page > 0 ? page : 1);
                }
            }
        }
    }

    m_tab_widget->removeTab(index);
    if (m_tab_widget->count() == 0)
    {
        setCurrentDocumentView(nullptr);
        if (m_config.behavior.close_on_last_tab)
        {
            // close() goes through closeEvent, so confirm_on_quit still
            // applies. dispatchLuaEvent below still fires — the window is
            // scheduled for close, not destroyed synchronously.
            close();
        }
    }

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnTabRemoved, &index);
#endif
}

// Handle when the file name is changed
void
Lektra::handleFileNameChanged(const QString &name) noexcept
{
    m_statusbar->setFilePath(name);
    this->setWindowTitle(name);
}

// Handle when the current tab is changed
void
Lektra::handleCurrentTabChanged(int index) noexcept
{
    QWidget *w = m_tab_widget->widget(index);

#ifdef WITH_LUA
    if (w)
        dispatchLuaEvent(DispatchType::OnTabChanged, &index);
#endif

    // Lazy load materialization: if the tab has the "lazy" role, it means
    // it's a placeholder for a file that hasn't been loaded yet. We need to
    // load the file, create a DocumentView for it, and replace the
    // placeholder widget with the new view.
    if (w && w->property("tabRole").toString() == "lazy")
    {
        const QString filePath = w->property("filePath").toString();
        const CallbackFn lazy  = w->property("callback").value<CallbackFn>();

        // Block signals to prevent recursion/cascading loads
        m_tab_widget->blockSignals(true);

        m_tab_widget->removeTab(index);
        w->deleteLater();

        // The real tab goes exactly where the placeholder was. (Adding it at
        // the end and moving it back used the wrong slot with
        // tabs.open_position other than "end", and left the tab bar's
        // per-tab state (split badges, selection) behind.)
        m_tab_insert_index = index;
        DocumentView *_    = OpenFileInNewTab(filePath, lazy);
        m_tab_insert_index = -1;

        m_tab_widget->setCurrentIndex(index);

        m_tab_widget->blockSignals(false);
        return;
    }

    // Stop animation on all views in the outgoing tab
    if (auto *oldContainer = m_tab_widget->container(m_prev_tab_index))
        for (DocumentView *view : oldContainer->getAllViews())
            view->stopGifPlayback();

    if (!validTabIndex(index))
    {
        setCurrentDocumentView(nullptr);
        return;
    }

    DocumentContainer *container = m_tab_widget->container(
        index); // get the root container for the current tab
    if (!container)
    {
        setCurrentDocumentView(nullptr);
        return;
    }

    // Update m_doc to current view in the container
    setCurrentDocumentView(container->view());

    // Start animation on all views in the incoming tab
    for (DocumentView *view : container->getAllViews())
        if (view->model()->isAnimated())
            view->startGifPlayback();

    m_prev_tab_index = index;

    if (m_doc)
    {
        emit m_doc->fileNameChanged(m_doc->fileName());
        updateUiEnabledState();
        updatePageNavigationActions();
        updateHistoryNavigationActions();
        updateSelectionModeActions();
        updateStatusbar();
    }
}

void
Lektra::handleTabDataRequested(int index, TabBar::TabData *outData) noexcept
{
    if (!validTabIndex(index))
        return;

    // Get the DocumentContainer, not the widget directly
    DocumentContainer *container = m_tab_widget->container(index);
    if (!container)
        return;

    // Get the active view from the container
    DocumentView *doc = container->view();
    if (!doc)
        return;

    // Now populate the data
    outData->filePath    = doc->filePath();
    outData->currentPage = doc->pageNo() + 1;
    outData->zoom        = doc->zoom();
    outData->invertColor = doc->invertColor();
    outData->rotation    = doc->model()->rotation();
    outData->fitMode     = static_cast<int>(doc->fitMode());
}

void
Lektra::handleTabDropReceived(const TabBar::TabData &data) noexcept
{
    if (data.filePath.isEmpty())
        return;

    // Open the file and restore its state
    OpenFileInNewTab(data.filePath, [this, data](void *)
    {
        if (!m_doc)
            return;

        // TabData arrives from another Lektra process (possibly a different
        // version) and cannot be trusted — clamp page, snap rotation to a
        // valid 90° step, and skip the rotation loop entirely if a bogus
        // value would make it never terminate.
        Model *model        = m_doc->model();
        const int pageCount = model ? model->numPages() : 0;
        int page            = data.currentPage - 1;
        if (page < 0)
            page = 0;
        if (pageCount > 0 && page >= pageCount)
            page = pageCount - 1;
        m_doc->GotoPage(page);
        m_doc->setZoom(data.zoom);
        m_doc->setInvertColor(data.invertColor);

        // Restore rotation. Normalise `data.rotation` to a multiple of 90
        // in [0, 360); anything else means the payload was corrupt and we
        // leave rotation untouched rather than spinning forever.
        if (model)
        {
            const int raw     = data.rotation;
            const int wrapped = ((raw % 360) + 360) % 360;
            if (wrapped % 90 == 0)
            {
                int currentRotation = model->rotation();
                const int target    = wrapped;
                // At most 4 iterations — any more means our own state was
                // off a 90° boundary too; bail rather than loop forever.
                for (int i = 0; i < 4 && currentRotation != target; ++i)
                {
                    m_doc->RotateClock();
                    currentRotation = (currentRotation + 90) % 360;
                }
            }
        }

        // Restore fit mode
        m_doc->setFitMode(static_cast<DocumentView::FitMode>(data.fitMode));
        // updateStatusbar();
    });
}

void
Lektra::handleTabDetached(int index, const QPoint &globalPos) noexcept
{
    Q_UNUSED(globalPos);

    if (!validTabIndex(index))
        return;

    // Close the tab that was successfully moved to another window
    m_tab_widget->tabCloseRequested(index);
}

void
Lektra::handleTabDetachedToNewWindow(int index,
                                     const TabBar::TabData &data) noexcept
{
    if (!validTabIndex(index))
        return;

    if (data.filePath.isEmpty())
        return;

    // Spawn a new Lektra process with the file
    QStringList args;
    args << "-p" << QString::number(data.currentPage);
    args << data.filePath;

    bool started = QProcess::startDetached(
        QCoreApplication::applicationFilePath(), args);

    if (started)
    {
        // Close the tab in this window
        m_tab_widget->tabCloseRequested(index);
    }
    else
    {
        m_message_bar->showMessage(tr("Failed to open tab in new window"));
    }
}

void
Lektra::handleTabContextMenu(int index, const QPoint &globalPos) noexcept
{
    const QList<int> selected = m_tab_widget->tabBar()->selectedTabs();
    if (selected.size() > 1 && selected.contains(index))
    {
        showMultiTabMenu(selected, globalPos);
        return;
    }

    QMenu menu;
    if (DocumentContainer *container = m_tab_widget->container(index);
        container && container->getViewCount() > 1)
    {
        menu.addAction(tr("Move Splits to Separate Tabs"), this,
                       [this, index]() { splitTabsIntoTabs({index}); });
        menu.addAction(tr("Move Splits to Separate Windows"), this,
                       [this, index]() { splitTabsToWindows({index}); });
    }
    menu.addAction(tr("Move Tab to New Window"), this, [this, index]()
    {
        TabBar::TabData data;
        handleTabDataRequested(index, &data);
        if (!data.filePath.isEmpty())
            handleTabDetachedToNewWindow(index, data);
    });

    menu.addAction(tr("Close Tab"), this,
                   [this, index]() { m_tab_widget->tabCloseRequested(index); });

    menu.exec(globalPos);
}

void
Lektra::showMultiTabMenu(const QList<int> &indices,
                         const QPoint &globalPos) noexcept
{
    QMenu menu;
    menu.addAction(tr("Close %1 Tabs").arg(indices.size()), this,
                   [this, indices]() { closeTabs(indices); });

    QMenu *mergeMenu = menu.addMenu(tr("Merge Into Split"));
    mergeMenu->addAction(tr("Vertical Split (Side by Side)"), this,
                         [this, indices]()
    { mergeTabsAsSplits(indices, true); });
    mergeMenu->addAction(tr("Horizontal Split (Stacked)"), this,
                         [this, indices]()
    { mergeTabsAsSplits(indices, false); });

    bool anySplits = false;
    for (int index : indices)
    {
        DocumentContainer *container = m_tab_widget->container(index);
        if (container && container->getViewCount() > 1)
            anySplits = true;
    }
    if (anySplits)
    {
        menu.addAction(tr("Move Splits to Separate Tabs"), this,
                       [this, indices]() { splitTabsIntoTabs(indices); });
        menu.addAction(tr("Move Splits to Separate Windows"), this,
                       [this, indices]() { splitTabsToWindows(indices); });
    }

    menu.addAction(tr("Move to New Window"), this,
                   [this, indices]() { moveTabsToNewWindow(indices); });
    menu.addAction(tr("Save Selection as Session..."), this,
                   [this, indices]() { saveTabsAsSession(indices); });

    menu.exec(globalPos);
}

QStringList
Lektra::tabFilePaths(const QList<int> &indices) noexcept
{
    QStringList paths;
    for (int index : indices)
    {
        TabBar::TabData data;
        handleTabDataRequested(index, &data);
        if (!data.filePath.isEmpty())
            paths << data.filePath;
    }
    return paths;
}

// The tabs a multi-tab operation acts on: the selection, or else the current
// tab.
QList<int>
Lektra::targetTabs() const noexcept
{
    if (!m_tab_widget)
        return {};
    QList<int> tabs = m_tab_widget->tabBar()->selectedTabs();
    if (tabs.isEmpty() && m_tab_widget->currentIndex() >= 0)
        tabs << m_tab_widget->currentIndex();
    return tabs;
}

// Closes highest index first so the remaining indices stay valid.
void
Lektra::closeTabs(QList<int> indices) noexcept
{
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (int index : indices)
        m_tab_widget->tabCloseRequested(index);
    m_tab_widget->tabBar()->clearTabSelection();
}

void
Lektra::mergeTabsAsSplits(const QList<int> &indices, bool vertical) noexcept
{
    if (indices.size() < 2)
    {
        m_message_bar->showMessage(tr("Select at least two tabs to merge"));
        return;
    }

    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());

    m_tab_widget->tabBar()->clearTabSelection();

    // The lowest-index tab stays and receives the others as splits. Its
    // index is unaffected by closing the higher ones, so close those first.
    const QStringList others = tabFilePaths(sorted.mid(1));
    closeTabs(sorted.mid(1));

    m_tab_widget->setCurrentIndex(sorted.first());
    for (const QString &path : others)
    {
        if (vertical)
            OpenFileVSplit(path);
        else
            OpenFileHSplit(path);
    }
}

// For each tab with splits, keeps its first view in place and moves every other
// split into a tab of its own (at the same page). Returns how many were moved.
int
Lektra::splitTabsIntoTabs(const QList<int> &indices) noexcept
{
    m_tab_widget->tabBar()->clearTabSelection();
    int moved = 0;
    for (int index : indices)
    {
        DocumentContainer *container = m_tab_widget->container(index);
        if (!container || container->getViewCount() < 2)
            continue;

        const QList<DocumentView *> views = container->getAllViews();
        for (int i = 1; i < views.size(); ++i)
            if (views.at(i)->detachToTab())
                ++moved;
    }
    return moved;
}

// The same, but every other split gets a window of its own.
int
Lektra::splitTabsToWindows(const QList<int> &indices) noexcept
{
    m_tab_widget->tabBar()->clearTabSelection();
    int moved = 0;
    for (int index : indices)
    {
        DocumentContainer *container = m_tab_widget->container(index);
        if (!container || container->getViewCount() < 2)
            continue;

        const QList<DocumentView *> views = container->getAllViews();
        for (int i = 1; i < views.size(); ++i)
            if (views.at(i)->detachToWindow())
                ++moved;
    }
    return moved;
}

void
Lektra::moveTabsToNewWindow(const QList<int> &indices) noexcept
{
    const QStringList paths = tabFilePaths(indices);
    if (paths.isEmpty())
        return;

    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 paths))
    {
        m_message_bar->showMessage(tr("Failed to open tabs in new window"));
        return;
    }
    closeTabs(indices);
}

void
Lektra::renameTab(int index, const QString &title) noexcept
{
    if (!m_tab_widget || !validTabIndex(index))
        return;

    TabBar *bar = m_tab_widget->tabBar();
    if (!title.trimmed().isEmpty())
    {
        bar->setCustomTitle(index, title.trimmed());
        return;
    }

    // Empty title: go back to the file name.
    bar->clearCustomTitle(index);
    if (DocumentContainer *container = m_tab_widget->container(index))
        if (DocumentView *view = container->view())
            bar->setTabText(index, m_config.tabs.full_path ? view->filePath()
                                                           : view->fileName());
}

void
Lektra::syncTabSplits(DocumentContainer *container) noexcept
{
    if (!m_tab_widget || !container)
        return;
    const int index = m_tab_widget->indexOf(container);
    if (index < 0)
        return;
    m_tab_widget->tabBar()->set_split_count(index, container->getViewCount());
}

// Opens the file of tab with index `index`
// in file manager program
void
Lektra::openInExplorerForIndex(int index) noexcept
{
    // Tabs hold a DocumentContainer, not a DocumentView, so a direct
    // qobject_cast of widget(index) is always null and this function used
    // to silently do nothing. Go via rootContainer to reach the view.
    if (!validTabIndex(index))
        return;
    DocumentContainer *container = m_tab_widget->container(index);
    if (!container)
        return;
    DocumentView *doc = container->view();
    if (!doc)
        return;
    const QString filePath = doc->filePath();
    if (filePath.isEmpty() || !QFile::exists(filePath))
        return;
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
}

// Initialize connections on each tab addition
void
Lektra::initTabConnections(DocumentView *docwidget) noexcept
{
    connect(docwidget, &DocumentView::statusbarNameChanged, m_statusbar,
            &Statusbar::setFilePath);

    connect(docwidget, &DocumentView::openFileFinished, this,
            [this](DocumentView *doc, Model::FileType /* ft */)
    {
        // Only update the statusbar if this view is the currently active
        // one. If it's a background split, don't clobber the active view's
        // info.
        if (m_doc == doc)
        {
            updateStatusbar();
            // Also drive the tab title, which suffers the same timing
            // problem
            int index = m_tab_widget->currentIndex();
            if (validTabIndex(index))
            {
                m_tab_widget->tabBar()->setTabText(
                    index, m_config.tabs.full_path ? doc->filePath()
                                                   : doc->fileName());
                // Clear any red left over from a previous failed open in
                // this same tab (e.g. retried with a different file).
                m_tab_widget->tabBar()->setTabFailed(index, false);
            }
            updateUiEnabledState();
        }
    });

    connect(docwidget, &DocumentView::modifiedChanged, this,
            [this](bool) { updateUiEnabledState(); });

    connect(docwidget, &DocumentView::historyChanged, this, [this, docwidget]()
    {
        if (m_doc == docwidget)
            updateHistoryNavigationActions();
    });

    connect(docwidget, &DocumentView::openFileFailed, this,
            [this](DocumentView *doc)
    {
        const bool wasCurrentView    = (m_doc == doc);
        DocumentContainer *container = doc->container();
        if (!container)
            return;

        // Capture before CloseFile() — Model::close() clears the filepath,
        // and the tab title below still needs to show what failed to open.
        const QString failedTitle
            = m_config.tabs.full_path ? doc->filePath() : doc->fileName();

        // If this is the only view in the container, keep the tab open
        // (rather than closing it) so the failure is visible/identifiable
        // instead of silently vanishing — its title turns red and shows
        // the filename that failed to load. A failed view inside a split
        // still gets closed, since the split's other pane(s) may still
        // have a perfectly good document open.
        doc->CloseFile();

        if (container->getViewCount() <= 1)
        {
            const int tabIndex = m_tab_widget->indexOf(container);
            if (tabIndex != -1)
            {
                m_tab_widget->tabBar()->setTabText(tabIndex, failedTitle);
                m_tab_widget->tabBar()->setTabFailed(tabIndex, true);
            }
        }
        else
        {
            container->closeView(doc);
        }

        // Update UI state if this was the current view
        if (wasCurrentView)
        {
            updateStatusbar();
            updateUiEnabledState();
        }
    });

    connect(docwidget, &DocumentView::currentPageChanged, this,
            [this, docwidget](int pageno)
    {
        if (m_doc == docwidget)
            m_statusbar->setPageNo(pageno);
    });

    connect(docwidget, &DocumentView::zoomChanged, this,
            [this, docwidget](double zoom)
    {
        if (m_doc == docwidget)
            m_statusbar->setZoom(zoom);
    });

    connect(docwidget, &DocumentView::searchBarSpinnerShow, m_search_bar,
            &SearchBar::showSpinner);

    connect(docwidget, &DocumentView::requestFocus, this,
            [this](DocumentView *view)
    {
        if (m_doc == view)
            return;

#ifndef NDEBUG
        qDebug()
            << "DocumentView requested focus, setting current document view"
            << view;
#endif
        setCurrentDocumentView(view);
    });

    // Connect undo stack signals to update undo/redo menu actions
    QUndoStack *undoStack = docwidget->model()->undoStack();
    connect(undoStack, &QUndoStack::canUndoChanged, this,
            [this, docwidget](bool canUndo)
    {
        if (m_doc == docwidget)
            m_actionUndo->setEnabled(canUndo);
    });
    connect(undoStack, &QUndoStack::canRedoChanged, this,
            [this, docwidget](bool canRedo)
    {
        if (m_doc == docwidget)
            m_actionRedo->setEnabled(canRedo);
    });

    connect(m_statusbar, &Statusbar::modeChangeRequested, docwidget,
            &DocumentView::NextSelectionMode);

    connect(m_statusbar, &Statusbar::fitModeChangeRequested, docwidget,
            &DocumentView::NextFitMode);

    connect(docwidget, &DocumentView::fileNameChanged, this,
            &Lektra::handleFileNameChanged);

    connect(docwidget, &DocumentView::pageChanged, m_statusbar,
            &Statusbar::setPageNo);

    connect(docwidget, &DocumentView::searchCountChanged, m_search_bar,
            &SearchBar::setSearchCount);

    connect(docwidget, &DocumentView::searchClearRequested, m_search_bar,
            &SearchBar::clearSearch);

    // connect(docwidget, &DocumentView::searchModeChanged, m_statusbar,
    //         &SearchBar::setSearchMode);

    connect(docwidget, &DocumentView::searchIndexChanged, m_search_bar,
            &SearchBar::setSearchIndex);

    connect(docwidget, &DocumentView::totalPageCountChanged, m_statusbar,
            &Statusbar::setTotalPageCount);

    connect(docwidget, &DocumentView::highlightColorChanged, m_statusbar,
            &Statusbar::setHighlightColor);

    connect(docwidget, &DocumentView::selectionModeChanged, m_statusbar,
            &Statusbar::setMode);

    connect(docwidget, &DocumentView::narrowModeChanged, m_statusbar,
            &Statusbar::setNarrowMode);

    connect(docwidget, &DocumentView::narrowPageRangeChanged, m_statusbar,
            &Statusbar::setPageRangeInfo);

    connect(docwidget, &DocumentView::clipboardContentChanged, this,
            [&](const QString &text) { m_clipboard->setText(text); });

    connect(docwidget, &DocumentView::autoResizeActionUpdate, this,
            [&](bool state) { m_actionAutoresize->setChecked(state); });

    connect(docwidget, &DocumentView::insertToDBRequested, this,
            &Lektra::insertFileToDB);

    connect(docwidget, &DocumentView::ctrlLinkClickRequested, this,
            &Lektra::handleCtrlLinkClickRequested);

    connect(docwidget, &DocumentView::linkPreviewRequested, this,
            &Lektra::handleLinkPreviewRequested);

    connect(docwidget, &DocumentView::linkOpenInNewTabRequested, this,
            &Lektra::handleLinkOpenInNewTab);

    connect(docwidget, &DocumentView::linkOpenVSplitRequested, this,
            &Lektra::handleLinkOpenVSplit);

    connect(docwidget, &DocumentView::linkOpenHSplitRequested, this,
            &Lektra::handleLinkOpenHSplit);

    connect(docwidget, &DocumentView::externalLinkRequested, this,
            &Lektra::handleExternalLinkRequested);
}

// Go to the first tab
void
Lektra::Tab_first() noexcept
{
    if (m_tab_widget->count() != 0)
    {
        m_tab_widget->setCurrentIndex(0);
    }
}

// Go to the last tab
void
Lektra::Tab_last() noexcept
{
    int count = m_tab_widget->count();
    if (count != 0)
    {
        m_tab_widget->setCurrentIndex(count - 1);
    }
}

// Go to the next tab
void
Lektra::Tab_next() noexcept
{
    int count        = m_tab_widget->count();
    int currentIndex = m_tab_widget->currentIndex();
    if (count != 0 && currentIndex < count)
    {
        m_tab_widget->setCurrentIndex(currentIndex + 1);
    }
}

// Go to the previous tab
void
Lektra::Tab_prev() noexcept
{
    int count        = m_tab_widget->count();
    int currentIndex = m_tab_widget->currentIndex();
    if (count != 0 && currentIndex > 0)
    {
        m_tab_widget->setCurrentIndex(currentIndex - 1);
    }
}

// Go to the tab at nth position specified by `tabno` (1-based index)
void
Lektra::Tab_goto(int index) noexcept
{
    if (index == -1)
    {
        index = QInputDialog::getInt(this, tr("Go to Tab"),
                                     tr("Enter tab number: "), 1, 1,
                                     m_tab_widget->count());
    }

    if (index >= 1 && index <= m_tab_widget->count())
        m_tab_widget->setCurrentIndex(index - 1);
    else
        m_message_bar->showMessage(tr("Invalid Tab Number"));
}

// Close the current tab
void
Lektra::Tab_close(int tabno) noexcept
{
    int indexToClose = (tabno == -1) ? m_tab_widget->currentIndex() : tabno;

    if (!validTabIndex(indexToClose))
        return;

    // Get the container
    DocumentContainer *container = m_tab_widget->container(indexToClose);
    if (!container)
        return;

    // Get all views to update hash
    QList<DocumentView *> views = container->getAllViews();

    // Close the tab (this will delete the container and all views)
    m_tab_widget->removeTab(indexToClose);

    // Update m_doc
    if (m_tab_widget->count() > 0)
    {
        int currentIndex = m_tab_widget->currentIndex();
        DocumentContainer *currentContainer
            = m_tab_widget->container(currentIndex);
        if (currentContainer)
        {
            setCurrentDocumentView(currentContainer->view());
        }
    }
    else
    {
        setCurrentDocumentView(nullptr);
        if (m_config.behavior.close_on_last_tab)
        {
            close();
        }
        else
        {
            showStartupWidget();
        }
    }

    updateUiEnabledState();
}

void
Lektra::TabMoveRight() noexcept
{
    QTabBar *bar = m_tab_widget->tabBar();
    const int n  = bar->count();
    if (n <= 1)
        return;

    const int i = bar->currentIndex();
    if (i < 0 || i == n - 1)
        return;

    bar->moveTab(i, i + 1);
}

void
Lektra::TabMoveLeft() noexcept
{
    QTabBar *bar = m_tab_widget->tabBar();
    const int n  = bar->count();
    if (n <= 1)
        return;

    const int i = bar->currentIndex();
    if (i == 0)
        return;

    bar->moveTab(i, i - 1);
}

void
Lektra::TabsCloseLeft() noexcept
{
    const int currentIndex = m_tab_widget->currentIndex();
    if (currentIndex <= 0)
        return;

    for (int i = currentIndex - 1; i >= 0; --i)
        m_tab_widget->tabCloseRequested(i);
}

void
Lektra::TabsCloseRight() noexcept
{
    const int currentIndex = m_tab_widget->currentIndex();
    const int ntabs        = m_tab_widget->count();

    if (currentIndex < 0 || currentIndex >= ntabs - 1)
        return;

    for (int i = ntabs - 1; i > currentIndex; --i)
        m_tab_widget->tabCloseRequested(i);
}

void
Lektra::TabsCloseOthers() noexcept
{
    const int ntabs = m_tab_widget->count();

    if (ntabs == 0)
        return;

    const int currentIndex = m_tab_widget->currentIndex();

    if (currentIndex < 0)
        return;

    for (int i = ntabs - 1; i >= 0; --i)
    {
        if (i == currentIndex)
            continue;
        m_tab_widget->tabCloseRequested(i);
    }
}

DocumentView *
Lektra::splitHelper(DocumentView::Id id, Qt::Orientation orientation) noexcept
{
    int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return nullptr;

    // Get the container for this tab
    DocumentContainer *container = m_tab_widget->container(currentTabIndex);
    if (!container)
        return nullptr;

    DocumentView *targetView = nullptr;
    if (id == -1)
        targetView = container->view();
    else
        targetView = container->get_child_view_by_id(id);
    if (!targetView)
        return nullptr;

    // Perform vertical split (top/bottom)
    container->split(targetView, orientation);
    m_tab_widget->tabBar()->set_split_count(currentTabIndex,
                                            container->getViewCount());
    return targetView;
}

DocumentView *
Lektra::VSplit(DocumentView::Id id) noexcept
{
    return splitHelper(id, Qt::Vertical);
}

DocumentView *
Lektra::HSplit(DocumentView::Id id) noexcept
{
    return splitHelper(id, Qt::Horizontal);
}

// Closes all splits except the current one in the current tab. If there is
// only one split, does nothing.
void
Lektra::Close_other_splits() noexcept
{
    const int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->container(currentTabIndex);
    if (!container)
        return;

    DocumentView *currentView = container->view();
    if (!currentView)
        return;

    container->close_other_views(currentView);
    m_tab_widget->tabBar()->set_split_count(currentTabIndex,
                                            container->getViewCount());
}

void
Lektra::ToggleSplitMaximize() noexcept
{
    const int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->container(currentTabIndex);
    if (!container)
        return;

    container->toggleMaximizeSplit();
}

void
Lektra::Close_split() noexcept
{
    const int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->container(currentTabIndex);
    if (!container)
        return;

    // Don't close if it's the only view
    if (container->getViewCount() <= 1)
        return;

    DocumentView *currentView = container->view();
    if (currentView)
    {
        container->closeView(currentView);
    }
    else
    {
        // TODO: Handle split not being closed ?
    }

    m_tab_widget->tabBar()->set_split_count(currentTabIndex,
                                            container->getViewCount());
    m_tab_widget->tabBar()->setTabText(
        currentTabIndex,
        m_config.tabs.full_path ? m_doc->filePath() : m_doc->fileName());
}

int
Lektra::insertNewTab(QWidget *page, const QString &title) noexcept
{
    if (m_tab_insert_index >= 0)
    {
        const int at       = std::min(m_tab_insert_index, m_tab_widget->count());
        m_tab_insert_index = -1;
        return m_tab_widget->insertTab(at, page, title);
    }

    using OP = Config::Tabs::OpenPosition;
    switch (m_config.tabs.open_position)
    {
        case OP::Start:
            return m_tab_widget->insertTab(0, page, title);
        case OP::AfterCurrent:
        {
            // If there is no current tab (fresh window), fall through to
            // End so we don't insert at -1 or 0 unexpectedly.
            const int cur = m_tab_widget->currentIndex();
            if (cur < 0)
                return m_tab_widget->addTab(page, title);
            return m_tab_widget->insertTab(cur + 1, page, title);
        }
        case OP::End:
        default:
            return m_tab_widget->addTab(page, title);
    }
}

void
Lektra::centerMouseInDocumentView(DocumentView *view) noexcept
{
    if (!view)
        return;

    DocumentView *safeView = view;

    QTimer::singleShot(0, view, [safeView]()
    {
        if (!safeView)
            return;

        const QPoint center = safeView->mapToGlobal(safeView->rect().center());

        QCursor::setPos(center);
    });
}

void
Lektra::Focus_split_up() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Up);
}

void
Lektra::Focus_split_down() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Down);
}

void
Lektra::Focus_split_left() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Left);
}

void
Lektra::Focus_split_right() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Right);
}

void
Lektra::focusSplitHelper(DocumentContainer::Direction direction) noexcept
{
    const int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->container(currentTabIndex);
    if (!container)
        return;

    container->focusSplit(direction);

    if (m_config.split.mouse_follows_focus)
        if (auto *view = container->view())
            centerMouseInDocumentView(view);
}

void
Lektra::restoreSplitNode(DocumentContainer *container, DocumentView *targetView,
                         const QJsonObject &node,
                         const CallbackFn &callback) noexcept
{
    const QString type = node["type"].toString();

    if (type == "view")
    {
        const QString path = node["file_path"].toString();
        const int page     = node["current_page"].toInt();
        const double zoom  = node["zoom"].toDouble();
        const int fitMode  = node["fit_mode"].toInt();
        const bool invert  = node["invert_color"].toBool();

        auto applyState
            = [this, page, zoom, fitMode, invert, callback](DocumentView *doc)
        {
            doc->setFitMode(static_cast<DocumentView::FitMode>(fitMode));
            doc->setZoom(zoom);
            doc->GotoPage(page - 1);
            if (invert)
                doc->setInvertColor(true);
            if (callback)
                callback(this);
        };

        if (path.isEmpty())
        {
            if (callback)
                callback(this);
            return;
        }

        // If this view already has the right file loaded, just apply state
        if (targetView->filePath() == path)
        {
            applyState(targetView);
            return;
        }

        // No existence pre-check — openAsync() below fails cleanly and the
        // view shows the failure itself if the saved path is gone.
        targetView->openAsync(path);

        connect(targetView, &DocumentView::openFileFinished, this,
                [applyState](DocumentView *doc, Model::FileType)
        { applyState(doc); }, Qt::SingleShotConnection);

        return;
    }

    if (type == "splitter")
    {
        const QJsonArray children = node["children"].toArray();
        const Qt::Orientation orient
            = static_cast<Qt::Orientation>(node["orientation"].toInt());
        const QJsonArray sizesArray = node["sizes"].toArray();

        if (children.isEmpty())
        {
            if (callback)
                callback(this);
            return;
        }

        // Build the full splitter structure FIRST (synchronously),
        // then fill each pane asynchronously
        QList<DocumentView *> panes;
        panes.append(targetView);

        for (int i = 1; i < children.size(); ++i)
        {
            // splitEmpty creates the pane without opening any file
            // so there's no async conflict
            DocumentView *newPane = container->splitEmpty(targetView, orient);
            if (newPane)
                panes.append(newPane);
        }

        // Apply saved sizes immediately after structure is built
        QSplitter *splitter
            = qobject_cast<QSplitter *>(targetView->parentWidget());
        if (splitter)
        {
            QList<int> sizes;
            for (const QJsonValue &s : sizesArray)
                sizes << s.toInt();
            if (sizes.size() == splitter->count())
                splitter->setSizes(sizes);
        }

        // Now fill each pane asynchronously — they're all independent
        // so we don't need to chain them, just count completions
        auto remaining = std::make_shared<int>(panes.size());

        for (int i = 0; i < panes.size() && i < children.size(); ++i)
        {
            const QJsonObject child = children[i].toObject();
            DocumentView *pane      = panes[i];

            restoreSplitNode(container, pane, child,
                             [remaining, callback](void *l)
            {
                --(*remaining);
                if (*remaining == 0 && callback)
                    callback(l);
            });
        }
    }
}

// Searches for an open DocumentView with the given file path and returns it
// if found, otherwise returns nullptr
DocumentView *
Lektra::findOpenView(const QString &path) const noexcept
{
    for (int i = 0; i < m_tab_widget->count(); ++i)
    {
        DocumentContainer *container = m_tab_widget->container(i);
        if (!container)
            continue;
        for (DocumentView *view : container->getAllViews())
            if (view->filePath() == path)
                return view;
    }
    return nullptr;
}

void
Lektra::handleCtrlLinkClickRequested(DocumentView *view,
                                     const BrowseLinkItem *linkItem) noexcept
{
    // Only handle internal links in a split — external links open
    // in browser as usual
    if (!view || !linkItem)
        return;

    if (!linkItem->isInternal())
    {
        if (!linkItem->link().isEmpty())
            QDesktopServices::openUrl(QUrl(linkItem->URI()));
        return;
    }

    // Create the location target data (copy values, not pointers)
    PageLocation target{linkItem->gotoPageNo(), linkItem->location().x,
                        linkItem->location().y};

    if (std::isnan(target.x))
        target.x = 0;
    if (std::isnan(target.y))
        target.y = 0;

    // Check if this is already a portal
    if (view->is_portal())
        return;

    // Check if portal already exists
    if (auto portal = view->portal())
    {
        portal->GotoLocation(target);
        return;
    }

    DocumentView *newView = create_portal(view, view->filePath());

    // Fix for jump marker event loop not executing
    connect(newView, &DocumentView::openFileFinished, this,
            [newView, target](DocumentView *, Model::FileType)
    {
        QTimer::singleShot(0, newView, [newView, target]()
        { newView->GotoLocation(target); });
    }, Qt::SingleShotConnection);
}

void
Lektra::handleLinkPreviewRequested(DocumentView *view,
                                   const BrowseLinkItem *linkItem) noexcept
{
    if (!view || !linkItem)
        return;

    if (!linkItem->isInternal())
    {
        if (!linkItem->link().isEmpty())
            QDesktopServices::openUrl(QUrl(linkItem->URI()));
        return;
    }

    PageLocation target{linkItem->gotoPageNo(), linkItem->location().x,
                        linkItem->location().y};
    if (std::isnan(target.x))
        target.x = 0;
    if (std::isnan(target.y))
        target.y = 0;

    // Create overlay + preview once, reuse after
    if (!m_preview_overlay)
    {
        // Full-window overlay with semi-transparent background
        m_preview_overlay = new QWidget(this);
        m_preview_overlay->setAttribute(Qt::WA_StyledBackground, true);
        m_preview_overlay->setStyleSheet("background: rgba(0, 0, 0, 50);");

        // Inner container for the actual preview content
        auto *innerContainer = new QWidget(m_preview_overlay);
        innerContainer->setObjectName("linkPreviewInner");
        innerContainer->setAttribute(Qt::WA_StyledBackground, true);
        innerContainer->setStyleSheet(
            QString("background: rgba(0, 0, 0, 50); border-radius: %1px;")
                .arg(m_config.preview.border_radius));

        m_preview_view = new DocumentView(m_config, m_dpr, innerContainer);

        auto *innerLayout = new QVBoxLayout(innerContainer);
        innerLayout->setContentsMargins(2, 2, 2, 2);
        innerLayout->addWidget(m_preview_view);

        // Center the inner container within the overlay
        auto *overlayLayout = new QGridLayout(m_preview_overlay);
        overlayLayout->setContentsMargins(0, 0, 0, 0);
        overlayLayout->addWidget(innerContainer, 0, 0, Qt::AlignCenter);

        // Close when clicking on the overlay background (outside inner
        // container)
        m_preview_overlay->installEventFilter(this);
    }

    // Resize overlay to fill window, inner container sized proportionally
    m_preview_overlay->resize(size());
    m_preview_overlay->move(0, 0);

    // Resize inner container
    QWidget *innerContainer
        = m_preview_overlay->findChild<QWidget *>("linkPreviewInner");
    if (innerContainer)
    {
        const QSize innerSize(width() * m_config.preview.size_ratio[0],
                              height() * m_config.preview.size_ratio[1]);
        innerContainer->setFixedSize(innerSize);
    }

    m_preview_overlay->raise();
    m_preview_overlay->show();

    auto navigateTo = [this, target](float zoom)
    {
        m_preview_view->setZoom(zoom);
        m_preview_view->GotoLocation(target);
    };

    if (m_preview_view->filePath() != view->filePath())
    {
        // Capture the zoom by value at connect time — m_doc can change tab
        // (and therefore zoom) between open kick-off and openFileFinished.
        // Also give the inner singleShot `this` as its receiver so a Lektra
        // destroyed before the timer fires cancels the call instead of
        // dereferencing a dead pointer.
        const float zoom = m_doc ? m_doc->zoom() : 1.0f;
        connect(m_preview_view, &DocumentView::openFileFinished, this,
                [this, navigateTo, zoom](DocumentView *, Model::FileType)
        {
            QTimer::singleShot(0, this,
                               [navigateTo, zoom]() { navigateTo(zoom); });
        }, Qt::SingleShotConnection);
        m_preview_view->openAsync(view->filePath());
    }
    else
    {
        navigateTo(m_doc ? m_doc->zoom() : 1.0f);
    }
}

void
Lektra::handleLinkOpenInNewTab(DocumentView *view,
                               const BrowseLinkItem *linkItem) noexcept
{
    if (!view || !linkItem || !linkItem->isInternal())
        return;

    PageLocation target{linkItem->gotoPageNo(), linkItem->location().x,
                        linkItem->location().y};
    if (std::isnan(target.x))
        target.x = 0;
    if (std::isnan(target.y))
        target.y = 0;

    DocumentView *newView = OpenFileInNewTab(view->filePath());
    if (!newView)
        return;

    connect(newView, &DocumentView::openFileFinished, this,
            [newView, target](DocumentView *, Model::FileType)
    {
        QTimer::singleShot(0, newView, [newView, target]()
        { newView->GotoLocation(target); });
    }, Qt::SingleShotConnection);
}

void
Lektra::handleLinkOpenVSplit(DocumentView *view,
                             const BrowseLinkItem *linkItem) noexcept
{
    if (!view || !linkItem || !linkItem->isInternal())
        return;

    PageLocation target{linkItem->gotoPageNo(), linkItem->location().x,
                        linkItem->location().y};
    if (std::isnan(target.x))
        target.x = 0;
    if (std::isnan(target.y))
        target.y = 0;

    DocumentView *newView = OpenFileVSplit(view->filePath());
    if (!newView)
        return;

    connect(newView, &DocumentView::openFileFinished, this,
            [newView, target](DocumentView *, Model::FileType)
    {
        QTimer::singleShot(0, newView, [newView, target]()
        { newView->GotoLocation(target); });
    }, Qt::SingleShotConnection);
}

void
Lektra::handleLinkOpenHSplit(DocumentView *view,
                             const BrowseLinkItem *linkItem) noexcept
{
    if (!view || !linkItem || !linkItem->isInternal())
        return;

    PageLocation target{linkItem->gotoPageNo(), linkItem->location().x,
                        linkItem->location().y};
    if (std::isnan(target.x))
        target.x = 0;
    if (std::isnan(target.y))
        target.y = 0;

    DocumentView *newView = OpenFileHSplit(view->filePath());
    if (!newView)
        return;

    connect(newView, &DocumentView::openFileFinished, this,
            [newView, target](DocumentView *, Model::FileType)
    {
        QTimer::singleShot(0, newView, [newView, target]()
        { newView->GotoLocation(target); });
    }, Qt::SingleShotConnection);
}

void
Lektra::handleExternalLinkRequested(const QString &uri) noexcept
{
    const QUrl url(uri);
    if (url.isLocalFile())
    {
        const QString path = url.toLocalFile();
        if (QFileInfo::exists(path)
            && Model::getFileTypeForPath(path) != Model::FileType::NONE)
        {
            OpenFilesInNewTab({path});
            return;
        }
    }

    QDesktopServices::openUrl(url);
}

/*
 * NOTE: This is problematic to move into the DocumentView class (because
 * the splitting related stuff are here and in the future we have to
 * implement smart splitting, so it's better to leave this here)
 */
// Helper function for quickly creating portals
// DocumentView *
DocumentView *
Lektra::create_portal(DocumentView *sourceView,
                      const QString &filePath) noexcept
{
    if (sourceView->portal() || sourceView->is_portal())
        return nullptr;

    const QString target
        = filePath.isEmpty() ? sourceView->filePath() : filePath;

    bool useHSplit = false;
    if (m_config.portal.split.compare("horizontal", Qt::CaseInsensitive) == 0)
        useHSplit = true;
    else if (m_config.portal.split.compare("smart", Qt::CaseInsensitive) == 0)
        useHSplit = sourceView->height() > sourceView->width();

    DocumentView *newView
        = useHSplit ? OpenFileHSplit(target) : OpenFileVSplit(target);
    if (!newView)
        return nullptr;

    sourceView->setPortal(newView);
    m_statusbar->setPortalMode(true);

    auto pair = std::make_shared<PortalPair>(sourceView, newView);

    connect(sourceView, &QObject::destroyed, this, [this, pair]()
    {
        pair->source = nullptr;
        if (pair->portal)
        {
            if (m_config.portal.respect_parent)
            {
                DocumentContainer *container = pair->portal->container();
                if (container)
                    container->closeView(pair->portal);
            }
            else
            {
                pair->portal->graphicsView()->setPortal(false);
                pair->portal->clear_source();
            }
        }
    }, Qt::SingleShotConnection);

    connect(newView, &QObject::destroyed, this, [this, pair]()
    {
        pair->portal = nullptr;
        if (pair->source)
        {
            pair->source->clearPortal();
            m_statusbar->setPortalMode(false);
        }
    }, Qt::SingleShotConnection);

    return newView;
}

DocumentView *
Lektra::get_view_by_id(const DocumentView::Id id) const noexcept
{
    for (int i = 0; i < m_tab_widget->count(); ++i)
    {
        DocumentContainer *container = m_tab_widget->container(i);
        if (!container)
            continue;

        DocumentView *view = container->view();

        if (view->id() == id)
            return view;

        DocumentView *child_view = container->get_child_view_by_id(id);

        if (child_view)
            return child_view;
    }

    return nullptr;
}

// Focus the portal view in the current tab, if it exists. Else create one
void
Lektra::Create_or_focus_portal() noexcept
{
    if (!m_doc)
        return;

    int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    if (DocumentView *portal = m_doc->portal())
    {
        DocumentContainer *p_container = portal->container();
        if (p_container)
            p_container->focusView(portal);
    }
    else
    {
        create_portal(m_doc, m_doc->filePath());
    }
}
