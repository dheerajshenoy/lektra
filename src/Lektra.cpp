#include "Lektra.hpp"

#include "AboutDialog.hpp"
#include "AppPaths.hpp"
#include "DispatchType.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
#include "DonateDialog.hpp"
#include "EditLastPagesWidget.hpp"
#include "GraphicsView.hpp"
#include "PageLocation.hpp"
#include "SaveSessionDialog.hpp"
#include "SearchBar.hpp"
#include "StartupWidget.hpp"
#include "TabBar.hpp"
#include "toml.hpp"
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

const char *HOME_DIR = getenv("HOME");

// Constructs the `Lektra` class
Lektra::Lektra() noexcept
{
    setAttribute(Qt::WA_NativeWindow,
                 true); // This is necessary for DPI updates
    setAcceptDrops(true);

    m_config_dir = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));

    if (m_config_file_path.isEmpty())
        m_config_file_path = m_config_dir.filePath("config.toml");
}

Lektra::Lektra(const QString &sessionName,
               const QJsonArray &sessionArray) noexcept
{
    m_config_dir = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));

    if (m_config_file_path.isEmpty())
        m_config_file_path = m_config_dir.filePath("config.toml");

    setAttribute(Qt::WA_NativeWindow); // This is necessary for DPI updates
    setAcceptDrops(true);
    construct();
    openSessionFromArray(sessionArray);
    setSessionName(sessionName);
    m_statusbar->setSessionName(sessionName);
}

Lektra::~Lektra() noexcept
{
    if (m_command_manager && m_config.command_palette.persist_frequency)
        m_command_manager->saveUsageCounts(
            m_app_data_dir.filePath("command_usage.json"));

    // Commands registered from Lua hold LuaRefGuard shared_ptrs that call
    // luaL_unref in their destructor. Reset before lua_close so the Lua state
    // is still alive when those destructors run.
    m_command_manager.reset();

#ifdef WITH_LUA
    if (m_L)
        lua_close(m_L);
#endif
}

// On-demand construction of `Lektra` (for use with argparse)
void
Lektra::construct() noexcept
{
    initCommands();
    // initDefaultKeybinds() is now called from initConfig() so that
    // `[keybindings].load_defaults = false` can actually skip defaults.
    // Previously the outer call here plus the one inside initConfig()
    // produced duplicate QShortcuts (and duplicated entries in
    // m_config.keybinds[action]) whenever a user had a [keybindings] block.
    initDefaultMousebinds();
    initConfig();
#ifdef WITH_LUA
    initLua();
#endif
    initGui();
    // warnShortcutConflicts();
    initDB();
    trimRecentFilesDatabase();
    populateRecentFiles();
    populateBookmarks();
    initConnections();
    updateUiEnabledState();
    setMinimumSize(200, 150);
    this->show();
    resize(m_config.window.initial_size[0], m_config.window.initial_size[1]);
    installEventFilter(this);

    {
        const QString sentinel = m_app_data_dir.filePath(".first_run_done");
        if (!QFile::exists(sentinel))
        {
            QFile f(sentinel);
            (void)f.open(QIODevice::WriteOnly);
            QTimer::singleShot(500, this, [this]() { ShowDonate(); });
        }
    }

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnAppReady, this);
#endif
}

// Initialize the recent files store
void
Lektra::initDB() noexcept
{
    QString recentf = m_app_data_dir.filePath("last_pages.json");
    m_recent_files_store.setFilePath(recentf);
    if (!m_recent_files_store.load())
        qWarning() << "Failed to load recent files store";

    if (m_command_manager && m_config.command_palette.persist_frequency)
        m_command_manager->loadUsageCounts(
            m_app_data_dir.filePath("command_usage.json"));
}
#ifdef WITH_LLM_SUPPORT

void
Lektra::initLLMView() noexcept
{
    m_llm_view = new LLMView(m_config, this);
    this->addDockWidget(Qt::RightDockWidgetArea, m_llm_view);
}
#endif

