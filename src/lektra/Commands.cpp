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

extern const char *HOME_DIR;

// Reads the arguments passed with `Lektra` from the
// commandline
void
Lektra::Read_args_parser(const argparse::ArgumentParser &argparser) noexcept
{

    if (argparser.is_used("version"))
    {
        QTextStream out(stdout);
        out << "Lektra version: " << APP_VERSION << Qt::endl;
        QCoreApplication::exit(0);
        return;
    }

    if (argparser.is_used("list-commands"))
    {
        QTextStream out(stdout);
        out << "Available commands:" << Qt::endl;

        initCommands();

        // Calculate the maximum width of command names for alignment
        int max_width = 0;

        auto commands = m_command_manager->commands();
        std::sort(commands.begin(), commands.end(),
                  [](Command a, Command b) { return a.name < b.name; });

        for (const auto &cmd : commands)
            max_width = std::max(max_width, static_cast<int>(cmd.name.size()));

        for (const auto &cmd : commands)
        {
            const QString line = QString("  %1  %2")
                                     .arg(cmd.name, -max_width)
                                     .arg(cmd.description);
            out << line << Qt::endl;
        }

        QCoreApplication::exit(0);
        return;
    }

    if (argparser.is_used("config"))
    {
        const QString config_arg
            = QString::fromStdString(argparser.get<std::string>("--config"));

        if (config_arg.endsWith(".lua", Qt::CaseInsensitive))
        {
            m_init_file_path   = config_arg;
            m_skip_toml_config = true;
        }
        else
        {
            m_config_file_path = config_arg;
            m_skip_lua_config  = true;
        }
    }

    // IPC probe — must happen before construct() so no window is created
    const QString ipcName
        = argparser.is_used("socket")
              ? QString::fromStdString(argparser.get<std::string>("--socket"))
              : QStringLiteral("lektra-ipc");
#ifdef WITH_SYNCTEX
    const bool hasSynctexForward = argparser.is_used("synctex-forward");
#else
    const bool hasSynctexForward = false;
#endif
    const bool newWindow      = argparser.is_used("new-window");
    const bool singleInstance = argparser.is_used("single-instance")
                                || readSingleInstanceFromConfig();
    // A new window must not hand its files to the running instance, nor take
    // over the socket it is listening on.
    bool socketInUse          = false;
    if (newWindow)
    {
        QLocalSocket probe;
        probe.connectToServer(ipcName);
        socketInUse = probe.waitForConnected(300);
    }
    // Hands the files (or the synctex request) to a running instance and exits;
    // returns if there is none to talk to.
    const auto handOffToRunningInstance = [&]()
    {
        QLocalSocket probe;
        probe.connectToServer(ipcName);
        if (probe.waitForConnected(300))
        {
            QJsonObject msg;

            if (argparser.is_used("files"))
            {
                auto files = argparser.get<std::vector<std::string>>("files");
                QJsonArray fileArr;
                QStringList names;
                for (const auto &f : files)
                    fileArr.append(QString::fromLocal8Bit(f.c_str()));
                msg["files"]  = fileArr;
                msg["page"]   = argparser.is_used("page")
                                    ? argparser.get<int>("--page")
                                    : -1;
                msg["vsplit"] = argparser.is_used("vsplit");
                msg["hsplit"] = argparser.is_used("hsplit");
                if (argparser.is_used("command"))
                    msg["command"] = QString::fromStdString(
                        argparser.get<std::string>("--command"));
            }

#ifdef WITH_SYNCTEX
            if (hasSynctexForward)
                msg["synctex_forward"] = QString::fromStdString(
                    argparser.get<std::string>("--synctex-forward"));
#endif

            probe.write(QJsonDocument(msg).toJson(QJsonDocument::Compact));
            probe.flush();
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            std::exit(0);
        }
    };

    if (!newWindow
        && (hasSynctexForward
            || (singleInstance && argparser.is_used("files"))))
        handOffToRunningInstance();

    // This creates the UI and applies the initial user config file settings
    this->construct();

    applyCommandLineOverrides(argparser);

    // The option may also have been set from init.lua
    // (lektra.opt.behavior.single_instance), which only ran during construct(),
    // so decide again now: hand over to a running instance if there is one,
    // and otherwise become the one that listens.
    const bool singleInstanceNow
        = singleInstance || m_config.behavior.single_instance;
    if (!newWindow && !singleInstance && singleInstanceNow
        && argparser.is_used("files"))
        handOffToRunningInstance();

    if (!socketInUse && (singleInstanceNow || argparser.is_used("socket")))
        startIPCServer(ipcName); // correct place — after construct()

    if (argparser.is_used("about"))
    {
        ShowAbout();
    }

    if (argparser.is_used("session"))
    {
        const QString &sessionName
            = QString::fromStdString(argparser.get<std::string>("--session"));
        LoadSession(sessionName);
    }

    if (argparser.is_used("page"))
        m_config.behavior._startpage_override = argparser.get<int>("--page");

#ifdef WITH_SYNCTEX
    if (argparser.is_used("synctex-forward"))
    {
        // Format: --synctex-forward={pdf}#{src}:{line}:{column}
        // Example: --synctex-forward=test.pdf#main.tex:14
        const QString &arg = QString::fromStdString(
            argparser.get<std::string>("--synctex-forward"));

        // Format: file.pdf#file.tex:line
        static const QRegularExpression re(
            QStringLiteral(R"(^(.*)#(.*):(\d+):(\d+)$)"));
        QRegularExpressionMatch match = re.match(arg);

        if (match.hasMatch())
        {
            const QString pdfPath = match.captured(1).replace(
                QLatin1Char('~'), QString::fromLatin1(HOME_DIR));
            const QString texPath = match.captured(2).replace(
                QLatin1Char('~'), QString::fromLatin1(HOME_DIR));
            const int line = match.captured(3).toInt();
            const int col  = match.captured(4).toInt();

            // Pass as callback so remember_last_visited doesn't override the
            // synctex jump position (callback presence suppresses savedPage).
            OpenFileInNewTab(pdfPath, [texPath, line, col](void *ptr)
            {
                auto *lektra = static_cast<Lektra *>(ptr);
                if (auto *view = lektra->currentDocument())
                    view->synctexForwardSearch(texPath, line, col);
            });
        }
        else
        {
            qWarning() << tr("Invalid --synctex-forward format. Expected "
                             "file.pdf#file.tex:line:column");
        }
    }
#endif

    bool hsplit = false;
    bool vsplit = false;

    if (argparser.is_used("vsplit"))
    {
        vsplit = true;
    }

    if (argparser.is_used("hsplit"))
    {
        hsplit = true;
    }

    if (argparser.is_used("tutorial"))
    {
        showTutorialFile();
        return;
    }

    // Build the list of CLI commands to run after a file loads
    auto runCliCommands = [&]()
    {
        if (!argparser.is_used("command"))
            return;

        QTextStream err(stderr);
        const std::string command_str = argparser.get<std::string>("--command");
        const QStringList command_list = QString::fromStdString(command_str)
                                             .split(';', Qt::SkipEmptyParts);
        for (const QString &cmd : command_list)
        {
            QStringList parts = cmd.split(' ', Qt::SkipEmptyParts);
            if (parts.isEmpty())
                continue;

            const QString cmd_name = parts[0];
            const QStringList args = parts.mid(1);
            auto c                 = m_command_manager->find(cmd_name);
            if (!c.name.isEmpty())
                c.action(args);
            else
                err << tr("Unknown command from command line:") << cmd_name
                    << Qt::endl;
        }
    };

    if (argparser.is_used("files"))
    {
        auto files = argparser.get<std::vector<std::string>>("files");
        QStringList qtFiles;
        qtFiles.reserve(static_cast<int>(files.size()));
        for (const auto &file : files)
            qtFiles.push_back(QString::fromLocal8Bit(file.c_str()));
        m_config.behavior.open_last_visited = false;
        const int pageOverride = m_config.behavior._startpage_override;

        if (!qtFiles.isEmpty())
        {
            if (hsplit)
                OpenFilesInHSplit(qtFiles);
            else if (vsplit)
                OpenFilesInVSplit(qtFiles);
            else
            {
                // Open the first file with a callback so runCliCommands
                // fires only after that document has actually loaded;
                // previously the multi-file branch ran runCliCommands
                // synchronously before any file had parsed, so e.g.
                // `lektra a.pdf b.pdf --command "goto 5"` executed against
                // whatever tab was current before (usually nothing).
                OpenFileInNewTab(qtFiles[0],
                                 [pageOverride, this, runCliCommands](void *)
                {
                    if (pageOverride > 0)
                        gotoPage(pageOverride);
                    runCliCommands();
                });
                // Remaining files each get a tab of their own, only opened
                // when first shown if tabs.lazy_load is on (a shell glob can
                // easily be hundreds of files); setting the current index
                // back to 0 keeps the user on the first file so
                // runCliCommands (which acts on m_doc) targets it.
                for (int i = 1; i < qtFiles.size(); ++i)
                {
                    if (m_config.tabs.lazy_load)
                        insertLazyTab(qtFiles[i]);
                    else
                        OpenFileInNewTab(qtFiles[i]);
                }
                if (qtFiles.size() > 1)
                    m_tab_widget->setCurrentIndex(0);
            }
        }
        else if (m_config.behavior.open_last_visited)
        {
            openLastVisitedFile();
        }
    }

    if (m_tab_widget->count() == 0 && m_config.window.startup_tab)
        showStartupWidget();
    m_config.behavior._startpage_override = -1;
}

