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

// TODO: Fix DWIM version
bool
Lektra::OpenFileDWIM(const QString &filename,
                     const CallbackFn &callback) noexcept
{
    if (m_tab_widget->count() == 0)
        return OpenFileInNewTab(filename, callback);

    DocumentContainer *container
        = m_tab_widget->container(m_tab_widget->currentIndex());
    if (!container)
        return OpenFileInNewTab(filename, callback);

    // No active view or empty — reuse current pane
    if (!m_doc || m_doc->filePath().isEmpty())
        return OpenFileInContainer(container, filename, callback, m_doc);

    if (container->getViewCount() > 1)
        return OpenFileInContainer(container, filename, callback, m_doc);

    // Single view with a file — open in new tab
    return OpenFileInNewTab(filename, callback);
}

bool
Lektra::OpenFileInContainer(DocumentContainer *container,
                            const QString &filename, const CallbackFn &callback,
                            DocumentView *targetView) noexcept
{
    if (!container)
        return false;

    if (filename.isEmpty())
    {
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter(supportedFormats());
        if (dialog.exec())
        {
            const QStringList selected = dialog.selectedFiles();
            if (!selected.isEmpty())
                return OpenFileInContainer(container, selected.first(),
                                           callback, targetView);
        }
        return false;
    }

    // No existence pre-check here — openAsync() below fails cleanly via
    // Model's own FileType::NONE fast-path and the view shows the failure
    // itself (see DocumentView::handleOpenFileFailed()), so a nonexistent
    // path gets the same in-tab treatment as any other file that fails to
    // open, instead of a dialog-only dead end.

    DocumentView *view = targetView ? targetView : container->view();
    if (!view)
        return false;

    // Close after view is resolved, on the correct view
    view->CloseFile();

    view->setDPR(m_dpr);

    const int tabIndex = m_tab_widget->currentIndex();

    if (callback)
    {
        connect(view, &DocumentView::openFileFinished, this,
                [this, callback](DocumentView *, Model::FileType)
        {
            // Deferred so the saved-page jump (queued by the view when its
            // file finishes opening) runs before the callback.
            QTimer::singleShot(0, this, [this, callback]() { callback(this); });
        }, Qt::SingleShotConnection);
    }
    else
    {
        connect(view, &DocumentView::openFileFinished, this,
                [this, tabIndex](DocumentView *doc, Model::FileType)
        {
            if (validTabIndex(tabIndex))
                m_tab_widget->tabBar()->setTabText(
                    tabIndex, m_config.tabs.full_path ? doc->filePath()
                                                      : doc->fileName());
            updateStatusbar();
            updateUiEnabledState();
            gotoPage(0);
        }, Qt::SingleShotConnection);
    }

    if (m_config.behavior.remember_last_visited)
    {
        const int savedPage = m_recent_files_store.pageNumber(filename);
        if (savedPage > 0)
            view->setPendingPage(savedPage - 1);
    }
    view->openAsync(filename);
    setCurrentDocumentView(view);
    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());

    return true;
}

void
Lektra::insertLazyTab(const QString &file) noexcept
{
    auto *placeholder = new QWidget(this);
    placeholder->setProperty("tabRole", "lazy");
    placeholder->setProperty("filePath", file);
    // No callback for plain multi-file open, but store an empty
    // one so materialization code is uniform
    placeholder->setProperty("callback", QVariant::fromValue(CallbackFn{}));

    const QString title
        = m_config.tabs.full_path ? file : QFileInfo(file).fileName();
    insertNewTab(placeholder, title);
}

void
Lektra::OpenFiles(const QStringList &files) noexcept
{
    bool isFirst = true;
    for (const QString &file : files)
    {
        if (!m_config.tabs.lazy_load || isFirst)
        {
            OpenFileInNewTab(file);
        }
        else
        {
            insertLazyTab(file);
        }
        isFirst = false;
    }
}

void
Lektra::OpenFilesInVSplit(const QStringList &files) noexcept
{
#ifndef NDEBUG
    qDebug() << "Lektra::OpenFilesInVSplit(): Opening files in vertical split:"
             << files.size();
#endif
    QStringList qfiles;

    if (files.isEmpty())
    {
        qfiles = QFileDialog::getOpenFileNames(this, tr("Open Files"),
                                               QString(), supportedFormats());
    }
    else
    {
        qfiles = files;
    }

    if (qfiles.isEmpty())
        return;

    // Split the current tab with the first file (falls back to a new tab
    // when no tab is open). Reading qfiles[0] into a local before moving
    // qfiles into the lambda: the order of evaluation of call arguments is
    // unspecified, so reading qfiles[0] in the same expression as
    // `qfiles = std::move(qfiles)` can index a moved-from list.
    const QString first = qfiles[0];
    OpenFileVSplit(first, [this, qfiles = std::move(qfiles)](void *)
    {
        for (int i = 1; i < qfiles.size(); ++i)
            OpenFileVSplit(qfiles[i]);
    });
}