// Initialize the GUI related Stuff
void
Lektra::initGui() noexcept
{
    QWidget *widget = new QWidget(this);
    this->setCentralWidget(widget);
    m_layout = new QVBoxLayout(widget);
    m_layout->setContentsMargins(0, 0, 0, 0);
    widget->setLayout(m_layout);
    widget->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_menuBar    = this->menuBar();
    m_tab_widget = new TabWidget(centralWidget());

    // Statusbar
    m_statusbar = new Statusbar(m_config.statusbar, this);
    m_statusbar->setPageInfoVisible(true);
    m_statusbar->setMode(GraphicsView::Mode::TextSelection);
    m_statusbar->setSessionName("");
    m_search_bar = new SearchBar(this);
    m_search_bar->setVisible(false);
    m_message_bar = new MessageBar(this);
    m_tab_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_layout->addWidget(m_tab_widget, 1);

    m_tab_widget->setCloseButtonMode(
        static_cast<TabBar::CloseButtonMode>(m_config.tabs.close_button_mode));
    m_tab_widget->setMovable(m_config.tabs.movable);
    m_tab_widget->setTabPosition(m_config.tabs.location);

    m_layout->addWidget(m_search_bar);
    m_layout->addWidget(m_message_bar);
    m_layout->addWidget(m_statusbar);

    m_tab_widget->setTabBarAutoHide(m_config.tabs.auto_hide);
    m_statusbar->setVisible(m_config.statusbar.visible);
    m_menuBar->setVisible(m_config.window.menubar);
    m_tab_widget->tabBar()->setVisible(m_config.tabs.visible);

    initMenubar();

    m_marks_manager = std::make_unique<MarkManager>(this);

    m_layout->setContentsMargins(0, 0, 0, 0);
    setContentsMargins(0, 0, 0, 0);
}

// Initialize all the connections for the `Lektra` class
void
Lektra::initConnections() noexcept
{
    connect(m_statusbar, &Statusbar::modeColorChangeRequested, this,
            [&](GraphicsView::Mode mode) { modeColorChangeRequested(mode); });

    connect(m_statusbar, &Statusbar::pageChangeRequested, this,
            &Lektra::gotoPage);

    QList<QScreen *> outputs = QGuiApplication::screens();
    connect(m_tab_widget, &TabWidget::currentChanged, this,
            &Lektra::handleCurrentTabChanged);

    connect(m_tab_widget, &TabWidget::tabAdded, this, [this](int index)
    {
#ifdef WITH_LUA
        dispatchLuaEvent(DispatchType::OnTabAdded, &index);
#endif
    });

    // Tab drag and drop connections for cross-window tab transfer
    connect(m_tab_widget, &TabWidget::tabDataRequested, this,
            &Lektra::handleTabDataRequested);
    connect(m_tab_widget, &TabWidget::tabDropReceived, this,
            &Lektra::handleTabDropReceived);
    connect(m_tab_widget, &TabWidget::tabDetached, this,
            &Lektra::handleTabDetached);
    connect(m_tab_widget, &TabWidget::tabDetachedToNewWindow, this,
            &Lektra::handleTabDetachedToNewWindow);
    connect(m_tab_widget, &TabWidget::contextMenuRequested, this,
            &Lektra::handleTabContextMenu);

    QWindow *win = window()->windowHandle();

    m_dpr = m_screen_dpr_map.value(win->screen()->name(), 1.0f);

    connect(win, &QWindow::screenChanged, this, &Lektra::handleScreenChange);

    connect(m_search_bar, &SearchBar::searchRequested, this,
            [this](const QString &term, bool useRegex)
    {
        if (m_doc)
            m_doc->Search(term, useRegex);
    });

    connect(m_search_bar, &SearchBar::searchIndexChangeRequested, this,
            &Lektra::GotoHit);
    connect(m_search_bar, &SearchBar::nextHitRequested, this, &Lektra::NextHit);
    connect(m_search_bar, &SearchBar::prevHitRequested, this, &Lektra::PrevHit);

    connect(m_tab_widget, &TabWidget::tabCloseRequested, this,
            &Lektra::handleTabCloseRequested);

    connect(m_navMenu, &QMenu::aboutToShow, this,
            &Lektra::updatePageNavigationActions);
}