void
Lektra::applyCommandLineOverrides(
    const argparse::ArgumentParser &argparser) noexcept
{
    if (argparser.is_used("layout"))
    {
        DocumentView::LayoutMode mode;
        const std::string str = argparser.get("--layout");

        if (str == "vertical")
            mode = DocumentView::LayoutMode::VERTICAL;
        else if (str == "single")
            mode = DocumentView::LayoutMode::SINGLE;
        else if (str == "horizontal")
            mode = DocumentView::LayoutMode::HORIZONTAL;
        else if (str == "book")
            mode = DocumentView::LayoutMode::BOOK;
        else if (str == "grid")
            mode = DocumentView::LayoutMode::GRID;
        else
            mode = DocumentView::LayoutMode::VERTICAL;

        m_config.layout.mode = mode;
    }
}

void
Lektra::initCommands() noexcept
{
    // Selection
    m_command_manager = std::make_unique<CommandManager>();

    m_command_manager->reg("selection_copy",
                           tr("Copy current selection to clipboard"),
                           [this](const QStringList &) { Selection_copy(); });
    m_command_manager->reg("selection_cancel",
                           tr("Cancel and clear current selection"),
                           [this](const QStringList &) { Selection_cancel(); });
    m_command_manager->reg(
        "selection_last", tr("Reselect the last text selection"),
        [this](const QStringList &) { ReselectLastTextSelection(); });

    // Toggles
    m_command_manager->reg("thumbnail_panel", tr("Toggle thumbnail panel"),
                           [this](const QStringList &)
    { ToggleThumbnailPanel(); });

    m_command_manager->reg(
        "trim_margins", tr("Toggle trim margins (hide blank page margins)"),
        [this](const QStringList &) { ToggleTrimMargins(); });

    // Multi-tab selection and operations. Operations act on the selected
    // tabs, or on the current tab when nothing is selected.
    m_command_manager->reg("tabs_select_toggle",
                           tr("Toggle selection of the current tab"),
                           [this](const QStringList &)
    {
        if (m_tab_widget->currentIndex() >= 0)
            m_tab_widget->tabBar()->setTabSelected(
                m_tab_widget->currentIndex(),
                !m_tab_widget->tabBar()->selectedTabs().contains(
                    m_tab_widget->currentIndex()));
    });
    m_command_manager->reg("tabs_select_all", tr("Select all tabs"),
                           [this](const QStringList &)
    { m_tab_widget->tabBar()->selectAllTabs(); });
    m_command_manager->reg("tabs_select_clear", tr("Clear tab selection"),
                           [this](const QStringList &)
    { m_tab_widget->tabBar()->clearTabSelection(); });
    m_command_manager->reg("tab_rename",
                           tr("Rename the current tab (empty name resets it)"),
                           [this](const QStringList &args)
    {
        if (!m_tab_widget || !validTabIndex(m_tab_widget->currentIndex()))
            return;
        const int index = m_tab_widget->currentIndex();
        QString title;
        if (!args.isEmpty())
            title = args.join(' ');
        else
        {
            bool ok = false;
            title   = QInputDialog::getText(
                this, tr("Rename Tab"), tr("Tab name (empty to reset):"),
                QLineEdit::Normal, m_tab_widget->tabText(index), &ok);
            if (!ok)
                return;
        }
        renameTab(index, title);
    });
    m_command_manager->reg("tabs_close_selected", tr("Close the selected tabs"),
                           [this](const QStringList &)
    { closeTabs(targetTabs()); });
    m_command_manager->reg("tabs_merge_vertical",
                           tr("Merge the selected tabs into a vertical split"),
                           [this](const QStringList &)
    { mergeTabsAsSplits(targetTabs(), true); });
    m_command_manager->reg(
        "tabs_merge_horizontal",
        tr("Merge the selected tabs into a horizontal split"),
        [this](const QStringList &)
    { mergeTabsAsSplits(targetTabs(), false); });
    m_command_manager->reg(
        "tabs_split_out",
        tr("Move the splits of the selected tabs into separate tabs"),
        [this](const QStringList &) { splitTabsIntoTabs(targetTabs()); });
    m_command_manager->reg(
        "tabs_move_to_window", tr("Move the selected tabs to a new window"),
        [this](const QStringList &) { moveTabsToNewWindow(targetTabs()); });
    m_command_manager->reg(
        "tabs_save_session",
        tr("Save the selected tabs as a session (optional name)"),
        [this](const QStringList &args)
    { saveTabsAsSession(targetTabs(), args.join(QLatin1Char(' '))); });

    // Caret mode (accessibility: keyboard-driven character-level text
    // cursor, cf. Firefox/Okular "caret browsing"). Left/Right/Up/Down also
    // work via h/j/k/l when caret mode is active, same as visual line mode.
    m_command_manager->reg("caret_mode",
                           tr("Toggle caret mode (keyboard text cursor)"),
                           [this](const QStringList &) { ToggleCaretMode(); });
    m_command_manager->reg("caret_left", tr("Caret mode: move left"),
                           [this](const QStringList &) { CaretLeft(); });
    m_command_manager->reg("caret_right", tr("Caret mode: move right"),
                           [this](const QStringList &) { CaretRight(); });
    m_command_manager->reg("caret_up", tr("Caret mode: move up"),
                           [this](const QStringList &) { CaretUp(); });
    m_command_manager->reg("caret_down", tr("Caret mode: move down"),
                           [this](const QStringList &) { CaretDown(); });
    m_command_manager->reg("caret_line_start",
                           tr("Caret mode: move to start of line"),
                           [this](const QStringList &) { CaretLineStart(); });
    m_command_manager->reg("caret_line_end",
                           tr("Caret mode: move to end of line"),
                           [this](const QStringList &) { CaretLineEnd(); });
    m_command_manager->reg("caret_select_left",
                           tr("Caret mode: extend selection left"),
                           [this](const QStringList &) { CaretSelectLeft(); });
    m_command_manager->reg("caret_select_right",
                           tr("Caret mode: extend selection right"),
                           [this](const QStringList &) { CaretSelectRight(); });
    m_command_manager->reg("caret_select_up",
                           tr("Caret mode: extend selection up"),
                           [this](const QStringList &) { CaretSelectUp(); });
    m_command_manager->reg("caret_select_down",
                           tr("Caret mode: extend selection down"),
                           [this](const QStringList &) { CaretSelectDown(); });

#ifdef WITH_LLM_SUPPORT
    m_command_manager->reg("llm_view", tr("Toggle LLM chat panel"),
                           [this](const QStringList &) { ToggleLLMView(); });
#endif

    m_command_manager->reg("presentation_mode", tr("Toggle presentation mode"),
                           [this](const QStringList &)
    { TogglePresentationMode(); });
    m_command_manager->reg(
        "laser_pointer_cursor", tr("Toggle laser pointer cursor"),
        [this](const QStringList &) { ToggleLaserPointerCursor(); });
    m_command_manager->reg("fullscreen", tr("Toggle fullscreen"),
                           [this](const QStringList &) { ToggleFullscreen(); });
    m_command_manager->reg("run_last_command", tr("Run the last command again"),
                           [this](const QStringList &)
    {
        if (!m_command_manager->runLast())
            m_message_bar->showMessage(tr("No command to repeat"));
    });
    m_command_manager->reg("command_palette", tr("Open command palette"),
                           [this](const QStringList &)
    { Show_command_picker(); });
    m_command_manager->reg("tabs", tr("Toggle tab bar"),
                           [this](const QStringList &) { ToggleTabBar(); });
    m_command_manager->reg("menubar", tr("Toggle menu bar"),
                           [this](const QStringList &) { ToggleMenubar(); });
    m_command_manager->reg("statusbar", tr("Toggle status bar"),
                           [this](const QStringList &) { ToggleStatusbar(); });
    m_command_manager->reg("focus_mode", tr("Toggle focus mode"),
                           [this](const QStringList &) { ToggleFocusMode(); });
    m_command_manager->reg("visual_line_mode", tr("Toggle visual line mode"),
                           [this](const QStringList &)
    { ToggleVisualLineMode(); });
    m_command_manager->reg(
        "toggle_comment_markers", tr("Toggle comment markers"),
        [this](const QStringList &) { ToggleCommentMarkers(); });

    // Link hints
    m_command_manager->reg("link_hint_visit",
                           tr("Open link using keyboard hint"),
                           [this](const QStringList &) { VisitLinkKB(); });
    m_command_manager->reg("link_hint_copy",
                           tr("Copy link URL using keyboard hint"),
                           [this](const QStringList &) { CopyLinkKB(); });

    // Page navigation
    m_command_manager->reg("page_first", tr("Go to first page"),
                           [this](const QStringList &) { FirstPage(); });
    m_command_manager->reg("page_last", tr("Go to last page"),
                           [this](const QStringList &) { LastPage(); });
    m_command_manager->reg("page_next", tr("Go to next page"),
                           [this](const QStringList &) { NextPage(); });
    m_command_manager->reg("page_prev", tr("Go to previous page"),
                           [this](const QStringList &) { PrevPage(); });
    m_command_manager->reg("page_goto", tr("Jump to a specific page number"),
                           [this](const QStringList &args)
    { Goto_page(args); });

    // Bookmark
    m_command_manager->reg("bookmark_add", tr("Add bookmark"),
                           [this](const QStringList &) { AddBookmark(); });

    m_command_manager->reg("bookmark_remove", tr("Remove bookmark"),
                           [this](const QStringList &) { RemoveBookmark(); });

    m_command_manager->reg("bookmarks", tr("List bookmarks"),
                           [this](const QStringList &)
    { Show_bookmark_picker(); });

    m_command_manager->reg("bookmark_export",
                           tr("Export bookmarks to a JSON file"),
                           [this](const QStringList &args)
    { BookmarkExport(args.isEmpty() ? QString() : args.at(0)); });

    m_command_manager->reg("bookmark_import",
                           tr("Import bookmarks from a JSON file "
                              "(merges with existing set)"),
                           [this](const QStringList &args)
    { BookmarkImport(args.isEmpty() ? QString() : args.at(0)); });

    // Marks
    m_command_manager->reg("mark_set",
                           tr("Set a named mark at current position"),
                           [this](const QStringList &args) { SetMark(args); });
    m_command_manager->reg("mark_delete", tr("Delete a named mark"),
                           [this](const QStringList &args)
    { DeleteMark(args); });
    m_command_manager->reg("mark_goto", tr("Jump to a named mark"),
                           [this](const QStringList &args) { GotoMark(args); });

    // Scrolling
    m_command_manager->reg("scroll_down", tr("Scroll down"),
                           [this](const QStringList &) { ScrollDown(); });
    m_command_manager->reg("scroll_up", tr("Scroll up"),
                           [this](const QStringList &) { ScrollUp(); });
    m_command_manager->reg("scroll_left", tr("Scroll left"),
                           [this](const QStringList &) { ScrollLeft(); });
    m_command_manager->reg("scroll_right", tr("Scroll right"),
                           [this](const QStringList &) { ScrollRight(); });
    m_command_manager->reg("scroll_down_half_page", tr("Scroll down"),
                           [this](const QStringList &)
    { ScrollDown_HalfPage(); });
    m_command_manager->reg("scroll_up_half_page", tr("Scroll up"),
                           [this](const QStringList &)
    { ScrollUp_HalfPage(); });

    // Rotation / Flip
    m_command_manager->reg("rotate_clock", tr("Rotate page clockwise"),
                           [this](const QStringList &) { RotateClock(); });
    m_command_manager->reg("rotate_anticlock",
                           tr("Rotate page counter-clockwise"),
                           [this](const QStringList &) { RotateAnticlock(); });
    m_command_manager->reg("flip_horizontal", tr("Flip page horizontally"),
                           [this](const QStringList &) { FlipH(); });
    m_command_manager->reg("flip_vertical", tr("Flip page vertically"),
                           [this](const QStringList &) { FlipV(); });

    // Location history
    m_command_manager->reg("location_prev", tr("Go back in location history"),
                           [this](const QStringList &) { GoBackHistory(); });
    m_command_manager->reg("location_next",
                           tr("Go forward in location history"),
                           [this](const QStringList &) { GoForwardHistory(); });

    // Zoom
    m_command_manager->reg("zoom_in", tr("Zoom in"),
                           [this](const QStringList &) { ZoomIn(); });
    m_command_manager->reg("zoom_out", tr("Zoom out"),
                           [this](const QStringList &) { ZoomOut(); });
    m_command_manager->reg("zoom_reset", tr("Reset zoom to default"),
                           [this](const QStringList &) { ZoomReset(); });
    m_command_manager->reg("zoom_set", tr("Set zoom to a specific level"),
                           [this](const QStringList &args) { Zoom_set(args); });

    // Reflow text size (EPUB/FB2/MOBI only) — re-paginates the document,
    // unlike zoom_in/zoom_out which are pure raster scaling.
    m_command_manager->reg(
        "font_size_increase", tr("Increase text size (reflowable documents)"),
        [this](const QStringList &) { ReflowFontSizeIncrease(); });
    m_command_manager->reg(
        "font_size_decrease", tr("Decrease text size (reflowable documents)"),
        [this](const QStringList &) { ReflowFontSizeDecrease(); });
    m_command_manager->reg(
        "font_size_reset",
        tr("Reset text size to default (reflowable documents)"),
        [this](const QStringList &) { ReflowFontSizeReset(); });

    // Splits
    m_command_manager->reg("split_horizontal", tr("Split view horizontally"),
                           [this](const QStringList &) { VSplit(); });
    m_command_manager->reg("split_vertical", tr("Split view vertically"),
                           [this](const QStringList &) { HSplit(); });
    m_command_manager->reg("split_close", tr("Close current split"),
                           [this](const QStringList &) { Close_split(); });
    m_command_manager->reg("split_focus_right", tr("Focus split to the right"),
                           [this](const QStringList &)
    { Focus_split_right(); });
    m_command_manager->reg("split_focus_left", tr("Focus split to the left"),
                           [this](const QStringList &) { Focus_split_left(); });
    m_command_manager->reg("split_focus_up", tr("Focus split above"),
                           [this](const QStringList &) { Focus_split_up(); });
    m_command_manager->reg("split_focus_down", tr("Focus split below"),
                           [this](const QStringList &) { Focus_split_down(); });
    m_command_manager->reg(
        "split_close_others", tr("Close all splits except current"),
        [this](const QStringList &) { Close_other_splits(); });
    m_command_manager->reg(
        "split_maximize", tr("Toggle maximize focused split"),
        [this](const QStringList &) { ToggleSplitMaximize(); });

    m_command_manager->reg(
        "sync_view", tr("Pick views of this tab to sync (zoom and scroll)"),
        [this](const QStringList &) { SelectViews(); });
    m_command_manager->reg("sync_view_all",
                           tr("Sync zoom and scroll of all views of this tab"),
                           [this](const QStringList &) { SyncViews(); });
    m_command_manager->reg("sync_view_stop", tr("Stop syncing views"),
                           [this](const QStringList &) { StopSyncViews(); });

    m_command_manager->reg("split_to_windows",
                           tr("Move the splits of the current tab into "
                              "separate windows (first stays)"),
                           [this](const QStringList &) { SplitsToWindows(); });

    m_command_manager->reg("split_to_tabs",
                           tr("Move the splits of the current tab into "
                              "separate tabs (first stays)"),
                           [this](const QStringList &) { SplitsToTabs(); });

    // Portal
    m_command_manager->reg("portal", tr("Create or focus portal"),
                           [this](const QStringList &)
    { Create_or_focus_portal(); });

    // File operations
    m_command_manager->reg("file_open_tab", tr("Open file in new tab"),
                           [this](const QStringList &args)
    {
        if (args.isEmpty())
            OpenFileInNewTab();
        else
            OpenFileInNewTab(args.at(0));
    });
    m_command_manager->reg(
        "file_open_no_history",
        tr("Open file in new tab without adding it to recent files"),
        [this](const QStringList &args)
    { OpenFileInNewTab(args.isEmpty() ? QString() : args.at(0), {}, true); });
    m_command_manager->reg("file_open_vsplit",
                           tr("Open file in vertical split"),
                           [this](const QStringList &args)
    { OpenFileVSplit(args.isEmpty() ? "" : args.at(0)); });
    m_command_manager->reg("file_open_hsplit",
                           tr("Open file in horizontal split"),
                           [this](const QStringList &args)
    { OpenFileHSplit(args.isEmpty() ? "" : args.at(0)); });
    m_command_manager->reg("file_open_dwim", tr("Open file (do what I mean)"),
                           [this](const QStringList &args)
    { OpenFileDWIM(args.isEmpty() ? "" : args.at(0)); });
    // Registered under both names: file_open_window matches the existing
    // file_open_{tab,vsplit,hsplit,dwim} family; open_file_new_window is
    // the name used by the tutorial and prior user documentation.
    {
        auto handler = [this](const QStringList &args)
        {
            if (args.isEmpty())
                OpenFileInNewWindow();
            else
                OpenFileInNewWindow(args.at(0));
        };
        m_command_manager->reg("file_open_window",
                               tr("Open file in a new window"), handler);
        m_command_manager->reg("open_file_new_window",
                               tr("Open file in a new window"), handler);
    }
    m_command_manager->reg("file_close", tr("Close current file"),
                           [this](const QStringList &args)
    { CloseFile(args.isEmpty() ? "" : args.at(0)); });
    m_command_manager->reg("file_save", tr("Save current file"),
                           [this](const QStringList &) { SaveFile(); });
    m_command_manager->reg("file_save_as",
                           tr("Save current file as a new name"),
                           [this](const QStringList &) { SaveAsFile(); });
    m_command_manager->reg("file_encrypt", tr("Encrypt current document"),
                           [this](const QStringList &) { EncryptDocument(); });
    m_command_manager->reg("file_decrypt", tr("Decrypt current document"),
                           [this](const QStringList &) { DecryptDocument(); });
    m_command_manager->reg("file_reload", tr("Reload current file from disk"),
                           [this](const QStringList &) { reloadDocument(); });
    m_command_manager->reg("file_properties", tr("Show file properties"),
                           [this](const QStringList &) { FileProperties(); });
    m_command_manager->reg("files_recent", tr("Show recently opened files"),
                           [this](const QStringList &)
    { Show_recent_files_picker(); });
    m_command_manager->reg(
        "files_recent_clean",
        tr("Remove recent-files entries whose file no longer exists on disk"),
        [this](const QStringList &) { cleanRecentFilesDatabase(); });
    m_command_manager->reg("file_picker", tr("Open file picker"),
                           [this](const QStringList &) { Show_file_picker(); });

    // Annotation modes
    m_command_manager->reg(
        "annot_edit_mode", tr("Toggle annotation select mode"),
        [this](const QStringList &) { ToggleAnnotSelect(); });
    m_command_manager->reg("annot_popup_mode",
                           tr("Toggle annotation popup mode"),
                           [this](const QStringList &) { ToggleAnnotPopup(); });
    m_command_manager->reg("annot_rect_mode",
                           tr("Toggle rectangle annotation mode"),
                           [this](const QStringList &) { ToggleAnnotRect(); });
    m_command_manager->reg("annot_ellipse_mode",
                           tr("Toggle ellipse annotation mode"),
                           [this](const QStringList &) { ToggleAnnotEllipse(); });
    m_command_manager->reg("annot_polygon_mode",
                           tr("Toggle polygon annotation mode"),
                           [this](const QStringList &) { ToggleAnnotPolygon(); });
    m_command_manager->reg("annot_note_mode",
                           tr("Toggle inline note annotation mode"),
                           [this](const QStringList &) { ToggleAnnotNote(); });
    m_command_manager->reg(
        "annot_highlight_mode", tr("Toggle text highlight mode"),
        [this](const QStringList &) { ToggleTextHighlight(); });
    m_command_manager->reg(
        "annot_underline_mode", tr("Toggle text underline mode"),
        [this](const QStringList &) { ToggleTextUnderline(); });
    m_command_manager->reg("none_mode", tr("Toggle none interaction mode"),
                           [this](const QStringList &) { ToggleNoneMode(); });

    // Selection modes
    m_command_manager->reg(
        "selection_mode_text", tr("Switch to text selection mode"),
        [this](const QStringList &) { ToggleTextSelection(); });
    m_command_manager->reg(
        "selection_mode_region", tr("Switch to region selection mode"),
        [this](const QStringList &) { ToggleRegionSelect(); });
    m_command_manager->reg("narrow_to_region",
                           tr("Narrow view to selected region"),
                           [this](const QStringList &) { NarrowToRegion(); });
    m_command_manager->reg("zoom_to_selection",
                           tr("Select a region and zoom in to fill it"),
                           [this](const QStringList &) { ZoomToSelection(); });
    m_command_manager->reg(
        "narrow_to_section",
        tr("Narrow view to a document section from the outline"),
        [this](const QStringList &args) { NarrowToSection(args); });
    m_command_manager->reg(
        "narrow_to_pages",
        tr("Narrow view to a page range (e.g. '5 10' or '5-10')"),
        [this](const QStringList &args)
    {
        if (!m_doc)
            return;

        // Accept either two integer args ("5", "10") or a single "5-10".
        // With no args, prompt the user for a range.
        int start = -1, end = -1;
        bool ok1 = false, ok2 = false;
        QString rangeText;
        if (args.size() >= 2)
        {
            rangeText = args.at(0) + "-" + args.at(1);
        }
        else if (args.size() == 1)
        {
            rangeText = args.at(0);
        }
        else
        {
            bool accepted = false;
            rangeText     = QInputDialog::getText(
                this, tr("Narrow to Pages"),
                tr("Enter page range (e.g. '5 10' or '5-10'):"),
                QLineEdit::Normal, QString(), &accepted);
            if (!accepted || rangeText.trimmed().isEmpty())
                return;
        }

        const QStringList parts = rangeText.split(QRegularExpression("[\\s-]+"),
                                                  Qt::SkipEmptyParts);
        if (parts.size() >= 2)
        {
            start = parts.at(0).toInt(&ok1);
            end   = parts.at(1).toInt(&ok2);
        }

        if (!ok1 || !ok2)
        {
            QMessageBox::critical(
                this, tr("Narrow to Pages"),
                tr("Expected a page range like '5 10' or '5-10'."));
            return;
        }

        NarrowToPages(start, end);
    });
    m_command_manager->reg("widen_region", tr("Exit narrow region (widen)"),
                           [this](const QStringList &) { WidenRegion(); });

    // Fit modes
    m_command_manager->reg("fit_width", tr("Fit page to window width"),
                           [this](const QStringList &) { Fit_width(); });
    m_command_manager->reg("fit_height", tr("Fit page to window height"),
                           [this](const QStringList &) { Fit_height(); });
    m_command_manager->reg("fit_page", tr("Fit entire page in window"),
                           [this](const QStringList &) { Fit_page(); });
    m_command_manager->reg("fit_auto", tr("Toggle automatic resize to fit"),
                           [this](const QStringList &) { ToggleAutoResize(); });
    // Smart fit — ignore blank page margins, fit only the content region.
    // Registered under two names each: fit_width_smart / fit_height_smart
    // matches the existing fit_* family; fit_to_page_width_smart /
    // fit_to_page_height_smart is the name from the feature request.
    {
        auto width_smart_handler = [this](const QStringList &)
        {
            Fit_width_smart();
        };
        auto height_smart_handler = [this](const QStringList &)
        {
            Fit_height_smart();
        };
        m_command_manager->reg("fit_width_smart",
                               tr("Fit content width to window "
                                  "(ignoring blank margins)"),
                               width_smart_handler);
        m_command_manager->reg("fit_to_page_width_smart",
                               tr("Fit content width to window "
                                  "(ignoring blank margins)"),
                               width_smart_handler);
        m_command_manager->reg("fit_height_smart",
                               tr("Fit content height to window "
                                  "(ignoring blank margins)"),
                               height_smart_handler);
        m_command_manager->reg("fit_to_page_height_smart",
                               tr("Fit content height to window "
                                  "(ignoring blank margins)"),
                               height_smart_handler);
    }

    // Sessions
    m_command_manager->reg("session_save", tr("Save current session"),
                           [this](const QStringList &) { SaveSession(); });
    m_command_manager->reg("session_save_as",
                           tr("Save current session under a new name"),
                           [this](const QStringList &) { SaveAsSession(); });
    m_command_manager->reg("session_load", tr("Load a saved session"),
                           [this](const QStringList &) { LoadSession(); });

    // Tabs
    m_command_manager->reg("tabs_close_left", tr("Close all tabs to the left"),
                           [this](const QStringList &) { TabsCloseLeft(); });
    m_command_manager->reg("tabs_close_right",
                           tr("Close all tabs to the right"),
                           [this](const QStringList &) { TabsCloseRight(); });
    m_command_manager->reg("tabs_close_others",
                           tr("Close all tabs except current"),
                           [this](const QStringList &) { TabsCloseOthers(); });
    m_command_manager->reg("tab_move_right", tr("Move current tab right"),
                           [this](const QStringList &) { TabMoveRight(); });
    m_command_manager->reg("tab_move_left", tr("Move current tab left"),
                           [this](const QStringList &) { TabMoveLeft(); });
    m_command_manager->reg("tab_first", tr("Switch to first tab"),
                           [this](const QStringList &) { Tab_first(); });
    m_command_manager->reg("tab_last", tr("Switch to last tab"),
                           [this](const QStringList &) { Tab_last(); });
    m_command_manager->reg("tab_next", tr("Switch to next tab"),
                           [this](const QStringList &) { Tab_next(); });
    m_command_manager->reg("tab_prev", tr("Switch to previous tab"),
                           [this](const QStringList &) { Tab_prev(); });
    m_command_manager->reg("tab_close", tr("Close current tab"),
                           [this](const QStringList &) { Tab_close(); });
    m_command_manager->reg("tab_goto", tr("Go to tab by number"),
                           [this](const QStringList &) { Tab_goto(); });
    m_command_manager->reg("tab_1", tr("Switch to tab 1"),
                           [this](const QStringList &) { Tab_goto(1); });
    m_command_manager->reg("tab_2", tr("Switch to tab 2"),
                           [this](const QStringList &) { Tab_goto(2); });
    m_command_manager->reg("tab_3", tr("Switch to tab 3"),
                           [this](const QStringList &) { Tab_goto(3); });
    m_command_manager->reg("tab_4", tr("Switch to tab 4"),
                           [this](const QStringList &) { Tab_goto(4); });
    m_command_manager->reg("tab_5", tr("Switch to tab 5"),
                           [this](const QStringList &) { Tab_goto(5); });
    m_command_manager->reg("tab_6", tr("Switch to tab 6"),
                           [this](const QStringList &) { Tab_goto(6); });
    m_command_manager->reg("tab_7", tr("Switch to tab 7"),
                           [this](const QStringList &) { Tab_goto(7); });
    m_command_manager->reg("tab_8", tr("Switch to tab 8"),
                           [this](const QStringList &) { Tab_goto(8); });
    m_command_manager->reg("tab_9", tr("Switch to tab 9"),
                           [this](const QStringList &) { Tab_goto(9); });

    // Pickers
    m_command_manager->reg("picker_outline", tr("Open document outline picker"),
                           [this](const QStringList &) { ShowOutline(); });
    m_command_manager->reg(
        "generate_outline",
        tr("Generate outline from document text (font-size heuristic)"),
        [this](const QStringList &) { GenerateOutline(); });
    m_command_manager->reg("export_outline", tr("Export outline to JSON file"),
                           [this](const QStringList &) { ExportOutline(); });
    m_command_manager->reg("load_outline", tr("Load outline from JSON file"),
                           [this](const QStringList &) { LoadOutline(); });
    m_command_manager->reg(
        "picker_highlight_search", tr("Search within highlights"),
        [this](const QStringList &) { Show_highlight_search(); });

    m_command_manager->reg(
        "picker_annot_comment_search", tr("Search annotation comments"),
        [this](const QStringList &) { Show_annot_comment_search(); });

    // Search
    m_command_manager->reg("search", tr("Search document"),
                           [this](const QStringList &args) { Search(args); });
    m_command_manager->reg("search_regex", tr("Search document using regex"),
                           [this](const QStringList &args)
    { SearchRegex(args); });
    m_command_manager->reg("search_next", tr("Jump to next search result"),
                           [this](const QStringList &) { NextHit(); });
    m_command_manager->reg("search_prev", tr("Jump to previous search result"),
                           [this](const QStringList &) { PrevHit(); });
    m_command_manager->reg(
        "search_args", tr("Search with inline query argument"),
        [this](const QStringList &args) { search(args.join(" ")); });
    m_command_manager->reg("search_cancel",
                           tr("Cancel current search and clear highlights"),
                           [this](const QStringList &) { searchCancel(); });
    m_command_manager->reg("search_below",
                           tr("Search document from the current page forward"),
                           [this](const QStringList &args)
    { SearchDirectional(args, DocumentView::SearchScope::Below); });
    m_command_manager->reg("search_above",
                           tr("Search document from the current page backward"),
                           [this](const QStringList &args)
    { SearchDirectional(args, DocumentView::SearchScope::Above); });

    // Layout modes
    m_command_manager->reg("layout_single", tr("Single page layout"),
                           [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::SINGLE); });
    m_command_manager->reg("layout_horizontal",
                           tr("Horizontal (left to right) layout"),
                           [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::HORIZONTAL); });
    m_command_manager->reg("layout_vertical",
                           tr("Vertical (top to bottom) layout"),
                           [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::VERTICAL); });
    m_command_manager->reg("layout_book", tr("Book (two page spread) layout"),
                           [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::BOOK); });
    m_command_manager->reg("layout_grid",
                           tr("Grid layout (pages in rows of several columns)"),
                           [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::GRID); });
    m_command_manager->reg(
        "grid_columns",
        tr("Set the number of pages per row of the grid layout (and show it)"),
        [this](const QStringList &args) { SetGridColumns(args); });

    // Miscellaneous
    m_command_manager->reg("preview", tr("Show the preview window"),
                           [this](const QStringList &)
    {
        if (m_preview_overlay)
        {
            m_preview_overlay->show();
        }
    });

    m_command_manager->reg("export_highlights",
                           tr("Export text highlight annotations"),
                           [this](const QStringList &)
    {
        if (!m_doc)
            return;
        auto *model = m_doc->model();
        if (!model || !model->supports_annotations())
            return;

        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export Highlights"), {},
            tr("JSON files (*.json);;All files (*)"));
        if (path.isEmpty())
            return;

        if (model->exportTextHighlights(path))
            m_message_bar->showMessage(
                tr("Highlights exported to %1").arg(path), 4.0f);
        else
            m_message_bar->showMessage(tr("Export highlights failed"), 6.0f);
    });

    m_command_manager->reg("open_config", tr("Open configuration file"),
                           [this](const QStringList &) { OpenConfigFile(); });

    m_command_manager->reg("set_dpr", tr("Set device pixel ratio"),
                           [this](const QStringList &) { SetDPR(); });
    m_command_manager->reg(
        "open_containing_folder", tr("Open folder containing current file"),
        [this](const QStringList &) { OpenContainingFolder(); });
    m_command_manager->reg("undo", tr("Undo last action"),
                           [this](const QStringList &) { Undo(); });
    m_command_manager->reg("redo", tr("Redo last undone action"),
                           [this](const QStringList &) { Redo(); });
    m_command_manager->reg(
        "highlight_selection", tr("Highlight current text selection"),
        [this](const QStringList &) { TextHighlightCurrentSelection(); });
    m_command_manager->reg(
        "underline_selection", tr("Underline current text selection"),
        [this](const QStringList &) { TextUnderlineCurrentSelection(); });
    m_command_manager->reg(
        "high_contrast", tr("Toggle high-contrast tone stretch"),
        [this](const QStringList &) { ToggleHighContrast(); });
    m_command_manager->reg("invert_color",
                           tr("Toggle inverted colour rendering"),
                           [this](const QStringList &) { InvertColor(); });
    m_command_manager->reg(
        "reshow_jump_marker", tr("Re-show the last jump marker"),
        [this](const QStringList &) { Reshow_jump_marker(); });
    m_command_manager->reg(
        "reopen_last_closed_file", tr("Reopen last closed file"),
        [this](const QStringList &) { Reopen_last_closed_file(); });
    m_command_manager->reg("copy_page_image", tr("Copy current page as image"),
                           [this](const QStringList &) { Copy_page_image(); });
    m_command_manager->reg(
        "export_pages",
        tr("Export pages as images (PNG by default), PDF or other formats; "
           "asks for the pages and where to save"),
        [this](const QStringList &args) { ExportPages(args); });
#ifndef NDEBUG
    m_command_manager->reg("debug_command", tr("Run debug command"),
                           [this](const QStringList &) { debug_command(); });
#endif

    // Help / About
    m_command_manager->reg("show_startup_widget", tr("Show startup screen"),
                           [this](const QStringList &)
    { showStartupWidget(); });
    m_command_manager->reg("show_tutorial_file", tr("Open tutorial document"),
                           [this](const QStringList &) { showTutorialFile(); });
    m_command_manager->reg("show_about", tr("Show about dialog"),
                           [this](const QStringList &) { ShowAbout(); });
    m_command_manager->reg(
        "check_for_updates", tr("Check whether a newer release is available"),
        [this](const QStringList &) { checkForUpdates(true); });
    m_command_manager->reg("whats_new", tr("Show what changed in this version"),
                           [this](const QStringList &) { showWhatsNew(); });
    m_command_manager->reg("donate", tr("Show donate / support dialog"),
                           [this](const QStringList &) { ShowDonate(); });
}
#ifndef NDEBUG

void
Lektra::debug_command() noexcept
{
    m_message_bar->showMessage("TEST MESSAGE");
}
#endif