void
Lektra::OpenFilesInHSplit(const QStringList &files) noexcept
{
#ifndef NDEBUG
    qDebug()
        << "Lektra::OpenFilesInHSplit(): Opening files in horizontal split:"
        << files.size();
#endif
    QStringList qfiles;

    if (files.isEmpty())
    {
        qfiles = QFileDialog::getOpenFileNames(this, tr("Open Files"),
                                               QString(), supportedFormats());
    }
    else
    {
        qfiles = files;
    }

    if (qfiles.isEmpty())
        return;

    // See OpenFilesInVSplit for the split-vs-new-tab rationale and why
    // qfiles[0] must be read before the move.
    const QString first = qfiles[0];
    OpenFileHSplit(first, [this, qfiles = std::move(qfiles)](void *)
    {
        for (int i = 1; i < qfiles.size(); ++i)
            OpenFileHSplit(qfiles[i]);
    });
}

void
Lektra::OpenFilesInNewTab(const QStringList &files,
                          const std::vector<CallbackFn> &callbacks) noexcept
{
#ifndef NDEBUG
    qDebug() << "Lektra::OpenFilesInNewTab(): Opening files in new tabs:"
             << files.size();
#endif

    if (static_cast<size_t>(files.size()) != callbacks.size())
    {
        qWarning() << "Number of files and callbacks do not match in"
                      "OpenFilesInNewTab."
                      "Aborting to prevent mismatched callbacks. Files count:"
                   << files.size();
        return;
    }
    QStringList qfiles;

    if (files.empty())
    {
        qfiles = QFileDialog::getOpenFileNames(this, tr("Open Files"),
                                               QString(), supportedFormats());
    }
    else
    {
        qfiles = files;
    }

    bool isFirst = true;
    for (int i = 0; i < qfiles.size(); ++i)
    {
        const QString &file = qfiles.at(i);
        const CallbackFn callback
            = i < callbacks.size() ? callbacks.at(i) : CallbackFn{};

        if (!m_config.tabs.lazy_load || isFirst)
        {
            OpenFileInNewTab(file, callback);
        }
        else
        {
            insertLazyTab(file);
        }
        isFirst = false;
    }
}