void
Lektra::closeEvent(QCloseEvent *e)
{
    // Update session file if in session
    if (!m_session_name.isEmpty())
        writeSessionToFile();

    // First pass: handle all unsaved changes dialogs and mark documents as
    // handled
    for (int i = 0; i < m_tab_widget->count(); i++)
    {
        DocumentContainer *container = m_tab_widget->rootContainer(i);
        if (!container)
            continue;

        for (DocumentView *doc : container->getAllViews())
        {
            if (!doc)
                continue;

            if (m_config.behavior.remember_last_visited && !doc->is_portal()
                && !doc->noHistory())
            {
                const int page = doc->pageNo() + 1;
                insertFileToDB(doc->filePath(), page > 0 ? page : 1);
            }

            // Unsaved Changes
            if (doc->isModified())
            {
                int ret = QMessageBox::warning(
                    this, tr("Unsaved Changes"),
                    tr("File %1 has unsaved changes. Do you want to save "
                       "them?")
                        .arg(m_tab_widget->tabText(i)),
                    QMessageBox::Save | QMessageBox::Discard
                        | QMessageBox::Cancel,
                    QMessageBox::Save);

                if (ret == QMessageBox::Cancel)
                {
                    e->ignore();
                    return;
                }

                else if (ret == QMessageBox::Save)
                {
                    doc->SaveFile();
                }
            }
        }
    }

    if (m_config.behavior.confirm_on_quit)
    {
        int ret = QMessageBox::question(
            this, tr("Confirm Quit"),
            tr("Are you sure you want to quit Lektra?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (ret == QMessageBox::No)
        {
            e->ignore();
            return;
        }
    }

    m_bookmark_manager.saveBookmarks(m_bookmarks_file_path);

    e->accept();

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnAppShutdown);
#endif
}

// Event filter to capture key events for link hints mode and
// other events
bool
Lektra::eventFilter(QObject *object, QEvent *event)
{
    const QEvent::Type type = event->type();

    if (m_link_hint_mode)
    {
        return handleLinkHintEvent(event);
    }

    // TODO: Do this cleanly, looks like spaghetti code.
    // Close preview window on Escape
    if (type == QEvent::KeyRelease)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape)
        {
            if (m_preview_view && m_preview_view->isVisible())
            {
                // if (QWidget *overlay = m_preview_view->parentWidget())
                //     overlay->deleteLater();
                // m_preview_view = nullptr;
                // TODO: maybe add config option ?
                m_preview_overlay->hide();
                return true;
            }

            if (m_command_picker)
            {
                if (m_command_picker->isVisible())
                {
                    m_command_picker->hide();
                    return true;
                }
            }

            if (m_outline_picker)
            {
                if (m_outline_picker->isVisible())
                {
                    m_outline_picker->hide();
                    return true;
                }
            }

            if (m_highlight_search_picker)
            {
                if (m_highlight_search_picker->isVisible())
                {
                    m_highlight_search_picker->hide();
                    return true;
                }
            }

            if (m_comment_search_picker)
            {
                if (m_comment_search_picker->isVisible())
                {
                    m_comment_search_picker->hide();
                    return true;
                }
            }

            if (m_recent_file_picker)
            {
                if (m_recent_file_picker->isVisible())
                {
                    m_recent_file_picker->hide();

                    return true;
                }
            }

            return true;
        }
    }

    // Close preview when clicking outside the inner container
    if (m_preview_overlay && m_preview_overlay->isVisible()
        && m_config.preview.close_on_click_outside)
    {
        if (type == QEvent::MouseButtonPress)
        {
            {
                // Check if click is on the overlay background (not the
                // inner container)
                if (object == m_preview_overlay)
                {
                    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
                    QWidget *innerContainer
                        = m_preview_overlay->findChild<QWidget *>(
                            "linkPreviewInner");
                    if (innerContainer)
                    {
                        QPoint posInOverlay = mouseEvent->pos();
                        QRect innerRect     = innerContainer->geometry();
                        if (!innerRect.contains(posInOverlay))
                        {
                            m_preview_overlay->hide();
                            return true;
                        }
                    }
                }
            }
        }
    }

    // Let other events pass through
    return QObject::eventFilter(object, event);
}

// Update the statusbar info
void
Lektra::updateStatusbar() noexcept
{
    if (m_doc)
    {
        Model *model = m_doc->model();
        if (!model)
            return;

        m_statusbar->setFilePath(m_doc->filePath());
        m_statusbar->setPortalMode(m_doc->portal());
        m_statusbar->setNarrowMode(m_doc->isNarrowed());
        m_statusbar->setMode(m_doc->selectionMode());
        m_statusbar->setHighlightColor(model->highlightAnnotColor());

        const int numPages = model->numPages();

        const bool isImage = model->isImage();
        if (numPages > 0)
        {
            m_statusbar->setPageInfoVisible(isImage);
            m_statusbar->setTotalPageCount(numPages);
            m_statusbar->setPageNo(m_doc->pageNo() + 1);
        }
        else
        {
            // File still loading — hide until openFileFinished fires
            m_statusbar->setPageInfoVisible(!isImage);
        }
    }
    else
    {
        // setPageInfoVisible's `state` param means "hide" despite the
        // name (see the two call sites above: setPageInfoVisible(isImage)
        // hides page info for images) — pass true here to actually hide
        // the stale page/total-page/mode/progress labels rather than
        // leaving them showing the just-closed document's last values.
        m_statusbar->setPageInfoVisible(true);
        m_statusbar->setFilePath("");
        m_statusbar->setHighlightColor("");
        m_statusbar->setPortalMode(false);
        m_statusbar->setNarrowMode(false);
    }
}