DocumentView *
Lektra::OpenFileInNewTab(const QString &filename, const CallbackFn &callback,
                         bool noHistory) noexcept
{
    if (filename.isEmpty())
    {
        // Show file picker
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter(supportedFormats());

        if (dialog.exec())
        {
            QStringList selected = dialog.selectedFiles();
            if (!selected.isEmpty())
                return OpenFileInNewTab(selected.first(), callback, noHistory);
        }
        return nullptr;
    }

    // No existence pre-check here — the tab is created below regardless,
    // and openAsync() fails cleanly (Model's FileType::NONE fast-path) with
    // the failure shown directly in that tab (red title + centered message
    // — see DocumentView::handleOpenFileFailed()) instead of a dialog-only
    // dead end that leaves no trace of the attempt.

    // Create a new DocumentView
    DocumentView *view = new DocumentView(m_config, m_dpr, this);
    view->setNoHistory(noHistory);

    connect(view, &DocumentView::openFileInNewTabRequested, this,
            [this](const QString &filePath, const CallbackFn &cb)
    { OpenFileInNewTab(filePath, cb); });

    // Create a DocumentContainer with this view
    DocumentContainer *container = new DocumentContainer(view, this);

    // Connect container signals
    connect(container, &DocumentContainer::viewCreated, this,
            [this, container](DocumentView *newView)
    {
        // Initialize the new view with connections
        initTabConnections(newView);

        // the tab shows how many splits it has
        syncTabSplits(container);

        // Update m_doc if this is in the current tab
        int currentTabIndex = m_tab_widget->currentIndex();
        DocumentContainer *currentContainer
            = m_tab_widget->container(currentTabIndex);
        if (currentContainer && currentContainer->view() == newView)
            setCurrentDocumentView(newView);
    });

    // Save page number when a split view is closed
    connect(container, &DocumentContainer::viewClosed, this,
            [this, container](DocumentView *closedView)
    {
        if (m_config.behavior.remember_last_visited && closedView
            && !closedView->filePath().isEmpty() && !closedView->is_portal()
            && !closedView->noHistory())
        {
            const int page = closedView->pageNo() + 1;
            insertFileToDB(closedView->filePath(), page > 0 ? page : 1);
        }

        // the tab shows how many splits it has (the view is already out of the
        // container when this is emitted)
        syncTabSplits(container);
    });

    connect(container, &DocumentContainer::currentViewChanged, container,
            [this](DocumentView *newView)
    {
        setCurrentDocumentView(newView);

        const int index = m_tab_widget->currentIndex();
        m_tab_widget->setTabTitle(index, m_config.tabs.full_path
                                             ? newView->filePath()
                                             : newView->fileName());

#ifdef WITH_LUA
        dispatchLuaEvent(DispatchType::OnViewChanged, newView);
#endif
    });

    // Initialize connections for the initial view
    initTabConnections(view);

    // Open the file asynchronously
    if (m_config.behavior.remember_last_visited)
    {
        const int savedPage = m_recent_files_store.pageNumber(filename);
        if (savedPage > 0)
            view->setPendingPage(savedPage - 1);
    }
    view->openAsync(filename);

    // Add the container as a tab. Block signals so QTabBar's insertTab does
    // not fire currentChanged reentrantly — handleCurrentTabChanged would
    // otherwise run on a half-initialised view (model still loading async,
    // m_doc still pointing at the previous view) and crash inside
    // updateStatusbar / m_doc->fileNameChanged.
    QString tabTitle = QFileInfo(filename).fileName();
    m_tab_widget->blockSignals(true);
    int tabIndex = insertNewTab(container, tabTitle);
    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());
    m_tab_widget->setCurrentIndex(tabIndex);
    m_tab_widget->blockSignals(false);

    // Wire up m_doc now that the tab is in place.
    setCurrentDocumentView(view);

    if (callback)
    {
        connect(view, &DocumentView::openFileFinished, this,
                [this, callback](DocumentView *, Model::FileType)
        {
            // Deferred so the saved-page jump (queued by the view when its
            // file finishes opening) runs before the callback.
            QTimer::singleShot(0, this, [this, callback]() { callback(this); });
        }, Qt::SingleShotConnection);
    }

    return view;
}

DocumentView *
Lektra::openFileSplitHelper(const QString &filename, const CallbackFn &callback,
                            Qt::Orientation orientation)
{
    if (filename.isEmpty())
    {
        // Show file picker
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter(supportedFormats());

        if (dialog.exec())
        {
            const QStringList selected = dialog.selectedFiles();
            if (!selected.isEmpty())
                return openFileSplitHelper(selected.first(), callback,
                                           orientation);
        }
        return nullptr;
    }

    const int tabIndex = m_tab_widget->currentIndex();

    if (!validTabIndex(tabIndex))
    {
        // No tabs open, open in new tab instead
        return OpenFileInNewTab(filename, callback);
    }

    DocumentContainer *container = m_tab_widget->container(tabIndex);

    if (!container)
        throw std::runtime_error("No container found for current tab");

    DocumentView *currentView = container->view();

    if (!currentView)
        return nullptr;

    DocumentView *newView
        = container->split(currentView, orientation, filename);

    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());

    // Restore saved page number after file loads (if remember_last_visited
    // is enabled)
    if (m_config.behavior.remember_last_visited)
    {
        const int savedPage = m_recent_files_store.pageNumber(filename);
        if (savedPage > 0)
        {
            connect(newView, &DocumentView::openFileFinished, this,
                    [newView, savedPage](DocumentView *, Model::FileType)
            { newView->GotoPage(savedPage - 1); }, Qt::SingleShotConnection);
        }
    }

    if (callback)
    {
        connect(newView, &DocumentView::openFileFinished,
                this, [this, callback](DocumentView *, Model::FileType) {
            QTimer::singleShot(0, this, [this, callback]() { callback(this); });
        }, Qt::SingleShotConnection);
    }

    return newView;
}

DocumentView *
Lektra::OpenFileVSplit(const QString &filename, const CallbackFn &callback)
{
    return openFileSplitHelper(filename, callback, Qt::Vertical);
}

DocumentView *
Lektra::OpenFileHSplit(const QString &filename, const CallbackFn &callback)
{
    return openFileSplitHelper(filename, callback, Qt::Horizontal);
}