// Shows the startup widget
void
Lektra::showStartupWidget() noexcept
{
    if (m_startup_widget)
    {
        int index = m_tab_widget->indexOf(m_startup_widget);
        if (index != -1)
            m_tab_widget->setCurrentIndex(index);
        return;
    }

    m_startup_widget = new StartupWidget(&m_recent_files_store, m_tab_widget);
    connect(m_startup_widget, &StartupWidget::openFileRequested, this,
            [this](const QString &path)
    {
        OpenFileInNewTab(path, [this](void *)
        {
            int index = m_tab_widget->indexOf(m_startup_widget);
            if (index != -1)
                m_tab_widget->tabCloseRequested(index);
        });
    });
    int index = m_tab_widget->addTab(m_startup_widget, tr("Startup"));
    m_tab_widget->setCurrentIndex(index);
    m_statusbar->setFilePath(tr("Start Page"));
}

// Update actions and stuff for system tabs
void
Lektra::updateActionsAndStuffForSystemTabs() noexcept
{
    m_statusbar->setPageInfoVisible(true);
    updateUiEnabledState();
    m_statusbar->setFilePath(tr("Start Page"));
}

// Undo operation
void
Lektra::Undo() noexcept
{
    if (m_doc && m_doc->model())
    {
        auto undoStack = m_doc->model()->undoStack();
        if (undoStack->canUndo())
            undoStack->undo();
    }
}

// Redo operation
void
Lektra::Redo() noexcept
{
    if (m_doc && m_doc->model())
    {
        auto redoStack = m_doc->model()->undoStack();
        if (redoStack->canRedo())
            redoStack->redo();
    }
}

// Sets the DPR of the current document
void
Lektra::SetDPR() noexcept
{
    if (m_doc)
    {
        QInputDialog id;
        bool ok;
        float dpr
            = id.getDouble(this, tr("Set DPR"),
                           tr("Enter the Device Pixel Ratio (DPR) value: "),
                           1.0, 0.0, 10.0, 2, &ok);
        if (ok)
            m_doc->setDPR(dpr);
        else
            QMessageBox::critical(this, tr("Set DPR"), tr("Invalid DPR value"));
    }
}

void
Lektra::modeColorChangeRequested(const GraphicsView::Mode mode) noexcept
{
    ColorDialog colorDialog(m_config.misc.color_dialog_colors, QColor(), this);
    colorDialog.setWindowTitle(tr("Select Color"));

    if (colorDialog.exec() == QDialog::Accepted)
    {
        QColor color = colorDialog.selectedColor();
        auto model   = m_doc->model();
        if (mode == GraphicsView::Mode::AnnotRect)
            model->setAnnotRectColor(color);
        else if (mode == GraphicsView::Mode::TextHighlight)
            model->setHighlightColor(color);
        else if (mode == GraphicsView::Mode::TextSelection)
            model->setSelectionColor(color);
        else if (mode == GraphicsView::Mode::AnnotPopup)
            model->setPopupColor(color);

        m_statusbar->setHighlightColor(color);
    }
}

// Handle Escape key press for the entire application
void
Lektra::handleEscapeKeyPressed() noexcept
{
#ifndef NDEBUG
    qDebug() << "Escape key pressed handled";
#endif

    m_lockedInputBuffer.clear();

    if (m_link_hint_mode)
    {
        m_doc->ClearKBHintsOverlay();
        m_link_hint_map.clear();
        m_link_hint_mode = false;
    }
}