void
Lektra::OpenFilesInNewWindow(const QStringList &filenames) noexcept
{
    if (filenames.empty())
        return;

    for (const QString &file : filenames)
    {
        OpenFileInNewWindow(file);
    }
}

bool
Lektra::OpenFileInNewWindow(const QString &filePath,
                            const CallbackFn &callback) noexcept
{
    if (filePath.isEmpty())
    {
        QStringList files;
        files = QFileDialog::getOpenFileNames(this, tr("Open File"), "",
                                              tr("PDF Files") + " " + "(*.pdf)"
                                                  + ";;" + tr("All Files") + " "
                                                  + "(*)");
        if (files.empty())
            return false;
        else
        {
            return OpenFileInNewWindow(files.first(), callback);
        }
    }

    QString fp = filePath;

    // expand ~
    if (fp == "~")
        fp = QDir::homePath();
    else if (fp.startsWith("~/"))
        fp = QDir(QDir::homePath()).filePath(fp.mid(2));

    // make absolute + clean
    fp = QDir::cleanPath(QFileInfo(fp).absoluteFilePath());

    // make absolute
    if (QDir::isRelativePath(fp))
        fp = QDir::current().absoluteFilePath(fp);

    if (!QFile::exists(fp))
    {
        QMessageBox::warning(this, tr("Open File"),
                             tr("Unable to find %1").arg(fp));
        return false;
    }

    QStringList args;
    args << fp;
    bool started = QProcess::startDetached(
        QCoreApplication::applicationFilePath(), args);
    if (!started)
        m_message_bar->showMessage(tr("Failed to open file in new window"));
    return started;
}

bool
Lektra::startNewWindow(const QString &file, int page) noexcept
{
    if (file.isEmpty())
        return false;

    // --new-window: without it, a window started while single-instance mode
    // is on would hand the file to this one instead.
    QStringList args{QStringLiteral("--new-window")};
    if (page > 0)
        args << QStringLiteral("--page") << QString::number(page);
    args << QFileInfo(file).absoluteFilePath();

    const bool started = QProcess::startDetached(
        QCoreApplication::applicationFilePath(), args);
    if (!started)
        m_message_bar->showMessage(tr("Failed to open a new window"));
    return started;
}

// Opens the properties widget with properties for the
// current file
void
Lektra::FileProperties() noexcept
{
    if (!m_doc)
        return;

    m_doc->FileProperties();
}

// Saves the current file
void
Lektra::SaveFile(const QString &filename) noexcept
{
    Q_UNUSED(filename);

    if (!m_doc)
        return;

    m_doc->SaveFile();
}

// Saves the current file as a new file
void
Lektra::SaveAsFile(const QString &filename) noexcept
{
    Q_UNUSED(filename);
    if (!m_doc)
        return;

    m_doc->SaveAsFile();
}

void
Lektra::dragEnterEvent(QDragEnterEvent *e) noexcept
{
    const QMimeData *mime = e->mimeData();

    if (mime->hasFormat(TabBar::MIME_TYPE) || mime->hasUrls())
        e->acceptProposedAction();
    else
        e->ignore();
}

void
Lektra::dropEvent(QDropEvent *e) noexcept
{
    const QMimeData *mime = e->mimeData();

    if (mime->hasFormat(TabBar::MIME_TYPE))
    {
        // Check if it's from our own TabBar (same window reordering)
        if (e->source() == m_tab_widget->tabBar())
        {
            e->ignore();
            return;
        }

        // It's from another window - accept it
        TabBar::TabData tabData
            = TabBar::TabData::deserialize(mime->data(TabBar::MIME_TYPE));

        if (!tabData.filePath.isEmpty())
        {
            handleTabDropReceived(tabData);

            e->setDropAction(Qt::MoveAction);
            e->accept();
            return;
        }

        e->ignore();
        return;
    }

    if (mime->hasUrls())
    {
        const auto urls = mime->urls();
        const auto mods = e->modifiers();

        for (const QUrl &url : urls)
        {
            if (!url.isLocalFile())
                continue;

            if (mods & Qt::ShiftModifier)
                OpenFileInNewWindow(url.toLocalFile());
            else
                OpenFileInNewTab(url.toLocalFile());
        }

        e->acceptProposedAction();
        return;
    }

    e->ignore();
}

// Reload the document in place
void
Lektra::reloadDocument() noexcept
{
    if (!m_doc)
        return;

    m_doc->reloadFile();
}