void
Lektra::setCurrentDocumentView(DocumentView *view) noexcept
{
    if (m_doc == view)
        return;

    if (m_doc)
        m_doc->setActive(false);

    // Clear the picker so it doesn't hold a stale pointer from the previous doc
    if (m_outline_picker)
        m_outline_picker->clearOutline();

    m_doc = view;

    if (!view)
    {
        // No tabs left in this window (e.g. the last/only tab was just
        // closed or detached to another window) — still have to refresh
        // the statusbar/UI-enabled state to the empty-document state,
        // not just bail out, or they stay stuck showing the closed
        // document.
        updateUiEnabledState();
        updatePageNavigationActions();
        updateStatusbar();
        return;
    }

    view->setActive(true);

    const int tabIndex = m_tab_widget->currentIndex();

    DocumentContainer *container = m_tab_widget->rootContainer(tabIndex);
    if (!container)
        return;

    m_tab_widget->tabBar()->setTabText(tabIndex, m_config.tabs.full_path
                                                     ? m_doc->filePath()
                                                     : m_doc->fileName());
    updateUiEnabledState();
    updatePageNavigationActions();
    updateStatusbar();
}

void
Lektra::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (m_preview_overlay && m_preview_overlay->isVisible())
    {
        // Resize overlay to fill window
        m_preview_overlay->resize(size());

        // Resize inner container
        QWidget *innerContainer
            = m_preview_overlay->findChild<QWidget *>("linkPreviewInner");
        if (innerContainer)
        {
            const QSize innerSize(width() * m_config.preview.size_ratio[0],
                                  height() * m_config.preview.size_ratio[1]);
            innerContainer->setFixedSize(innerSize);
        }
    }
}
#ifdef WITH_LLM_SUPPORT

void
Lektra::ToggleLLMView() noexcept
{
    if (!m_llm_view)
    {
        initLLMView();
        return;
    }

    m_llm_view->setVisible(!m_llm_view->isVisible());
}
#endif

void
Lektra::onNewIPCConnection()
{
    QLocalServer *server = qobject_cast<QLocalServer *>(sender());
    if (!server)
        return;
    QLocalSocket *clientSocket = server->nextPendingConnection();
    if (!clientSocket)
        return;
    connect(clientSocket, &QLocalSocket::readyRead, this,
            &Lektra::onIPCDataReady);
    connect(clientSocket, &QLocalSocket::disconnected, clientSocket,
            &QLocalSocket::deleteLater);
}

void
Lektra::onIPCDataReady()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket)
        return;

    const QByteArray data = socket->readAll();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);

    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject msg  = doc.object();
    const QJsonArray files = msg["files"].toArray();
    const int page         = msg["page"].toInt(-1);
    const bool vsplit      = msg["vsplit"].toBool();
    const bool hsplit      = msg["hsplit"].toBool();
    const QString cmd      = msg["command"].toString();

    QStringList filePaths;
    for (const auto &v : files)
        filePaths.push_back(v.toString());

    if (!filePaths.isEmpty())
    {
        if (vsplit)
            OpenFilesInVSplit(filePaths);
        else if (hsplit)
            OpenFilesInHSplit(filePaths);
        else if (filePaths.size() == 1)
        {
            OpenFileInNewTab(filePaths[0], [page, this](void *)
            {
                if (page > 0)
                    gotoPage(page);
            });
        }
        else
            OpenFiles(filePaths);
    }

#ifdef WITH_SYNCTEX
    const QString synctexArg = msg["synctex_forward"].toString();
    if (!synctexArg.isEmpty())
    {
        static const QRegularExpression re(
            QStringLiteral(R"(^(.*)#(.*):(\d+):(\d+)$)"));
        QRegularExpressionMatch match = re.match(synctexArg);
        if (match.hasMatch())
        {
            const QString pdfPath = match.captured(1).replace(
                QLatin1Char('~'), QString::fromLatin1(HOME_DIR));
            const QString texPath = match.captured(2).replace(
                QLatin1Char('~'), QString::fromLatin1(HOME_DIR));
            const int fwdLine = match.captured(3).toInt();
            const int fwdCol  = match.captured(4).toInt();

            // Find an already-open view for this PDF and jump in-place.
            DocumentView *existing = nullptr;
            const QFileInfo pdfInfo(pdfPath);
            for (int i = 0; i < m_tab_widget->count(); ++i)
            {
                DocumentContainer *c = m_tab_widget->rootContainer(i);
                if (!c)
                    continue;
                for (DocumentView *v : c->getAllViews())
                {
                    if (v && QFileInfo(v->filePath()) == pdfInfo)
                    {
                        existing = v;
                        m_tab_widget->setCurrentIndex(i);
                        break;
                    }
                }
                if (existing)
                    break;
            }

            if (existing)
            {
                existing->synctexForwardSearch(texPath, fwdLine, fwdCol);
            }
            else
            {
                OpenFileInNewTab(pdfPath, [texPath, fwdLine, fwdCol](void *ptr)
                {
                    auto *lektra = static_cast<Lektra *>(ptr);
                    if (auto *view = lektra->currentDocument())
                        view->synctexForwardSearch(texPath, fwdLine, fwdCol);
                });
            }
        }
    }