// Open the containing folder of the current document
void
Lektra::OpenContainingFolder() noexcept
{
    if (m_doc)
    {
        QString filepath = m_doc->fileName();
        QDesktopServices::openUrl(QUrl(QFileInfo(filepath).absolutePath()));
    }
}

// Encrypt the current document
void
Lektra::EncryptDocument() noexcept
{
    if (m_doc)
    {
        m_doc->EncryptDocument();
    }
}

void
Lektra::DecryptDocument() noexcept
{
    if (m_doc)
        m_doc->DecryptDocument();
}

void
Lektra::showTutorialFile() noexcept
{
    const QString doc_path = AppPaths::appTutorialPath();
    if (!doc_path.isEmpty() && QFileInfo::exists(doc_path))
    {
        OpenFileInNewTab(doc_path);
        return;
    }

#if defined(__linux__) || defined(__APPLE__) && defined(__MACH__)
    QMessageBox::warning(this, tr("Show Tutorial File"),
                         tr("Tutorial file could not be found."));
#elif defined(_WIN32)
    QMessageBox::warning(this, "Show Tutorial File",
                         tr("Not yet implemented for Windows"));
#endif
}

void
Lektra::CloseFile(const QString &filename) noexcept
{
    Q_UNUSED(filename);

    if (!m_doc)
        return;

    int indexToClose = m_tab_widget->currentIndex();
    Tab_close(indexToClose);
}

// Show a picker with the list of recent files from the recent files store,
// and allow the user to open a file from there.
void
Lektra::Show_recent_files_picker() noexcept
{
    const auto &entries = m_recent_files_store.entries();

    if (entries.empty())
    {
        QMessageBox::information(this, tr("Recent Files"),
                                 tr("No recent files found."));
        return;
    }

    const QStringList recentFiles = m_recent_files_store.files();

    if (!m_recent_file_picker)
    {
        m_recent_file_picker = new RecentFilesPicker(m_config.picker, this);
        m_recent_file_picker->setKeybindings(m_picker_keybinds);

        connect(m_recent_file_picker, &RecentFilesPicker::fileRequested, this,
                [this](const QString &file)
        { OpenFileInNewTab(file, [this](void *) { m_doc->setFocus(); }); });
    }

    // Always update the recent files list before launching
    m_recent_file_picker->setRecentFiles(recentFiles);
    m_recent_file_picker->launch();
}

void
Lektra::Show_file_picker() noexcept
{
    if (!m_file_picker)
    {
        m_file_picker = new FilePicker(m_config.picker, this);
        m_file_picker->setKeybindings(m_picker_keybinds);
        connect(m_file_picker, &FilePicker::fileRequested, this,
                [this](const QString &file)
        { OpenFileInNewTab(file, [this](void *) { m_doc->setFocus(); }); });
    }
    m_file_picker->launch();
}

void
Lektra::Reopen_last_closed_file() noexcept
{
    const auto &entries = m_recent_files_store.entries();
    if (entries.empty())
        return;

    // Skip the currently open file — go to the one before it
    const RecentFileEntry *target = nullptr;
    for (const auto &entry : entries)
    {
        if (m_doc && entry.file_path == m_doc->filePath())
            continue;
        target = &entry;
        break;
    }

    if (!target)
        return;

    if (!QFile::exists(target->file_path))
    {
        qWarning() << tr("Reopen_last_file: file no longer exists:")
                   << target->file_path;
        return;
    }

    const int savedPage  = target->page_number;
    const QString &fpath = target->file_path;

    OpenFileInNewTab(fpath, [this, savedPage](void *) { gotoPage(savedPage); });
}

DocumentView *
Lektra::OpenFile(const QString &filename, int on_doc_id,
                 const CallbackFn &callback) noexcept
{
    if (filename.isEmpty())
        return nullptr;

    // Search for doc_id if != -1, if found, close and open file in the same
    // view
    if (on_doc_id != -1)
    {
        DocumentView *view = get_view_by_id(on_doc_id);
        if (view)
        {
            view->openAsync(filename);
            if (callback)
            {
                connect(view, &DocumentView::openFileFinished, this,
                        [this, callback](DocumentView *, Model::FileType)
                { callback(this); }, Qt::SingleShotConnection);
            }
            return view;
        }
    }

    // Otherwise, open in a new tab
    return OpenFileInNewTab(filename, callback);
}

void
Lektra::CloseFile(DocumentView::Id doc_id) noexcept
{
    if (doc_id == -1)
    {
        m_doc->CloseFile();
    }
    else
    {
        DocumentView *view = get_view_by_id(doc_id);
        if (view)
            view->CloseFile();
    }
}