#endif

    if (!cmd.isEmpty())
    {
        const QStringList cmdList = cmd.split(';', Qt::SkipEmptyParts);
        for (const QString &c : cmdList)
        {
            QStringList parts = c.split(' ', Qt::SkipEmptyParts);
            if (parts.isEmpty())
                continue;
            auto command = m_command_manager->find(parts[0]);
            if (!command.name.isEmpty())
                command.action(parts.mid(1));
        }
    }

    this->raise();
    this->activateWindow();
}

void
Lektra::startIPCServer(const QString &name)
{
#ifndef NDEBUG
    qDebug() << "Starting IPC server with name:" << name;
#endif

    auto *server = new QLocalServer(this);
    // Remove existing socket file if it crashed previously
    QLocalServer::removeServer(name);

    if (server->listen(name))
    {
        connect(server, &QLocalServer::newConnection, this,
                &Lektra::onNewIPCConnection);
    }
}

// Add this helper before Read_args_parser
// TODO: Don't do this hacky stuff, refactor the argument parsing logic to
// be more flexible and not require this
bool
Lektra::readSingleInstanceFromConfig() noexcept
{
    try
    {
        const toml::table tbl
            = toml::parse_file(m_config_file_path.toStdString());
        if (auto v = tbl["behavior"]["single_instance"].value<bool>())
            return *v;
    }
    catch (...)
    {
    }
    return false; // default off if config missing/unparseable
}

QStringList
Lektra::getKeybindings(const QString &cmdname) const noexcept
{
    for (const auto &cmd : m_command_manager->const_commands())
    {
        if (cmd.name == cmdname)
            return m_config.keybinds[cmdname];
    }

    return {};
}
#ifdef WITH_LUA

// QStringList
// Lektra::getMousebindings(const QString &action) const noexcept
// {
// }
//
void
Lektra::loadLuaConfig() noexcept
{
    if (m_skip_lua_config)
        return;

    const QString init_file = m_init_file_path.isEmpty()
                                  ? m_config_dir.filePath("init.lua")
                                  : m_init_file_path;
    if (QFile::exists(init_file))
    {
        const std::string config_path
            = m_config_dir.absolutePath().toStdString();
        const std::string path_snippet = "package.path = \"" + config_path
                                         + "/?.lua;" + config_path
                                         + "/?/init.lua;\" .. package.path";
        luaL_dostring(m_L, path_snippet.c_str());

        if (luaL_dofile(m_L, init_file.toStdString().c_str()) != LUA_OK)
        {
            qWarning() << "Failed to execute init.lua:"
                       << lua_tostring(m_L, -1);
            QMessageBox::critical(nullptr, "Lua Error",
                                  "Failed to execute init.lua:\n"
                                      + QString(lua_tostring(m_L, -1)));
            lua_pop(m_L, 1);
        }
    }
}
#endif

void
Lektra::handleScreenChange(QScreen *screen) noexcept
{
    if (std::holds_alternative<QMap<QString, float>>(m_config.rendering.dpr))
    {
        m_dpr = m_screen_dpr_map.value(screen->name(), 1.0f);
        if (m_doc)
            m_doc->setDPR(m_dpr);
    }
    else if (std::holds_alternative<float>(m_config.rendering.dpr))
    {
        m_dpr = std::get<float>(m_config.rendering.dpr);
        if (m_doc)
            m_doc->setDPR(m_dpr);
    }

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnScreenChanged, screen);
#endif
}

void
Lektra::applyWindowBackground() noexcept
{
    if (!m_tab_widget)
        return;
    for (int i = 0; i < m_tab_widget->count(); ++i)
    {
        DocumentContainer *container = m_tab_widget->rootContainer(i);
        if (!container)
            continue;
        for (DocumentView *view : container->getAllViews())
        {
            if (view)
                view->graphicsView()->viewport()->update();
        }
    }
}
