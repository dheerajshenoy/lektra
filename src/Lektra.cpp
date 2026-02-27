#include "Lektra.hpp"

#include "AboutDialog.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
#include "EditLastPagesWidget.hpp"
#include "GraphicsView.hpp"
#include "SaveSessionDialog.hpp"
#include "SearchBar.hpp"
#include "StartupWidget.hpp"
#include "TabBar.hpp"
#include "utils.hpp"

#include <QColorDialog>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeData>
#include <QProcess>
#include <QSplitter>
#include <QStyleHints>
#include <QWindow>
#include <algorithm>
#include <qobject.h>
#include <variant>

namespace
{

static inline void
set_title_format_if_present(toml::node_view<toml::node> n,
                            QString &title_format)
{
    if (auto v = n.value<std::string>())
    {
        QString window_title = QString::fromStdString(*v);
        window_title.replace("{}", "%1");
        title_format = window_title;
    }
}

template <typename T>
static inline void
set(toml::node_view<toml::node> node, T &target)
{
    if (auto v = node.value<T>())
        target = *v;
}

static inline void
set(toml::node_view<toml::node> n, QString &dst)
{
    if (auto v = n.value<std::string>())
        dst = QString::fromStdString(*v);
}

static inline void
set_color(toml::node_view<toml::node> n, uint32_t &dst)
{
    if (auto s = n.value<std::string>())
    {
        uint32_t tmp = dst;
        if (parseHexColor(*s, tmp))
            dst = tmp;
    }
}

} // namespace

// Constructs the `Lektra` class
Lektra::Lektra() noexcept
{
    setAttribute(Qt::WA_NativeWindow,
                 true); // This is necessary for DPI updates
    setAcceptDrops(true);
}

Lektra::Lektra(const QString &sessionName,
               const QJsonArray &sessionArray) noexcept
{
    setAttribute(Qt::WA_NativeWindow); // This is necessary for DPI updates
    setAcceptDrops(true);
    construct();
    openSessionFromArray(sessionArray);
    setSessionName(sessionName);
    m_statusbar->setSessionName(sessionName);
}

// On-demand construction of `Lektra` (for use with argparse)
void
Lektra::construct() noexcept
{
    initCommands();
    initConfig();
    initGui();
    if (m_load_default_keybinding)
        initDefaultKeybinds();
    warnShortcutConflicts();
    initDB();
    trimRecentFilesDatabase();
    populateRecentFiles();
    initConnections();
    updateUiEnabledState();
    setMinimumSize(200, 150);
    this->show();
    resize(m_config.window.initial_size[0], m_config.window.initial_size[1]);
    installEventFilter(this);
}

// Initialize the menubar related stuff
void
Lektra::initMenubar() noexcept
{
    // --- File Menu ---
    QMenu *fileMenu = m_menuBar->addMenu("&File");

    fileMenu->addAction(
        QString("Open File\t%1").arg(m_config.shortcuts["file_open_tab"]), this,
        [&]() { OpenFileInNewTab(); });

    fileMenu->addAction(QString("Open File In VSplit\t%1")
                            .arg(m_config.shortcuts["file_open_vsplit"]),
                        this, [&]() { OpenFileVSplit(); });

    fileMenu->addAction(QString("Open File In HSplit\t%1")
                            .arg(m_config.shortcuts["file_open_hsplit"]),
                        this, [&]() { OpenFileHSplit(); });

    m_recentFilesMenu = fileMenu->addMenu("Recent Files");

    m_actionFileProperties
        = fileMenu->addAction(QString("File Properties\t%1")
                                  .arg(m_config.shortcuts["file_properties"]),
                              this, &Lektra::FileProperties);

    m_actionOpenContainingFolder = fileMenu->addAction(
        QString("Open Containing Folder\t%1")
            .arg(m_config.shortcuts["open_containing_folder"]),
        this, &Lektra::OpenContainingFolder);
    m_actionOpenContainingFolder->setEnabled(false);

    m_actionSaveFile = fileMenu->addAction(
        QString("Save File\t%1").arg(m_config.shortcuts["file_save"]), this,
        &Lektra::SaveFile);

    m_actionSaveAsFile = fileMenu->addAction(
        QString("Save As File\t%1").arg(m_config.shortcuts["file_save_as"]),
        this, &Lektra::SaveAsFile);

    QMenu *sessionMenu = fileMenu->addMenu("Session");

    m_actionSessionSave = sessionMenu->addAction(
        QString("Save\t%1").arg(m_config.shortcuts["session_save"]), this,
        [&]() { SaveSession(); });
    m_actionSessionSaveAs = sessionMenu->addAction(
        QString("Save As\t%1").arg(m_config.shortcuts["session_save_as"]), this,
        [&]() { SaveAsSession(); });
    m_actionSessionLoad = sessionMenu->addAction(
        QString("Load\t%1").arg(m_config.shortcuts["session_load"]), this,
        [&]() { LoadSession(); });

    m_actionSessionSaveAs->setEnabled(false);

    m_actionCloseFile = fileMenu->addAction(
        QString("Close File\t%1").arg(m_config.shortcuts["file_close"]), this,
        [this]() { Tab_close(); });

    fileMenu->addSeparator();
    fileMenu->addAction("Quit", this, &QMainWindow::close);

    QMenu *editMenu = m_menuBar->addMenu("&Edit");
    m_actionUndo    = editMenu->addAction(
        QString("Undo\t%1").arg(m_config.shortcuts["undo"]), this,
        &Lektra::Undo);
    m_actionRedo = editMenu->addAction(
        QString("Redo\t%1").arg(m_config.shortcuts["redo"]), this,
        &Lektra::Redo);
    m_actionUndo->setEnabled(false);
    m_actionRedo->setEnabled(false);
    editMenu->addAction(
        QString("Last Pages\t%1").arg(m_config.shortcuts["edit_last_pages"]),
        this, &Lektra::editLastPages);

    // --- View Menu ---
    m_viewMenu         = m_menuBar->addMenu("&View");
    m_actionFullscreen = m_viewMenu->addAction(
        QString("Fullscreen\t%1").arg(m_config.shortcuts["fullscreen"]), this,
        &Lektra::ToggleFullscreen);
    m_actionFullscreen->setCheckable(true);
    m_actionFullscreen->setChecked(m_config.window.fullscreen);

    m_actionZoomIn = m_viewMenu->addAction(
        QString("Zoom In\t%1").arg(m_config.shortcuts["zoom_in"]), this,
        &Lektra::ZoomIn);
    m_actionZoomOut = m_viewMenu->addAction(
        QString("Zoom Out\t%1").arg(m_config.shortcuts["zoom_out"]), this,
        &Lektra::ZoomOut);

    m_viewMenu->addSeparator();

    m_fitMenu = m_viewMenu->addMenu("Fit");

    m_actionFitWidth = m_fitMenu->addAction(
        QString("Width\t%1").arg(m_config.shortcuts["fit_width"]), this,
        &Lektra::Fit_width);

    m_actionFitHeight = m_fitMenu->addAction(
        QString("Height\t%1").arg(m_config.shortcuts["fit_height"]), this,
        &Lektra::Fit_height);

    m_actionFitWindow = m_fitMenu->addAction(
        QString("Page\t%1").arg(m_config.shortcuts["fit_page"]), this,
        &Lektra::Fit_page);

    m_fitMenu->addSeparator();

    // Auto Resize toggle (independent)
    m_actionAutoresize = m_viewMenu->addAction(
        QString("Auto Fit\t%1").arg(m_config.shortcuts["fit_auto"]), this,
        &Lektra::ToggleAutoResize);
    m_actionAutoresize->setCheckable(true);
    m_actionAutoresize->setChecked(
        m_config.layout.auto_resize); // default on or off

    // --- Layout Menu ---

    m_viewMenu->addSeparator();
    m_layoutMenu                    = m_viewMenu->addMenu("Layout");
    QActionGroup *layoutActionGroup = new QActionGroup(this);
    layoutActionGroup->setExclusive(true);

    m_actionLayoutSingle = m_layoutMenu->addAction(
        QString("Single Page\t%1").arg(m_config.shortcuts["layout_single"]),
        this, [&]() { SetLayoutMode(DocumentView::LayoutMode::SINGLE); });

    m_actionLayoutLeftToRight = m_layoutMenu->addAction(
        QString("Left to Right Page\t%1")
            .arg(m_config.shortcuts["layout_left_to_right"]),
        this,
        [&]() { SetLayoutMode(DocumentView::LayoutMode::LEFT_TO_RIGHT); });

    m_actionLayoutTopToBottom = m_layoutMenu->addAction(
        QString("Top to Bottom Page\t%1")
            .arg(m_config.shortcuts["layout_top_to_bottom"]),
        this,
        [&]() { SetLayoutMode(DocumentView::LayoutMode::TOP_TO_BOTTOM); });

    m_actionLayoutBook = m_layoutMenu->addAction(
        QString("Book\t%1").arg(m_config.shortcuts["layout_book"]), this,
        [&]() { SetLayoutMode(DocumentView::LayoutMode::BOOK); });

    layoutActionGroup->addAction(m_actionLayoutSingle);
    layoutActionGroup->addAction(m_actionLayoutLeftToRight);
    layoutActionGroup->addAction(m_actionLayoutTopToBottom);
    layoutActionGroup->addAction(m_actionLayoutBook);

    m_actionLayoutSingle->setCheckable(true);
    m_actionLayoutLeftToRight->setCheckable(true);
    m_actionLayoutTopToBottom->setCheckable(true);
    m_actionLayoutBook->setCheckable(true);
    m_actionLayoutSingle->setChecked(m_config.layout.mode
                                     == DocumentView::LayoutMode::SINGLE);

    m_actionLayoutLeftToRight->setChecked(
        m_config.layout.mode == DocumentView::LayoutMode::LEFT_TO_RIGHT);
    m_actionLayoutTopToBottom->setChecked(
        m_config.layout.mode == DocumentView::LayoutMode::TOP_TO_BOTTOM);
    m_actionLayoutBook->setChecked(m_config.layout.mode
                                   == DocumentView::LayoutMode::BOOK);

    // --- Toggle Menu ---

    m_viewMenu->addSeparator();
    m_toggleMenu = m_viewMenu->addMenu("Show/Hide");

#ifdef ENABLE_LLM_SUPPORT
    m_actionToggleLLMWidget = m_toggleMenu->addAction(
        QString("LLM Widget\t%1").arg(m_config.shortcuts["llm_widget"]), this,
        &Lektra::ToggleLLMWidget);
    m_actionToggleLLMWidget->setCheckable(true);
    m_actionToggleLLMWidget->setChecked(m_config.llm_widget.visible);
#endif

    m_actionCommandPicker = m_toggleMenu->addAction(
        QString("Command Picker\t%1").arg(m_config.shortcuts["command_picker"]),
        this, &Lektra::Show_command_picker);

    m_actionToggleOutline = m_toggleMenu->addAction(
        QString("Outline\t%1").arg(m_config.shortcuts["picker_outline"]), this,
        &Lektra::Show_outline);
    m_actionToggleOutline->setCheckable(true);
    m_actionToggleOutline->setChecked(m_outline_picker
                                      && !m_outline_picker->isHidden());

    m_actionToggleHighlightAnnotSearch = m_toggleMenu->addAction(
        QString("Highlight Annotation Search\t%1")
            .arg(m_config.shortcuts["picker_highlight_search"]),
        this, &Lektra::Show_highlight_search);
    m_actionToggleHighlightAnnotSearch->setCheckable(true);
    m_actionToggleHighlightAnnotSearch->setChecked(
        m_highlight_search_picker && !m_highlight_search_picker->isHidden());

    m_actionToggleMenubar = m_toggleMenu->addAction(
        QString("Menubar\t%1").arg(m_config.shortcuts["menubar"]), this,
        &Lektra::ToggleMenubar);
    m_actionToggleMenubar->setCheckable(true);
    m_actionToggleMenubar->setChecked(!m_menuBar->isHidden());

    m_actionToggleTabBar = m_toggleMenu->addAction(
        QString("Tabs\t%1").arg(m_config.shortcuts["tabs"]), this,
        &Lektra::ToggleTabBar);
    m_actionToggleTabBar->setCheckable(true);
    m_actionToggleTabBar->setChecked(!m_tab_widget->tabBar()->isHidden());

    m_actionTogglePanel = m_toggleMenu->addAction(
        QString("Statusbar\t%1").arg(m_config.shortcuts["statusbar"]), this,
        &Lektra::TogglePanel);
    m_actionTogglePanel->setCheckable(true);
    m_actionTogglePanel->setChecked(!m_statusbar->isHidden());

    m_actionInvertColor = m_viewMenu->addAction(
        QString("Invert Color\t%1").arg(m_config.shortcuts["invert_color"]),
        this, &Lektra::InvertColor);
    m_actionInvertColor->setCheckable(true);
    m_actionInvertColor->setChecked(m_config.behavior.invert_mode);

    // --- Tools Menu ---

    QMenu *toolsMenu = m_menuBar->addMenu("Tools");

    m_modeMenu = toolsMenu->addMenu("Mode");

    QActionGroup *modeActionGroup = new QActionGroup(this);
    modeActionGroup->setExclusive(true);

    m_actionRegionSelect = m_modeMenu->addAction(
        QString("Region Selection\t%1")
            .arg(m_config.shortcuts["selection_mode_region"]),
        this, &Lektra::ToggleRegionSelect);
    m_actionRegionSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionRegionSelect);

    m_actionTextSelect = m_modeMenu->addAction(
        QString("Text Selection\t%1")
            .arg(m_config.shortcuts["selection_mode_text"]),
        this, &Lektra::ToggleTextSelection);
    m_actionTextSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionTextSelect);

    m_actionTextHighlight = m_modeMenu->addAction(
        QString("Text Highlight\t%1")
            .arg(m_config.shortcuts["annot_highlight_mode"]),
        this, &Lektra::ToggleTextHighlight);
    m_actionTextHighlight->setCheckable(true);
    modeActionGroup->addAction(m_actionTextHighlight);

    m_actionAnnotRect
        = m_modeMenu->addAction(QString("Annotate Rectangle\t%1")
                                    .arg(m_config.shortcuts["annot_rect_mode"]),
                                this, &Lektra::ToggleAnnotRect);
    m_actionAnnotRect->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotRect);

    m_actionAnnotEdit
        = m_modeMenu->addAction(QString("Edit Annotations\t%1")
                                    .arg(m_config.shortcuts["annot_edit_mode"]),
                                this, &Lektra::ToggleAnnotSelect);
    m_actionAnnotEdit->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotEdit);

    m_actionAnnotPopup = m_modeMenu->addAction(
        QString("Annotate Popup\t%1")
            .arg(m_config.shortcuts["annot_popup_mode"]),
        this, &Lektra::ToggleAnnotPopup);
    m_actionAnnotPopup->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotPopup);

    switch (m_config.behavior.initial_mode)
    {
        case GraphicsView::Mode::RegionSelection:
            m_actionRegionSelect->setChecked(true);
            break;
        case GraphicsView::Mode::TextSelection:
            m_actionTextSelect->setChecked(true);
            break;
        case GraphicsView::Mode::TextHighlight:
            m_actionTextHighlight->setChecked(true);
            break;
        case GraphicsView::Mode::AnnotSelect:
            m_actionAnnotEdit->setChecked(true);
            break;
        case GraphicsView::Mode::AnnotRect:
            m_actionAnnotRect->setChecked(true);
            break;
        case GraphicsView::Mode::AnnotPopup:
            m_actionAnnotPopup->setChecked(true);
            break;
        default:
            break;
    }

    m_actionEncrypt = toolsMenu->addAction(
        QString("Encrypt Document\t%1").arg(m_config.shortcuts["file_encrypt"]),
        this, &Lektra::EncryptDocument);
    m_actionEncrypt->setEnabled(false);

    m_actionDecrypt = toolsMenu->addAction(
        QString("Decrypt Document\t%1").arg(m_config.shortcuts["file_decrypt"]),
        this, &Lektra::DecryptDocument);
    m_actionDecrypt->setEnabled(false);

    // --- Navigation Menu ---
    m_navMenu = m_menuBar->addMenu("&Navigation");

    m_navMenu->addAction(
        QString("StartPage\t%1").arg(m_config.shortcuts["show_startup_widget"]),
        this, &Lektra::showStartupWidget);

    m_actionGotoPage = m_navMenu->addAction(
        QString("Goto Page\t%1").arg(m_config.shortcuts["page_goto"]), this,
        &Lektra::Goto_page);

    m_actionFirstPage = m_navMenu->addAction(
        QString("First Page\t%1").arg(m_config.shortcuts["page_first"]), this,
        &Lektra::FirstPage);

    m_actionPrevPage = m_navMenu->addAction(
        QString("Previous Page\t%1").arg(m_config.shortcuts["page_prev"]), this,
        &Lektra::PrevPage);

    m_actionNextPage = m_navMenu->addAction(
        QString("Next Page\t%1").arg(m_config.shortcuts["page_next"]), this,
        &Lektra::NextPage);
    m_actionLastPage = m_navMenu->addAction(
        QString("Last Page\t%1").arg(m_config.shortcuts["page_last"]), this,
        &Lektra::LastPage);

    m_actionPrevLocation
        = m_navMenu->addAction(QString("Previous Location\t%1")
                                   .arg(m_config.shortcuts["location_prev"]),
                               this, &Lektra::GoBackHistory);
    m_actionNextLocation = m_navMenu->addAction(
        QString("Next Location\t%1").arg(m_config.shortcuts["location_next"]),
        this, &Lektra::GoForwardHistory);

    // QMenu *markMenu = m_navMenu->addMenu("Marks");

    // m_actionSetMark = markMenu->addAction(
    //     QString("Set Mar\t%1").arg(m_config.shortcuts["set_mark"]), this,
    //     &Lektra::SetMark);

    /* Help Menu */
    QMenu *helpMenu = m_menuBar->addMenu("&Help");
    m_actionAbout   = helpMenu->addAction(
        QString("About\t%1").arg(m_config.shortcuts["show_about"]), this,
        &Lektra::ShowAbout);

    m_actionShowTutorialFile = helpMenu->addAction(
        QString("Open Tutorial File\t%1")
            .arg(m_config.shortcuts["show_tutorial_file"]),
        this, &Lektra::showTutorialFile);
}

// Initialize the recent files store
void
Lektra::initDB() noexcept
{
    m_recent_files_path = m_config_dir.filePath("last_pages.json");
    m_recent_files_store.setFilePath(m_recent_files_path);
    if (!m_recent_files_store.load())
        qWarning() << "Failed to load recent files store";
}

// Initialize the config related stuff
void
Lektra::initConfig() noexcept
{
    m_config_dir = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));

    // If config file path is not set, use the default one
    if (m_config_file_path.isEmpty())
        m_config_file_path = m_config_dir.filePath("config.toml");

    auto primaryScreen                      = QGuiApplication::primaryScreen();
    m_screen_dpr_map[primaryScreen->name()] = primaryScreen->devicePixelRatio();

    m_session_dir = QDir(m_config_dir.filePath("sessions"));

    if (!QFile::exists(m_config_file_path))
        return;

    toml::table toml;

    try
    {
        toml = toml::parse_file(m_config_file_path.toStdString());
    }
    catch (std::exception &e)
    {
        QMessageBox::critical(
            this, "Error in configuration file",
            QString("There are one or more error(s) in your config "
                    "file:\n%1\n\nLoading default config.")
                .arg(e.what()));
        return;
    }

    // Portals
    if (auto portal = toml["portal"])
    {
        set(portal["enabled"], m_config.portal.enabled);
        set(portal["border_width"], m_config.portal.border_width);
        set(portal["dim_inactive"], m_config.portal.dim_inactive);
    }

    // Scripts
    if (auto scripts = toml["scripts"].as_table())
    {
        for (auto &&[key, value] : *scripts)
        {
            if (value.is_string())
            {
                const QString scriptName
                    = QString::fromStdString(std::string(key.str()));
                const QString scriptPath
                    = QString::fromStdString(value.as_string()->get());
            }
            else
            {
                // Log a warning if the user put a number or boolean in the
                // scripts table
                std::cerr << "Warning: Script '" << key.str()
                          << "' must be a string path." << std::endl;
            }
        }
    }

    // Tabs
    if (auto tabs = toml["tabs"])
    {
        set(tabs["visible"], m_config.tabs.visible);
        set(tabs["auto_hide"], m_config.tabs.auto_hide);
        set(tabs["closable"], m_config.tabs.closable);
        set(tabs["movable"], m_config.tabs.movable);
        if (auto str = tabs["elide_mode"])
        {
            Qt::TextElideMode mode;
            if (str == "left")
                mode = Qt::ElideLeft;
            else if (str == "right")
                mode = Qt::ElideRight;
            else if (str == "middle")
                mode = Qt::ElideMiddle;
            else
                mode = Qt::ElideNone;
            m_config.tabs.elide_mode = mode;
        }

        if (auto str = tabs["location"])
        {
            QTabWidget::TabPosition location;

            if (str == "left")
                location = QTabWidget::West;
            else if (str == "right")
                location = QTabWidget::East;
            else if (str == "bottom")
                location = QTabWidget::South;
            else
                location = QTabWidget::North;

            m_config.tabs.location = location;
        }
        set(tabs["full_path"], m_config.tabs.full_path);
        set(tabs["lazy_load"], m_config.tabs.lazy_load);
    }

    // Window
    if (auto window = toml["window"])
    {
        set(window["startup_tab"], m_config.window.startup_tab);
        set(window["menubar"], m_config.window.menubar);
        set(window["fullscreen"], m_config.window.fullscreen);

        if (window["initial_size"].is_table())
        {
            int width{600}, height{400};

            const auto &size_table = *window["initial_size"].as_table();

            if (auto toml_width = size_table["width"].value<int>())
                width = *toml_width;
            if (auto toml_height = size_table["height"].value<int>())
                height = *toml_height;

            if (width > 0 && height > 0)
            {
                m_config.window.initial_size = {width, height};
            }
        }

        if (m_config.window.fullscreen)
            this->showFullScreen();

        // Only override title format if key exists
        set_title_format_if_present(window["window_title"],
                                    m_config.window.title_format);
    }

    // Statusbar
    if (auto statusbar = toml["statusbar"])
    {
        set(statusbar["visible"], m_config.statusbar.visible);

        if (auto padding_array = statusbar["padding"].as_array();
            padding_array && padding_array->size() >= 4)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (auto v
                    = padding_array->get(static_cast<size_t>(i))->value<int>())
                    m_config.statusbar.padding[i] = *v;
            }
        }

        set(statusbar["show_progress"], m_config.statusbar.show_progress);
        set(statusbar["file_name_only"], m_config.statusbar.file_name_only);
        set(statusbar["show_file_info"], m_config.statusbar.show_file_info);
        set(statusbar["show_page_number"], m_config.statusbar.show_page_number);
        set(statusbar["show_mode"], m_config.statusbar.show_mode);
        set(statusbar["show_session_name"],
            m_config.statusbar.show_session_name);
    }

    // Layout
    if (auto layout = toml["layout"])
    {
        if (auto str = layout["mode"])
        {
            DocumentView::LayoutMode mode;

            if (str == "top_to_bottom")
                mode = DocumentView::LayoutMode::TOP_TO_BOTTOM;
            else if (str == "single")
                mode = DocumentView::LayoutMode::SINGLE;
            else if (str == "left_to_right")
                mode = DocumentView::LayoutMode::LEFT_TO_RIGHT;
            else if (str == "book")
                mode = DocumentView::LayoutMode::BOOK;
            else
                mode = DocumentView::LayoutMode::TOP_TO_BOTTOM;

            m_config.layout.mode = mode;
        }
        if (auto str = layout["initial_fit"])
        {
            DocumentView::FitMode initial_fit;

            if (str == "width")
            {
                initial_fit = DocumentView::FitMode::Width;
            }
            else if (str == "height")
            {
                initial_fit = DocumentView::FitMode::Height;
            }
            else if (str == "window")
            {
                initial_fit = DocumentView::FitMode::Window;
            }
            else
            {
                initial_fit = DocumentView::FitMode::Width;
            }

            m_config.layout.initial_fit = initial_fit;
        }
        set(layout["auto_resize"], m_config.layout.auto_resize);
        set(layout["spacing"], m_config.layout.spacing);
    }

    // Zoom
    if (auto zoom = toml["zoom"])
    {
        set(zoom["level"], m_config.zoom.level);
        set(zoom["factor"], m_config.zoom.factor);
    }

    // Selection
    if (auto selection = toml["selection"])
    {
        set(selection["drag_threshold"], m_config.selection.drag_threshold);
        set(selection["copy_on_select"], m_config.selection.copy_on_select);
    }
    /* scrollbars */
    if (auto scrollbars = toml["scrollbars"])
    {
        set(scrollbars["vertical"], m_config.scrollbars.vertical);
        set(scrollbars["horizontal"], m_config.scrollbars.horizontal);
        set(scrollbars["search_hits"], m_config.scrollbars.search_hits);
        set(scrollbars["auto_hide"], m_config.scrollbars.auto_hide);
        set(scrollbars["size"], m_config.scrollbars.size);
        set(scrollbars["hide_timeout"], m_config.scrollbars.hide_timeout);
    }

    // Command Palette
    if (auto command_palette = toml["command_palette"])
    {
        set(command_palette["description"],
            m_config.command_palette.description);
        set(command_palette["height"], m_config.command_palette.height);
        set(command_palette["width"], m_config.command_palette.width);
        set(command_palette["vscrollbar"], m_config.command_palette.vscrollbar);
        set(command_palette["show_grid"], m_config.command_palette.grid);
        set(command_palette["show_shortcuts"],
            m_config.command_palette.shortcuts);

        set(command_palette["placeholder_text"],
            m_config.command_palette.placeholder_text);
    }

    // Picker
    if (auto picker = toml["picker"])
    {
        set(picker["border"], m_config.picker.border);

        if (auto picker_shadow = picker["shadow"])
        {
            set(picker_shadow["enabled"], m_config.picker.shadow.enabled);
            set(picker_shadow["blur_radius"],
                m_config.picker.shadow.blur_radius);
            set(picker_shadow["offset_x"], m_config.picker.shadow.offset_x);
            set(picker_shadow["offset_y"], m_config.picker.shadow.offset_y);
            set(picker_shadow["opacity"], m_config.picker.shadow.opacity);
        }

        // Picker.Keys
        if (auto picker_keys = picker["keys"])
        {
            // Member: m_pickers
            if (picker_keys.is_table())
            {
                const auto &keys = *picker_keys.as_table();
                const auto get
                    = [&](std::string_view field, QKeyCombination fallback)
                {
                    const auto *node = keys.get(field);
                    if (!node || !node->is_string())
                        return fallback;
                    const auto seq = QKeySequence::fromString(
                        QString::fromStdString(
                            std::string(node->as_string()->get())),
                        QKeySequence::PortableText);
                    return seq.isEmpty() ? fallback : seq[0];
                };

                m_picker_keybinds = Picker::Keybindings{
                    .moveDown = get("down", Picker::Keybindings{}.moveDown),
                    .pageDown
                    = get("page_down", Picker::Keybindings{}.pageDown),
                    .moveUp  = get("up", Picker::Keybindings{}.moveUp),
                    .pageUp  = get("page_up", Picker::Keybindings{}.pageUp),
                    .accept  = get("accept", Picker::Keybindings{}.accept),
                    .dismiss = get("dismiss", Picker::Keybindings{}.dismiss),
                };
            }
        }
    }

    // Markers
    if (auto markers = toml["markers"])
    {
        set(markers["jump_marker"], m_config.markers.jump_marker);
    }

    // Links
    if (auto links = toml["links"])
    {
        set(links["boundary"], m_config.links.boundary);
        set(links["detect_urls"], m_config.links.detect_urls);
        set(links["url_regex"], m_config.links.url_regex);
    }

    // Link Hints
    if (auto link_hints = toml["link_hints"])
    {
        set(link_hints["size"], m_config.link_hints.size);
    }

    // Outline
    if (auto outline = toml["outline"])
    {
        set(outline["indent_width"], m_config.outline.indent_width);
        set(outline["show_page_numbers"], m_config.outline.show_page_numbers);
    }

    // Highlight Search
    if (auto highlight_search = toml["highlight_search"])
    {
        // TODO
    }

#ifdef ENABLE_LLM_SUPPORT
    // LLM Widget
    if (auto llm_widget = toml["llm_widget"])
    {
        set(llm_widget["panel_position"], m_config.llm_widget.panel_position);
        set(llm_widget["panel_width"], m_config.llm_widget.panel_width);
        set(llm_widget["visible"], m_config.llm_widget.visible);
    }

    // LLM
    if (auto llm = toml["llm"])
    {
        set(llm["provider"], m_config.llm.provider);
        set(llm["model"], m_config.llm.model);
        set(llm["max_tokens"], m_config.llm.max_tokens);
    }
#endif

    // Colors
    if (auto colors = toml["colors"])
    {
        set_color(colors["accent"], m_config.colors.accent);
        set_color(colors["background"], m_config.colors.background);
        set_color(colors["search_match"], m_config.colors.search_match);
        set_color(colors["search_index"], m_config.colors.search_index);
        set_color(colors["link_hint_bg"], m_config.colors.link_hint_bg);
        set_color(colors["link_hint_fg"], m_config.colors.link_hint_fg);
        set_color(colors["selection"], m_config.colors.selection);
        set_color(colors["highlight"], m_config.colors.highlight);
        set_color(colors["jump_marker"], m_config.colors.jump_marker);
        set_color(colors["annot_rect"], m_config.colors.annot_rect);
        set_color(colors["annot_popup"], m_config.colors.annot_popup);
        set_color(colors["page_background"], m_config.colors.page_background);
        set_color(colors["page_foreground"], m_config.colors.page_foreground);
        set_color(colors["portal_border"], m_config.colors.portal_border);
    }

    // Rendering
    if (auto rendering = toml["rendering"])
    {
        set(rendering["antialiasing"], m_config.rendering.antialiasing);
        set(rendering["text_antialiasing"],
            m_config.rendering.text_antialiasing);
        set(rendering["smooth_pixmap_transform"],
            m_config.rendering.smooth_pixmap_transform);
        set(rendering["antialiasing_bits"],
            m_config.rendering.antialiasing_bits);

        // If DPR is specified in config, use that (can be scalar or map)
        if (rendering["dpr"])
        {
            if (rendering["dpr"].is_value())
            {
                if (auto v = rendering["dpr"].value<float>())
                {
                    m_config.rendering.dpr = *v;
                    m_screen_dpr_map[QGuiApplication::primaryScreen()->name()]
                        = *v;
                }
            }
            else if (rendering["dpr"].is_table())
            {
                // Only build a map if table exists; else leave default
                auto dpr_table = rendering["dpr"];
                if (auto t = dpr_table.as_table())
                {
                    // Start from current map (if you want table to
                    // "add/override") or clear it (if you want table to
                    // "replace"). Here: replace, because that's what your
                    // old code effectively did.
                    m_screen_dpr_map.clear();
                    for (auto &[screen_name, value] : *t)
                    {
                        if (auto v = value.value<float>())
                        {
                            const QString screen_str = QString::fromStdString(
                                std::string(screen_name.str()));

                            for (QScreen *screen : QApplication::screens())
                            {
                                if (screen->name() == screen_str)
                                {
                                    m_screen_dpr_map[screen->name()] = *v;
                                    break;
                                }
                            }
                        }
                    }

                    m_config.rendering.dpr = m_screen_dpr_map;
                }
            }
        }
        else
        {
            m_screen_dpr_map[QGuiApplication::primaryScreen()->name()] = 1.0f;
        }
    }

    // Split
    if (auto split = toml["split"])
    {
        set(split["mouse_follows_focus"], m_config.split.mouse_follows_focus);
        set(split["focus_follows_mouse"], m_config.split.focus_follows_mouse);
        set(split["dim_inactive"], m_config.split.dim_inactive);
        set(split["dim_inactive_opacity"], m_config.split.dim_inactive_opacity);
    }

    // Behavior

    if (auto behavior = toml["behavior"])
    {
#ifdef HAS_SYNCTEX
        set(behavior["synctex_editor_command"],
            m_config.behavior.synctex_editor_command);
#endif

        set(behavior["preload_pages"], m_config.behavior.preload_pages);
        set(behavior["confirm_on_quit"], m_config.behavior.confirm_on_quit);
        set(behavior["undo_limit"], m_config.behavior.undo_limit);
        set(behavior["remember_last_visited"],
            m_config.behavior.remember_last_visited);
        set(behavior["always_open_in_new_window"],
            m_config.behavior.always_open_in_new_window);
        set(behavior["page_history"], m_config.behavior.page_history_limit);
        set(behavior["invert_mode"], m_config.behavior.invert_mode);
        set(behavior["auto_reload"], m_config.behavior.auto_reload);
        set(behavior["recent_files"], m_config.behavior.recent_files);
        set(behavior["num_recent_files"], m_config.behavior.num_recent_files);
        set(behavior["cache_pages"], m_config.behavior.cache_pages);
    }

    if (auto keys = toml["keybindings"])
    {
        m_load_default_keybinding = false;

        for (auto &[action, value] : *keys.as_table())
        {
            if (value.is_value())
                setupKeybinding(
                    QString::fromStdString(std::string(action.str())),
                    QString::fromStdString(value.value_or<std::string>("")));
        }
    }

#ifndef NDEBUG
    qDebug() << "Finished reading config file:" << m_config_file_path;
#endif
}

// Initialize the keybindings related stuff
void
Lektra::initDefaultKeybinds() noexcept
{
    struct DefaultBinding
    {
        const char *action;
        const char *key;
    };

    constexpr DefaultBinding defaults[] = {
        {"scroll_left", "h"},
        {"scroll_down", "j"},
        {"scroll_up", "k"},
        {"scroll_right", "l"},
        {"page_next", "Shift+j"},
        {"page_prev", "Shift+k"},
        {"page_first", "g,g"},
        {"page_last", "Shift+g"},
        {"page_goto", "Ctrl+g"},
        {"search", "/"},
        {"search_next", "n"},
        {"search_prev", "Shift+n"},
        {"zoom_in", "="},
        {"zoom_out", "-"},
        {"zoom_reset", "0"},
        {"fit_width", "Ctrl+Shift+W"},
        {"fit_height", "Ctrl+Shift+H"},
        {"fit_page", "Ctrl+Shift+="},
        {"fit_auto", "Ctrl+Shift+R"},
        {"picker_outline", "t"},
        {"picker_highlight_search", "Alt+Shift+H"},
        {"location_prev", "Ctrl+o"},
        {"location_next", "Ctrl+i"},
        {"selection_mode_text", "1"},
        {"annot_highlight_mode", "2"},
        {"annot_rect_mode", "3"},
        {"selection_mode_region", "4"},
        {"annot_popup_mode", "5"},
        {"link_hint_visit", "f"},
        {"file_open_tab", "o"},
        {"file_save", "Ctrl+s"},
        {"visual_line_mode", "v"},
        {"undo", "u"},
        {"redo", "Ctrl+r"},
        {"invert_color", "i"},
        {"menubar", "Ctrl+Shift+m"},
        {"command_palette", ":"},
        {"rotate_clock", ">"},
        {"rotate_anticlock", "<"},
        {"tab_1", "Alt+1"},
        {"tab_2", "Alt+2"},
        {"tab_3", "Alt+3"},
        {"tab_4", "Alt+4"},
        {"tab_5", "Alt+5"},
        {"tab_6", "Alt+6"},
        {"tab_7", "Alt+7"},
        {"tab_8", "Alt+8"},
        {"tab_9", "Alt+9"},
    };

    for (const auto &binding : defaults)
    {
        setupKeybinding(QString::fromLatin1(binding.action),
                        QString::fromLatin1(binding.key));
    }
}

void
Lektra::warnShortcutConflicts() noexcept
{
    QHash<QString, QStringList> shortcutsByKey;
    for (auto it = m_config.shortcuts.constBegin();
         it != m_config.shortcuts.constEnd(); ++it)
    {
        const QString key = it.value().trimmed();
        if (key.isEmpty())
            continue;

        const QKeySequence seq(key);
        if (seq.isEmpty())
            continue;

        const QString normalized = seq.toString(QKeySequence::PortableText);
        if (normalized.isEmpty())
            continue;

        shortcutsByKey[normalized].append(it.key());
    }

    QStringList conflicts;
    for (auto it = shortcutsByKey.constBegin(); it != shortcutsByKey.constEnd();
         ++it)
    {
        if (it.value().size() < 2)
            continue;

        QString keyDisplay
            = QKeySequence(it.key()).toString(QKeySequence::NativeText);
        if (keyDisplay.isEmpty())
            keyDisplay = it.key();

        const QString actions = it.value().join(", ");
        conflicts.append(QString("%1 -> %2").arg(keyDisplay, actions));
    }

    if (conflicts.isEmpty())
        return;

    const int maxItems = 3;
    QString message;
    if (conflicts.size() <= maxItems)
    {
        message = QString("Shortcut conflict(s): %1").arg(conflicts.join("; "));
    }
    else
    {
        message = QString("Shortcut conflict(s): %1; and %2 more")
                      .arg(conflicts.mid(0, maxItems).join("; "))
                      .arg(conflicts.size() - maxItems);
    }

    qWarning() << message;
    m_message_bar->showMessage(message, 6.0f);
}

// Initialize the GUI related Stuff
void
Lektra::initGui() noexcept
{
    QWidget *widget = new QWidget(this);
    this->setCentralWidget(widget);
    m_layout = new QVBoxLayout(widget);
    m_layout->setContentsMargins(0, 0, 0, 0);
    widget->setLayout(m_layout);

    m_menuBar    = this->menuBar();
    m_tab_widget = new TabWidget(centralWidget());

    // Panel
    m_statusbar = new Statusbar(m_config.statusbar, this);
    m_statusbar->hidePageInfo(true);
    m_statusbar->setMode(GraphicsView::Mode::TextSelection);
    m_statusbar->setSessionName("");
    m_search_bar = new SearchBar(this);
    m_search_bar->setVisible(false);
    m_message_bar = new MessageBar(this);
    m_tab_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

#ifdef ENABLE_LLM_SUPPORT
    m_llm_widget = new LLMWidget(m_config, this);
    m_llm_widget->setVisible(m_config.llm_widget.visible);
    connect(m_llm_widget, &LLMWidget::actionRequested, this,
            [this](const QString &action, const QStringList &args)
    {
        if (action.isEmpty() || action == QStringLiteral("noop"))
            return;
        const auto it = m_actionMap.find(action);
        if (it == m_actionMap.end())
        {
            m_message_bar->showMessage(QStringLiteral("LLM: Unknown action"));
            return;
        }
        it.value()(args);
    });

    QSplitter *llm_splitter = new QSplitter(Qt::Horizontal, this);
    llm_splitter->addWidget(m_tab_widget);
    llm_splitter->addWidget(m_llm_widget);
    llm_splitter->setStretchFactor(0, 1);
    llm_splitter->setStretchFactor(1, 0);
    const int llmWidth = m_config.llm_widget.panel_width;
    llm_splitter->setSizes({this->width() - llmWidth, llmWidth});
    llm_splitter->setFrameShape(QFrame::NoFrame);
    llm_splitter->setFrameShadow(QFrame::Plain);
    llm_splitter->setHandleWidth(1);
    llm_splitter->setContentsMargins(0, 0, 0, 0);
    llm_splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_layout->addWidget(llm_splitter, 1);
#else
    m_layout->addWidget(m_tab_widget, 1);
#endif

    m_tab_widget->setTabsClosable(m_config.tabs.closable);
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

    m_marks_manager = new MarkManager(this);
}

// Updates the UI elements checking if valid
// file is open or not
void
Lektra::updateUiEnabledState() noexcept
{
    const bool hasOpenedFile = m_doc ? true : false;

    m_actionOpenContainingFolder->setEnabled(hasOpenedFile);
    m_actionZoomIn->setEnabled(hasOpenedFile);
    m_actionZoomOut->setEnabled(hasOpenedFile);
    m_actionGotoPage->setEnabled(hasOpenedFile);
    m_actionFirstPage->setEnabled(hasOpenedFile);
    m_actionPrevPage->setEnabled(hasOpenedFile);
    m_actionNextPage->setEnabled(hasOpenedFile);
    m_actionLastPage->setEnabled(hasOpenedFile);
    m_actionFileProperties->setEnabled(hasOpenedFile);
    m_actionCloseFile->setEnabled(hasOpenedFile);
    m_fitMenu->setEnabled(hasOpenedFile);
    // m_actionToggleOutline->setEnabled(hasOpenedFile);
    m_modeMenu->setEnabled(hasOpenedFile);
    m_actionInvertColor->setEnabled(hasOpenedFile);
    m_actionSaveFile->setEnabled(hasOpenedFile);
    m_actionSaveAsFile->setEnabled(hasOpenedFile);
    m_actionPrevLocation->setEnabled(hasOpenedFile);
    m_actionNextLocation->setEnabled(hasOpenedFile);
    m_actionEncrypt->setEnabled(hasOpenedFile);
    m_actionDecrypt->setEnabled(hasOpenedFile);
    m_actionSessionSave->setEnabled(hasOpenedFile);
    m_actionSessionSaveAs->setEnabled(!m_session_name.isEmpty());
    updateSelectionModeActions();
}

// Helper function to construct `QShortcut` Qt shortcut
// from the config file
void
Lektra::setupKeybinding(const QString &action, const QString &key) noexcept
{
    if (const Command *command = m_command_manager.find(action))
    {
#ifndef NDEBUG
        qDebug() << "Keybinding set:" << action << "->" << key;
#endif
        QShortcut *shortcut = new QShortcut(QKeySequence(key), this);
        connect(shortcut, &QShortcut::activated,
                [command]() { command->action({}); });
        m_config.shortcuts[action] = key;
    }
}

// Toggles the fullscreen mode
void
Lektra::ToggleFullscreen() noexcept
{
    bool isFullscreen = this->isFullScreen();
    if (isFullscreen)
        this->showNormal();
    else
        this->showFullScreen();
    m_actionFullscreen->setChecked(!isFullscreen);
}

// Toggles the panel
void
Lektra::TogglePanel() noexcept
{
    bool shown = !m_statusbar->isHidden();
    m_statusbar->setHidden(shown);
    m_actionTogglePanel->setChecked(!shown);
}

// Toggles the menubar
void
Lektra::ToggleMenubar() noexcept
{
    bool shown = !m_menuBar->isHidden();
    m_menuBar->setHidden(shown);
    m_actionToggleMenubar->setChecked(!shown);
}

// Shows the about page
void
Lektra::ShowAbout() noexcept
{
    AboutDialog *abw = new AboutDialog(this);
    abw->show();
}

// Reads the arguments passed with `Lektra` from the
// commandline
void
Lektra::Read_args_parser(argparse::ArgumentParser &argparser) noexcept
{

    if (argparser.is_used("version"))
    {
        qInfo() << "Lektra version: " << APP_VERSION;
        exit(0);
    }

    if (argparser.is_used("config"))
    {
        m_config_file_path
            = QString::fromStdString(argparser.get<std::string>("--config"));
    }

    this->construct();

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
        m_config.behavior.startpage_override = argparser.get<int>("--page");

#ifdef HAS_SYNCTEX
    if (argparser.is_used("synctex-forward"))
    {
        m_config.behavior.startpage_override = -1; // do not override the page

        // Format: --synctex-forward={pdf}#{src}:{line}:{column}
        // Example: --synctex-forward=test.pdf#main.tex:14
        const QString &arg = QString::fromStdString(
            argparser.get<std::string>("--synctex-forward"));

        // Format: file.pdf#file.tex:line
        static const QRegularExpression re(
            QStringLiteral(R"(^(.*)#(.*):(\d+):(\d+)$)"));
        QRegularExpressionMatch match = re.match(arg);

        static const QString homeDir = QString::fromLocal8Bit(qgetenv("HOME"));
        if (match.hasMatch())
        {
            const QString pdfPath
                = match.captured(1).replace(QLatin1Char('~'), homeDir);
            const QString texPath
                = match.captured(2).replace(QLatin1Char('~'), homeDir);
            const int line   = match.captured(3).toInt();
            const int column = match.captured(4).toInt();
            Q_UNUSED(line);
            Q_UNUSED(column);
            OpenFileInNewTab(pdfPath);
            // TODO:
            // synctexLocateInPdf(texPath, line, column);
            // m_model->synctexLocateInPdf();
        }
        else
        {
            qWarning() << "Invalid --synctex-forward format. Expected "
                          "file.pdf#file.tex:line:column";
        }
    }
#endif

    bool hsplit{false}, vsplit{false};

    if (argparser.is_used("vsplit"))
    {
        vsplit = true;
    }

    if (argparser.is_used("hsplit"))
    {
        hsplit = true;
    }

    if (argparser.is_used("files"))
    {
        auto files = argparser.get<std::vector<std::string>>("files");
        m_config.behavior.open_last_visited = false;

        if (!files.empty())
        {
            if (hsplit)
                OpenFilesInHSplit(files);
            else if (vsplit)
                OpenFilesInVSplit(files);
            else
                OpenFiles(files);
        }
        else if (m_config.behavior.open_last_visited)
        {
            openLastVisitedFile();
        }
    }

    if (m_tab_widget->count() == 0 && m_config.window.startup_tab)
        showStartupWidget();
    m_config.behavior.startpage_override = -1;
}

// Populates the `QMenu` for recent files with
// recent files entries from the store
void
Lektra::populateRecentFiles() noexcept
{
    if (!m_config.behavior.recent_files)
    {
        m_recentFilesMenu->setEnabled(false);
        return;
    }

    m_recentFilesMenu->clear();
    for (const RecentFileEntry &entry : m_recent_files_store.entries())
    {
        if (entry.file_path.isEmpty())
            continue;
        const QString path  = entry.file_path;
        const int page      = entry.page_number;
        QAction *fileAction = new QAction(path, m_recentFilesMenu);
        connect(fileAction, &QAction::triggered, this, [this, path, page]()
        { OpenFileInNewTab(path, [this, page]() { gotoPage(page); }); });

        m_recentFilesMenu->addAction(fileAction);
    }

    if (m_recentFilesMenu->isEmpty())
        m_recentFilesMenu->setDisabled(true);
    else
        m_recentFilesMenu->setEnabled(true);
}

// Opens a widget that allows to edit the recent files
// entries
void
Lektra::editLastPages() noexcept
{
    if (!m_config.behavior.remember_last_visited)
    {
        QMessageBox::information(
            this, "Edit Last Pages",
            "Couldn't find the recent files data. Maybe "
            "`remember_last_visited` option is turned off in the config "
            "file");
        return;
    }

    EditLastPagesWidget *elpw
        = new EditLastPagesWidget(&m_recent_files_store, this);
    elpw->show();
    connect(elpw, &EditLastPagesWidget::finished, this,
            &Lektra::populateRecentFiles);
}

// Helper function to open last visited file
void
Lektra::openLastVisitedFile() noexcept
{
    const auto &entries = m_recent_files_store.entries();
    if (entries.empty())
        return;

    const auto &entry = entries.front();
    if (QFile::exists(entry.file_path))
    {
        OpenFileInNewTab(entry.file_path);
        gotoPage(entry.page_number);
    }
}

// Zoom out the file
void
Lektra::ZoomOut() noexcept
{
    if (m_doc)
        m_doc->ZoomOut();
}

// Zoom in the file
void
Lektra::ZoomIn() noexcept
{
    if (m_doc)
        m_doc->ZoomIn();
}

void
Lektra::Zoom_set() noexcept
{
    if (m_doc)
    {
        bool ok           = false;
        const double zoom = QInputDialog::getDouble(
            this, "Set Zoom",
            "Enter zoom level (e.g. 1.5 for 150%):", m_doc->zoom(), 0.1, 10.0,
            2, &ok);
        if (ok)
            m_doc->setZoom(zoom);
    }
}

// Resets zoom
void
Lektra::ZoomReset() noexcept
{
    if (m_doc)
        m_doc->ZoomReset();
}

// Go to a particular page (asks user with a dialog)
void
Lektra::Goto_page() noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    int total = m_doc->model()->numPages();
    if (total == 0)
    {
        QMessageBox::information(this, "Goto Page",
                                 "This document has no pages");
        return;
    }

    bool ok;
    int pageno = QInputDialog::getInt(
        this, "Goto Page", QString("Enter page number (1 to %1)").arg(total),
        m_doc->pageNo() + 1, 0, m_doc->numPages(), 1, &ok);

    if (!ok)
        return;

    if (pageno <= 0 || pageno > total)
    {
        QMessageBox::critical(this, "Goto Page",
                              QString("Page %1 is out of range").arg(pageno));
        return;
    }

    gotoPage(pageno);
}

// Go to a particular page (no dialog)
void
Lektra::gotoPage(int pageno) noexcept
{
    if (m_doc)
    {
        m_doc->GotoPageWithHistory(pageno - 1);
    }
}

void
Lektra::GotoLocation(int pageno, float x, float y) noexcept
{
    if (m_doc)
        m_doc->GotoLocation({pageno, x, y});
}

void
Lektra::GotoLocation(const DocumentView::PageLocation &loc) noexcept
{
    if (m_doc)
        m_doc->GotoLocation(loc);
}

// Goes to the next search hit
void
Lektra::NextHit() noexcept
{
    if (m_doc)
        m_doc->NextHit();
}

void
Lektra::GotoHit(int index) noexcept
{
    if (m_doc)
        m_doc->GotoHit(index);
}

// Goes to the previous search hit
void
Lektra::PrevHit() noexcept
{
    if (m_doc)
        m_doc->PrevHit();
}

// Scrolls left in the file
void
Lektra::ScrollLeft() noexcept
{
    if (m_doc)
        m_doc->ScrollLeft();
}

// Scrolls right in the file
void
Lektra::ScrollRight() noexcept
{
    if (m_doc)
        m_doc->ScrollRight();
}

// Scrolls up in the file
void
Lektra::ScrollUp() noexcept
{
    if (m_doc)
        m_doc->ScrollUp();
}

// Scrolls down in the file
void
Lektra::ScrollDown() noexcept
{
    if (m_doc)
        m_doc->ScrollDown();
}

// Rotates the file in clockwise direction
void
Lektra::RotateClock() noexcept
{
    if (m_doc)
        m_doc->RotateClock();
}

// Rotates the file in anticlockwise direction
void
Lektra::RotateAnticlock() noexcept
{
    if (m_doc)
        m_doc->RotateAnticlock();
}

// Shows link hints for each visible link to visit link
// using the keyboard
void
Lektra::VisitLinkKB() noexcept
{
    if (m_doc)
    {
        m_lockedInputBuffer.clear();
        m_link_hint_map = m_doc->LinkKB();
        if (!m_link_hint_map.isEmpty())
        {
            m_link_hint_current_mode = LinkHintMode::Visit;
            m_link_hint_mode         = true;
            m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);
        }
    }
}

// Shows link hints for each visible link to copy link
// using the keyboard
void
Lektra::CopyLinkKB() noexcept
{
    if (m_doc)
    {
        m_lockedInputBuffer.clear();
        m_link_hint_map = m_doc->LinkKB();
        if (!m_link_hint_map.isEmpty())
        {
            m_link_hint_current_mode = LinkHintMode::Copy;
            m_link_hint_mode         = true;
            m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);
        }
    }
}

// Clears the currently selected text in the file
void
Lektra::Selection_cancel() noexcept
{
    if (m_doc)
        m_doc->ClearTextSelection();
}

// Copies the text selection (if any) to the clipboard
void
Lektra::Selection_copy() noexcept
{
    if (m_doc)
        m_doc->YankSelection();
}

// TODO: Fix DWIM version
bool
Lektra::OpenFileDWIM(const QString &filename) noexcept
{
    if (m_tab_widget->count() == 0)
        return OpenFileInNewTab(filename);

    DocumentContainer *container
        = m_tab_widget->rootContainer(m_tab_widget->currentIndex());
    if (!container)
        return OpenFileInNewTab(filename);

    // No active view or empty — reuse current pane
    if (!m_doc || m_doc->filePath().isEmpty())
        return OpenFileInContainer(container, filename, {}, m_doc);

    if (container->getViewCount() > 1)
        return OpenFileInContainer(container, filename, {}, m_doc);

    // Single view with a file — open in new tab
    return OpenFileInNewTab(filename);
}

bool
Lektra::OpenFileInContainer(DocumentContainer *container,
                            const QString &filename,
                            const std::function<void()> &callback,
                            DocumentView *targetView) noexcept
{
    if (!container)
        return false;

    if (filename.isEmpty())
    {
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter(tr("PDF Files (*.pdf);;All Files (*)"));
        if (dialog.exec())
        {
            const QStringList selected = dialog.selectedFiles();
            if (!selected.isEmpty())
                return OpenFileInContainer(container, selected.first(),
                                           callback, targetView);
        }
        return false;
    }

    // if (DocumentView *existing = findOpenView(filename))
    // {
    //     DocumentContainer *container = existing->container();
    //     m_tab_widget->setCurrentIndex(m_tab_widget->indexOf(container));
    //     container->focusView(existing);
    //     if (callback)
    //         callback();
    //     return true;
    // }

    DocumentView *view = targetView ? targetView : container->view();
    if (!view)
        return false;

    view->setDPR(m_dpr); // match OpenFileInNewTab

    const int tabIndex = m_tab_widget->currentIndex();
    // Update tab title once loaded
    connect(view, &DocumentView::openFileFinished, this,
            [this, tabIndex](DocumentView *doc)
    {
        if (validTabIndex(tabIndex))
            m_tab_widget->tabBar()->setTabText(tabIndex, m_config.tabs.full_path
                                                             ? doc->filePath()
                                                             : doc->fileName());
        updatePanel();
    }, Qt::SingleShotConnection);

    view->openAsync(filename);

    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());

    setCurrentDocumentView(view); // immediate, like OpenFileInNewTab

    if (callback)
    {
        connect(view, &DocumentView::openFileFinished, this,
                [callback](DocumentView *) { callback(); },
                Qt::SingleShotConnection);
    }

    insertFileToDB(filename, 1);
    return true;
}

void
Lektra::OpenFiles(const std::vector<std::string> &filenames) noexcept
{
    for (const std::string &f : filenames)
        OpenFileInNewTab(QString::fromStdString(f));
}

void
Lektra::OpenFilesInVSplit(const std::vector<std::string> &files) noexcept
{
#ifndef NDEBUG
    qDebug() << "Lektra::OpenFilesInVSplit(): Opening files in vertical split:"
             << files.size();
#endif
    if (files.empty())
        return;

    // First file always opens in a new tab
    OpenFileInNewTab(QString::fromStdString(files[0]), [this, files]()
    {
        // Subsequent files split into that tab
        for (size_t i = 1; i < files.size(); ++i)
            OpenFileVSplit(QString::fromStdString(files[i]));
    });
}

void
Lektra::OpenFilesInHSplit(const std::vector<std::string> &files) noexcept
{
#ifndef NDEBUG
    qDebug()
        << "Lektra::OpenFilesInHSplit(): Opening files in horizontal split:"
        << files.size();
#endif
    if (files.empty())
        return;

    // First file always opens in a new tab
    OpenFileInNewTab(QString::fromStdString(files[0]), [this, files]()
    {
        // Subsequent files split into that tab
        for (size_t i = 1; i < files.size(); ++i)
            OpenFileHSplit(QString::fromStdString(files[i]));
    });
}

// Opens multiple files given a list of file paths
void
Lektra::OpenFilesInNewTab(const std::vector<std::string> &files) noexcept
{
    const bool was_batch_opening = m_batch_opening;
    m_batch_opening              = true;
    for (const std::string &s : files)
        OpenFileInNewTab(QString::fromStdString(s));
    m_batch_opening = was_batch_opening;
}

// Opens multiple files given a list of file paths
void
Lektra::OpenFilesInNewTab(const QStringList &files) noexcept
{
    const bool was_batch_opening = m_batch_opening;
    m_batch_opening              = true;
    for (const QString &file : files)
        OpenFileInNewTab(file);
    m_batch_opening = was_batch_opening;
}

// Opens a file given the DocumentView pointer
// bool
// Lektra::OpenFile(DocumentView *view) noexcept
// {
//     initTabConnections(view);
//     view->setDPR(m_dpr);

//     const QString title
//         = m_config.tabs.full_path ? m_doc->filePath() :
//         m_doc->fileName();
//     m_tab_widget->addTab(view, title);

//     // Switch to already opened filepath, if it's open.
//     auto it = m_path_tab_map.find(fileName);
//     if (it != m_path_tab_map.end())
//     {
//         int existingIndex = m_tab_widget->indexOf(it.value());
//         if (existingIndex != -1)
//         {
//             m_tab_widget->setCurrentIndex(existingIndex);
//             return true;
//         }
//     }

//     return false;
// }

DocumentView *
Lektra::OpenFileInNewTab(const QString &filename,
                         const std::function<void()> &callback) noexcept
{
    if (filename.isEmpty())
    {
        // Show file picker
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter(tr("PDF Files (*.pdf);;All Files (*)"));

        if (dialog.exec())
        {
            QStringList selected = dialog.selectedFiles();
            if (!selected.isEmpty())
                return OpenFileInNewTab(selected.first(), callback);
        }
        return nullptr;
    }

    // Check if file is already open
    // if (DocumentView *existing = findOpenView(filename))
    // {
    //     DocumentContainer *container = existing->container();
    //     m_tab_widget->setCurrentIndex(m_tab_widget->indexOf(container));
    //     container->focusView(existing);
    //     if (callback)
    //         callback();
    //     return true;
    // }

    // Create a new DocumentView
    DocumentView *view = new DocumentView(m_config, this);

    // Create a DocumentContainer with this view
    DocumentContainer *container = new DocumentContainer(view, this);

    // Connect container signals
    connect(container, &DocumentContainer::viewCreated, this,
            [this](DocumentView *newView)
    {
        // Initialize the new view with connections
        initTabConnections(newView);

        // Update m_doc if this is in the current tab
        int currentTabIndex = m_tab_widget->currentIndex();
        DocumentContainer *currentContainer
            = m_tab_widget->rootContainer(currentTabIndex);
        if (currentContainer && currentContainer->view() == newView)
            setCurrentDocumentView(newView);
    });

    connect(container, &DocumentContainer::viewClosed, this,
            [this](DocumentView *closedView)
    {
        // If the closed view was m_doc, update to current view
        // if (m_doc == closedView)
        // {
        //     int currentTabIndex = m_tab_widget->currentIndex();
        //     DocumentContainer *currentContainer
        //         = m_tab_widget->rootContainer(currentTabIndex);
        //     if (currentContainer)
        //         setCurrentDocumentView(currentContainer->view());
        // }
    });

    connect(container, &DocumentContainer::currentViewChanged, container,
            [this](DocumentView *newView) { setCurrentDocumentView(newView); });

    // Initialize connections for the initial view
    initTabConnections(view);

    // Set DPR BEFORE opening the file to ensure correct resolution
    // rendering
    view->setDPR(m_dpr);

    // Open the file asynchronously
    view->openAsync(filename);

    // Add the container as a tab
    QString tabTitle = QFileInfo(filename).fileName();
    int tabIndex     = m_tab_widget->addTab(container, tabTitle);

    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());

    // Set as current tab
    m_tab_widget->setCurrentIndex(tabIndex);

    // Update current view reference
    setCurrentDocumentView(view);

    // Add to recent files
    insertFileToDB(filename, 1);

    if (callback)
    {
        connect(view, &DocumentView::openFileFinished, this,
                [callback](DocumentView *view)
        {
            Q_UNUSED(view);
            callback();
        }, Qt::SingleShotConnection);
    }

    return view;
}

DocumentView *
Lektra::openFileSplitHelper(const QString &filename,
                            const std::function<void()> &callback,
                            Qt::Orientation orientation)
{
    if (filename.isEmpty())
    {
        // Show file picker
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter(tr("PDF Files (*.pdf);;All Files (*)"));

        if (dialog.exec())
        {
            const QStringList selected = dialog.selectedFiles();
            if (!selected.isEmpty())
                return openFileSplitHelper(selected.first(), callback,
                                           orientation);
        }
        return nullptr;
    }

    // Check if file is already open
    // if (DocumentView *existing = findOpenView(filename))
    // {
    //     DocumentContainer *container = existing->container();
    //     m_tab_widget->setCurrentIndex(m_tab_widget->indexOf(container));
    //     container->focusView(existing);
    //     if (callback)
    //         callback();
    //     return true;
    // }

    const int tabIndex = m_tab_widget->currentIndex();

    if (!validTabIndex(tabIndex))
    {
        // No tabs open, open in new tab instead
        return OpenFileInNewTab(filename, callback);
    }

    DocumentContainer *container = m_tab_widget->rootContainer(tabIndex);

    if (!container)
        throw std::runtime_error("No container found for current tab");

    DocumentView *currentView = container->view();
    if (!currentView)
        return nullptr;

    DocumentView *newView
        = container->split(currentView, orientation, filename);

    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());
    insertFileToDB(filename, 1);

    if (callback)
    {
        connect(newView, &DocumentView::openFileFinished, this,
                [callback](DocumentView *) { callback(); },
                Qt::SingleShotConnection);
    }

    return newView;
}

DocumentView *
Lektra::OpenFileVSplit(const QString &filename,
                       const std::function<void()> &callback)
{
    return openFileSplitHelper(filename, callback, Qt::Vertical);
}

DocumentView *
Lektra::OpenFileHSplit(const QString &filename,
                       const std::function<void()> &callback)
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
                            const std::function<void()> &callback) noexcept
{
    if (filePath.isEmpty())
    {
        QStringList files;
        files = QFileDialog::getOpenFileNames(
            this, "Open File", "", "PDF Files (*.pdf);; All Files (*)");
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

    // if (DocumentView *existing = findOpenView(filePath))
    // {
    //     DocumentContainer *container = existing->container();
    //     m_tab_widget->setCurrentIndex(m_tab_widget->indexOf(container));
    //     container->focusView(existing);
    //     if (callback)
    //         callback();
    //     return true;
    // }

    if (!QFile::exists(fp))
    {
        QMessageBox::warning(this, "Open File",
                             QString("Unable to find %1").arg(fp));
        return false;
    }

    QStringList args;
    args << fp;
    bool started = QProcess::startDetached(
        QCoreApplication::applicationFilePath(), args);
    if (!started)
        m_message_bar->showMessage("Failed to open file in new window");
    return started;
}

// Opens the properties widget with properties for the
// current file
void
Lektra::FileProperties() noexcept
{
    if (m_doc)
        m_doc->FileProperties();
}

// Saves the current file
void
Lektra::SaveFile() noexcept
{
    if (m_doc)
        m_doc->SaveFile();
}

// Saves the current file as a new file
void
Lektra::SaveAsFile() noexcept
{
    if (m_doc)
        m_doc->SaveAsFile();
}

// Fit the document to the width of the window
void
Lektra::Fit_width() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Width);
}

// Fit the document to the height of the window
void
Lektra::Fit_height() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Height);
}

// Fit the document to the window
void
Lektra::Fit_page() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Window);
}

// Toggle auto-resize mode
void
Lektra::ToggleAutoResize() noexcept
{
    if (m_doc)
        m_doc->ToggleAutoResize();
}

// Show or hide the outline panel
void
Lektra::Show_outline() noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    if (!m_doc->model()->getOutline())
    {
        QMessageBox::information(this, "Outline",
                                 "This document has no outline");
        return;
    }

    if (!m_outline_picker)
    {
        m_outline_picker = new OutlinePicker(m_config.outline, this);
        m_outline_picker->setKeybindings(m_picker_keybinds);
        connect(m_outline_picker, &OutlinePicker::jumpToLocationRequested, this,
                [this](int page, const QPointF &pos) // page returned is 1-based
        {
            m_doc->GotoLocationWithHistory(
                {page, (float)pos.x(), (float)pos.y()});
        });
    }

    m_outline_picker->setOutline(m_doc->model()->getOutline());

    if (m_outline_picker->hasOutline())
    {
        m_outline_picker->launch();
    }
}

// Show the highlight search panel
void
Lektra::Show_highlight_search() noexcept
{
    if (!m_doc)
        return;

    if (!m_highlight_search_picker)
    {
        m_highlight_search_picker = new HighlightSearchPicker(this);
        m_highlight_search_picker->setKeybindings(m_picker_keybinds);

        connect(m_highlight_search_picker,
                &HighlightSearchPicker::gotoLocationRequested, this,
                [this](int page, float x, float y)
        { GotoLocation(page, x, y); });
    }

    m_highlight_search_picker->setModel(m_doc->model());
    m_highlight_search_picker->launch();
}

// Invert colors of the document
void
Lektra::InvertColor() noexcept
{
    if (m_doc)
    {
        m_doc->setInvertColor(!m_doc->invertColor());
        m_actionInvertColor->setChecked(!m_actionInvertColor->isChecked());
    }
}

// Toggle text highlight mode
void
Lektra::ToggleTextHighlight() noexcept
{
    if (m_doc)
    {
        if (m_doc->fileType() == Model::FileType::PDF)
            m_doc->ToggleTextHighlight();
        else
            QMessageBox::information(this, "Toggle Text Highlight",
                                     "Not a PDF file to annotate");
    }
}

// Toggle text selection mode
void
Lektra::ToggleTextSelection() noexcept
{
    if (m_doc)
        m_doc->ToggleTextSelection();
}

// Toggle rectangle annotation mode
void
Lektra::ToggleAnnotRect() noexcept
{
    if (m_doc)
    {
        if (m_doc->fileType() == Model::FileType::PDF)
            m_doc->ToggleAnnotRect();
        else
            QMessageBox::information(this, "Toggle Annot Rect",
                                     "Not a PDF file to annotate");
    }
}

// Toggle annotation select mode
void
Lektra::ToggleAnnotSelect() noexcept
{
    if (m_doc)
    {
        if (m_doc->fileType() == Model::FileType::PDF)
            m_doc->ToggleAnnotSelect();
        else
            QMessageBox::information(this, "Toggle Annot Select",
                                     "Not a PDF file to annotate");
    }
}

// Toggle popup annotation mode
void
Lektra::ToggleAnnotPopup() noexcept
{
    if (!m_doc)
        return;

    if (m_doc->fileType() == Model::FileType::PDF)
        m_doc->ToggleAnnotPopup();
    else
        QMessageBox::information(this, "Toggle Annot Popup",
                                 "Not a PDF file to annotate");
}

// Toggle region select mode
void
Lektra::ToggleRegionSelect() noexcept
{
    if (!m_doc)
        return;
    m_doc->ToggleRegionSelect();
}

// Go to the first page
void
Lektra::FirstPage() noexcept
{
    if (!m_doc)
        return;

    m_doc->GotoFirstPage();
    updatePageNavigationActions();
}

// Go to the previous page
void
Lektra::PrevPage() noexcept
{
    if (!m_doc)
        return;
    m_doc->GotoPrevPage();
    updatePageNavigationActions();
}

// Go to the next page
void
Lektra::NextPage() noexcept
{
    if (!m_doc)
        return;

    m_doc->GotoNextPage();
    updatePageNavigationActions();
}

// Go to the last page
void
Lektra::LastPage() noexcept
{
    if (m_doc)
        m_doc->GotoLastPage();

    updatePageNavigationActions();
}

// Go back in the page history
void
Lektra::GoBackHistory() noexcept
{
    if (m_doc)
        m_doc->GoBackHistory();
}

// Go forward in the page history
void
Lektra::GoForwardHistory() noexcept
{
    if (m_doc)
        m_doc->GoForwardHistory();
}

// Highlight text annotation for the current selection
void
Lektra::TextHighlightCurrentSelection() noexcept
{
    if (m_doc)
        m_doc->TextHighlightCurrentSelection();
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

    // Tab drag and drop connections for cross-window tab transfer
    connect(m_tab_widget, &TabWidget::tabDataRequested, this,
            &Lektra::handleTabDataRequested);
    connect(m_tab_widget, &TabWidget::tabDropReceived, this,
            &Lektra::handleTabDropReceived);
    connect(m_tab_widget, &TabWidget::tabDetached, this,
            &Lektra::handleTabDetached);
    connect(m_tab_widget, &TabWidget::tabDetachedToNewWindow, this,
            &Lektra::handleTabDetachedToNewWindow);

    QWindow *win = window()->windowHandle();

    m_dpr = m_screen_dpr_map[win->screen()->name()];

    connect(win, &QWindow::screenChanged, this, [&](QScreen *screen)
    {
        if (std::holds_alternative<QMap<QString, float>>(
                m_config.rendering.dpr))
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
    });

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
            [this](const int index)
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

        m_tab_widget->removeTab(index);
        if (m_tab_widget->count() == 0)
        {
            setCurrentDocumentView(nullptr);
        }
    });

    connect(m_actionInvertColor, &QAction::triggered, [&]() { InvertColor(); });

    connect(m_navMenu, &QMenu::aboutToShow, this,
            &Lektra::updatePageNavigationActions);
}

// Handle when the file name is changed
void
Lektra::handleFileNameChanged(const QString &name) noexcept
{
    m_statusbar->setFileName(name);
    this->setWindowTitle(name);
}

// Handle when the current tab is changed
void
Lektra::handleCurrentTabChanged(int index) noexcept
{
    if (!validTabIndex(index))
    {
        setCurrentDocumentView(nullptr);
        return;
    }

    DocumentContainer *container = m_tab_widget->rootContainer(
        index); // get the root container for the current tab
    if (!container)
    {
        setCurrentDocumentView(nullptr);
        return;
    }

    // Update m_doc to current view in the container
    setCurrentDocumentView(container->view());

    if (m_doc)
    {
        // Update UI
        emit m_doc->fileNameChanged(m_doc->fileName());
        updateUiEnabledState();
        updatePageNavigationActions();
        updateSelectionModeActions();

        // Update panel if needed
        updatePanel();
    }
}

void
Lektra::handleTabDataRequested(int index, TabBar::TabData *outData) noexcept
{
    if (!validTabIndex(index))
        return;

    // Get the DocumentContainer, not the widget directly
    DocumentContainer *container = m_tab_widget->rootContainer(index);
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
    OpenFileInNewTab(data.filePath, [this, data]()
    {
        if (!m_doc)
            return;

        // Restore document state
        m_doc->GotoPage(data.currentPage - 1);
        m_doc->setZoom(data.zoom);
        m_doc->setInvertColor(data.invertColor);

        // Restore rotation
        int currentRotation = m_doc->model()->rotation();
        int targetRotation  = data.rotation;
        while (currentRotation != targetRotation)
        {
            m_doc->RotateClock();
            currentRotation = (currentRotation + 90) % 360;
        }

        // Restore fit mode
        m_doc->setFitMode(static_cast<DocumentView::FitMode>(data.fitMode));
        // updatePanel();
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
        m_message_bar->showMessage("Failed to open tab in new window");
    }
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
        DocumentView *doc
            = qobject_cast<DocumentView *>(m_tab_widget->widget(i));
        if (doc)
        {
            if (m_config.behavior.remember_last_visited)
            {
                const int page = doc->pageNo() + 1;
                insertFileToDB(doc->filePath(), page > 0 ? page : 1);
            }

            // Unsaved Changes
            if (doc->isModified())
            {
                int ret = QMessageBox::warning(
                    this, "Unsaved Changes",
                    QString("File %1 has unsaved changes. Do you want to save "
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
            this, "Confirm Quit", "Are you sure you want to quit Lektra?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (ret == QMessageBox::No)
        {
            e->ignore();
            return;
        }
    }

    e->accept();
}

void
Lektra::ToggleTabBar() noexcept
{
    QTabBar *bar = m_tab_widget->tabBar();

    if (bar->isVisible())
        bar->hide();
    else
        bar->show();
}

// Event filter to capture key events for link hints mode and
// other events
bool
Lektra::eventFilter(QObject *object, QEvent *event)
{
    const QEvent::Type type = event->type();

    // Link Hint Handle Key Press
    if (m_link_hint_mode)
    {
        if (handleLinkHintEvent(event))
            return true;
    }

    // Context menu for the tab widgets
    if ((object == m_tab_widget->tabBar() || object == m_tab_widget)
        && type == QEvent::ContextMenu)
        return handleTabContextMenu(object, event);

    // Let other events pass through
    return QObject::eventFilter(object, event);
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

bool
Lektra::handleTabContextMenu(QObject *object, QEvent *event) noexcept
{
    auto *contextEvent = static_cast<QContextMenuEvent *>(event);
    if (!contextEvent || !m_tab_widget)
        return false;

    const QPoint tabPos = object == m_tab_widget->tabBar()
                              ? contextEvent->pos()
                              : m_tab_widget->tabBar()->mapFrom(
                                    m_tab_widget, contextEvent->pos());
    const int index     = m_tab_widget->tabBar()->tabAt(tabPos);
    if (index == -1)
        return true;

    QMenu menu;
    menu.addAction("Open Location", this,
                   [this, index]() { openInExplorerForIndex(index); });
    menu.addAction("File Properties", this,
                   [this, index]() { filePropertiesForIndex(index); });
    menu.addSeparator();
    menu.addAction("Move Tab to New Window", this, [this, index]()
    {
        TabBar::TabData data;
        handleTabDataRequested(index, &data);
        if (!data.filePath.isEmpty())
            handleTabDetachedToNewWindow(index, data);
    });
    menu.addAction("Close Tab", this,
                   [this, index]() { m_tab_widget->tabCloseRequested(index); });

    menu.exec(contextEvent->globalPos());
    return true;
}

bool
Lektra::handleLinkHintEvent(QEvent *event) noexcept
{
    const QEvent::Type type = event->type();
    switch (type)
    {
        case QEvent::KeyPress:
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            switch (keyEvent->key())
            {
                case Qt::Key_Escape:
                    handleEscapeKeyPressed();
                    return true;

                case Qt::Key_Backspace:
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
                    m_lockedInputBuffer.removeLast();
#else
                    if (!m_lockedInputBuffer.isEmpty())
                        m_lockedInputBuffer.chop(1);
#endif
                    if (m_doc)
                        m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);
                    return true;
                default:
                    break;
            }

            QString text = keyEvent->text();
            if (text.isEmpty())
            {
                const int key = keyEvent->key();
                if (key >= Qt::Key_0 && key <= Qt::Key_9)
                    text = QString(QChar('0' + (key - Qt::Key_0)));
            }

            bool appended = false;
            if (text.size() == 1 && text.at(0).isDigit())
            {
                m_lockedInputBuffer += text;
                appended = true;
            }

            if (!appended)
                return true;

            if (m_doc)
                m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);

            int num = m_lockedInputBuffer.toInt();
            auto it = m_link_hint_map.find(num);
            if (it != m_link_hint_map.end())
            {
                const Model::LinkInfo &info = it.value();

                switch (m_link_hint_current_mode)
                {
                    case LinkHintMode::None:
                        break;

                    case LinkHintMode::Visit:
                        m_doc->FollowLink(info);
                        break;

                    case LinkHintMode::Copy:
                        m_clipboard->setText(info.uri);
                        break;
                }

                m_lockedInputBuffer.clear();
                m_link_hint_map.clear();
                m_doc->ClearKBHintsOverlay();
                m_link_hint_mode = false;
                return true;
            }
            keyEvent->accept();
            return true;
        }
        case QEvent::ShortcutOverride:
            event->accept();
            return true;
        default:
            break;
    }

    return false;
}

// Used for waiting input events like marks etc.
// bool
// Lektra::handleGetInputEvent(QEvent *event) noexcept
// {
//     const QEvent::Type type = event->type();
//     switch (type)
//     {
//         case QEvent::KeyPress:
//         {
//             QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
//             switch (keyEvent->key())
//             {
//                 case Qt::Key_Escape:
//                     handleEscapeKeyPressed();
//                     return true;

//                 case Qt::Key_Backspace:
// #if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
//                     m_lockedInputBuffer.removeLast();
// #else
//                     if (!m_lockedInputBuffer.isEmpty())
//                         m_lockedInputBuffer.chop(1);
// #endif
//                     return true;
//                 default:
//                     break;
//             }

//             QString text = keyEvent->text();

//             bool appended = false;
//             if (text.size() == 1 && text.at(0).isDigit())
//             {
//                 m_lockedInputBuffer += text;
//                 appended = true;
//             }

//             if (!appended)
//                 return true;

//             int num = m_lockedInputBuffer.toInt();
//             keyEvent->accept();
//             return true;
//         }

//         case QEvent::ShortcutOverride:
//             event->accept();
//             return true;
//         default:
//             break;
//     }

//     return false;
// }

// Opens the file of tab with index `index`
// in file manager program
void
Lektra::openInExplorerForIndex(int index) noexcept
{
    DocumentView *doc
        = qobject_cast<DocumentView *>(m_tab_widget->widget(index));
    if (doc)
    {
        const QString filePath = doc->filePath();
        if (QFile::exists(filePath))
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
    }
}

// Shows the properties of file of tab with index `index`
void
Lektra::filePropertiesForIndex(int index) noexcept
{
    DocumentView *doc
        = qobject_cast<DocumentView *>(m_tab_widget->widget(index));
    // doc->setInvertColor(!doc->invertColor());
    if (doc)
        doc->FileProperties();
}

// Initialize connections on each tab addition
void
Lektra::initTabConnections(DocumentView *docwidget) noexcept
{
    connect(docwidget, &DocumentView::panelNameChanged, this,
            [this](const QString &name) { m_statusbar->setFileName(name); });

    connect(docwidget, &DocumentView::openFileFinished, this,
            [this](DocumentView *doc)
    {
        // Only update the panel if this view is the currently active one.
        // If it's a background split, don't clobber the active view's info.
        if (m_doc == doc)
        {
            updatePanel();
            // Also drive the tab title, which suffers the same timing
            // problem
            int index = m_tab_widget->currentIndex();
            if (validTabIndex(index))
            {
                m_tab_widget->tabBar()->setTabText(
                    index, m_config.tabs.full_path ? doc->filePath()
                                                   : doc->fileName());
            }
        }
    });

    connect(docwidget, &DocumentView::currentPageChanged, this,
            [this, docwidget](int pageno)
    {
        if (m_doc == docwidget)
            m_statusbar->setPageNo(pageno);
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

    // connect(docwidget, &DocumentView::searchModeChanged, m_panel,
    //         &SearchBar::setSearchMode);

    connect(docwidget, &DocumentView::searchIndexChanged, m_search_bar,
            &SearchBar::setSearchIndex);

    connect(docwidget, &DocumentView::totalPageCountChanged, m_statusbar,
            &Statusbar::setTotalPageCount);

    connect(docwidget, &DocumentView::highlightColorChanged, m_statusbar,
            &Statusbar::setHighlightColor);

    connect(docwidget, &DocumentView::selectionModeChanged, m_statusbar,
            &Statusbar::setMode);

    connect(docwidget, &DocumentView::clipboardContentChanged, this,
            [&](const QString &text) { m_clipboard->setText(text); });

    connect(docwidget, &DocumentView::autoResizeActionUpdate, this,
            [&](bool state) { m_actionAutoresize->setChecked(state); });

    connect(docwidget, &DocumentView::insertToDBRequested, this,
            &Lektra::insertFileToDB);

    connect(docwidget, &DocumentView::ctrlLinkClickRequested, this,
            &Lektra::handleCtrlLinkClickRequested);
}

// Insert file to store when tab is closed to track
// recent files
void
Lektra::insertFileToDB(const QString &fname, int pageno) noexcept
{
#ifndef NDEBUG
    qDebug() << "Inserting file to recent files store:" << fname
             << "Page number:" << pageno;
#endif
    const QDateTime now = QDateTime::currentDateTime();
    m_recent_files_store.upsert(fname, pageno, now);
    if (!m_recent_files_store.save())
        qWarning() << "Failed to save recent files store";
}

// Update the menu actions based on the current document state
void
Lektra::updateMenuActions() noexcept
{
    const bool valid = m_doc != nullptr;

    m_statusbar->hidePageInfo(!valid);
    m_actionCloseFile->setEnabled(valid);

    if (valid)
    {
        Model *model = m_doc->model();
        if (model)
        {
            m_actionInvertColor->setEnabled(model->invertColor());
            QUndoStack *undoStack = model->undoStack();
            m_actionUndo->setEnabled(undoStack->canUndo());
            m_actionRedo->setEnabled(undoStack->canRedo());
        }
        else
            m_actionInvertColor->setEnabled(false);

        m_actionAutoresize->setCheckable(true);
        m_actionAutoresize->setChecked(m_doc->autoResize());
        m_actionTextSelect->setChecked(false);
        m_actionTextHighlight->setChecked(false);
        m_actionAnnotEdit->setChecked(false);
        m_actionAnnotRect->setChecked(false);
        m_actionAnnotPopup->setChecked(false);
        updateSelectionModeActions();
    }
    else
    {
        m_actionInvertColor->setEnabled(false);
        m_actionAutoresize->setCheckable(false);

        m_actionTextSelect->setChecked(false);
        m_actionTextHighlight->setChecked(false);
        m_actionAnnotEdit->setChecked(false);
        m_actionAnnotRect->setChecked(false);
        m_actionAnnotPopup->setChecked(false);
        m_actionUndo->setEnabled(false);
        m_actionRedo->setEnabled(false);
        m_modeMenu->setEnabled(false);
    }
}

// Update the panel info
void
Lektra::updatePanel() noexcept
{
    if (m_doc)
    {
        Model *model = m_doc->model();
        if (!model)
            return;

        if (m_config.statusbar.file_name_only)
            m_statusbar->setFileName(m_doc->fileName());
        else
            m_statusbar->setFileName(m_doc->filePath());

        m_statusbar->setPortalMode(m_doc->portal());
        m_statusbar->setMode(m_doc->selectionMode());
        m_statusbar->setHighlightColor(model->highlightAnnotColor());

        const int numPages = model->numPages();
        if (numPages > 0)
        {
            m_statusbar->hidePageInfo(false);
            m_statusbar->setTotalPageCount(numPages);
            m_statusbar->setPageNo(m_doc->pageNo() + 1);
        }
        else
        {
            // File still loading — hide until openFileFinished fires
            m_statusbar->hidePageInfo(true);
        }
    }
    else
    {
        m_statusbar->hidePageInfo(true);
        m_statusbar->setFileName("");
        m_statusbar->setHighlightColor("");
    }
}

// Loads the given session (if it exists)
void
Lektra::LoadSession(QString sessionName) noexcept
{
    QStringList existingSessions = getSessionFiles();
    if (existingSessions.empty())
    {
        QMessageBox::information(this, "Load Session", "No sessions found");
        return;
    }

    if (sessionName.isEmpty())
    {
        bool ok;
        sessionName = QInputDialog::getItem(
            this, "Load Session",
            "Session to load (existing sessions are listed): ",
            existingSessions, 0, true, &ok);
    }

    QFile file(m_session_dir.filePath(sessionName + ".json"));

    if (file.open(QIODevice::ReadOnly))
    {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);

        if (err.error != QJsonParseError::NoError)
        {
            QMessageBox::critical(this, "Session File Parse Error",
                                  err.errorString());
#ifndef NDEBUG
            qDebug() << "JSON parse error:" << err.errorString();
#endif
            return;
        }

        if (!doc.isArray())
        {
            QMessageBox::critical(this, "Session File Parse Error",
                                  "Session file root is not an array");
#ifndef NDEBUG
            qDebug() << "Session file root is not an array";
#endif
            return;
        }

        // Create a new Lektra window to load the session into if there's
        // document already opened in the current window
        if (m_tab_widget->count() > 0)
        {
            Lektra *newWindow = new Lektra(sessionName, doc.array());
            newWindow->setAttribute(Qt::WA_DeleteOnClose, true);
        }
        else
        {
            // Open here in this window
            openSessionFromArray(doc.array());
        }
    }
    else
    {

        QMessageBox::critical(
            this, "Open Session",
            QStringLiteral("Could not open session: %1").arg(sessionName));
    }
}

// Returns the session files
QStringList
Lektra::getSessionFiles() noexcept
{
    QStringList sessions;

    if (!m_session_dir.exists())
    {
        if (!m_session_dir.mkpath("."))
        {
            QMessageBox::warning(
                this, "Session Directory",
                "Unable to create sessions directory for some reason");
            return sessions;
        }
    }

    for (const QString &file : m_session_dir.entryList(
             QStringList() << "*.json", QDir::Files | QDir::NoSymLinks))
        sessions << QFileInfo(file).completeBaseName();

    return sessions;
}

// Saves the current session
void
Lektra::SaveSession() noexcept
{
    if (!m_doc)
    {
        QMessageBox::information(this, "Save Session",
                                 "No files in session to save the session");
        return;
    }

    const QStringList &existingSessions = getSessionFiles();

    while (true)
    {
        SaveSessionDialog dialog(existingSessions, this);

        if (dialog.exec() != QDialog::Accepted)
            return;

        const QString &sessionName = dialog.sessionName();

        if (sessionName.isEmpty())
        {
            QMessageBox::information(this, "Save Session",
                                     "Session name cannot be empty");
            return;
        }

        if (m_session_name != sessionName)
        {
            // Ask for overwrite if session with same name exists
            if (existingSessions.contains(sessionName))
            {
                auto choice = QMessageBox::warning(
                    this, "Overwrite Session",
                    QString("Session named \"%1\" already exists. Do you "
                            "want to "
                            "overwrite it?")
                        .arg(sessionName),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

                if (choice == QMessageBox::No)
                    continue;
                if (choice == QMessageBox::Yes)
                {
                    setSessionName(sessionName);
                    break;
                }
            }
            else
            {
                setSessionName(sessionName);
                break;
            }
        }
    }
    // Save the session now
    writeSessionToFile();
}

void
Lektra::writeSessionToFile() noexcept
{
    QJsonArray sessionArray;

    for (int i = 0; i < m_tab_widget->count(); ++i)
    {
        DocumentContainer *container = m_tab_widget->rootContainer(i);
        if (!container)
            continue;

        QJsonObject tabEntry;
        tabEntry["splits"] = container->serializeSplits();
        sessionArray.append(tabEntry);
    }

    const QString sessionFileName
        = m_session_dir.filePath(m_session_name + ".json");
    QFile file(sessionFileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(
            this, "Save Session",
            QStringLiteral("Could not save session: %1").arg(m_session_name));
        return;
    }
    file.write(QJsonDocument(sessionArray).toJson());
    file.close();
}

// Saves the current session under new name
void
Lektra::SaveAsSession(const QString &sessionPath) noexcept
{
    Q_UNUSED(sessionPath);
    if (m_session_name.isEmpty())
    {
        QMessageBox::information(
            this, "Save As Session",
            "Cannot save session as you are not currently in a session");
        return;
    }

    QStringList existingSessions = getSessionFiles();

    QString selectedPath = QFileDialog::getSaveFileName(
        this, "Save As Session", m_session_dir.absolutePath(),
        "Lektra session files (*.json); All Files (*.*)");

    if (selectedPath.isEmpty())
        return;

    if (QFile::exists(selectedPath))
    {
        auto choice = QMessageBox::warning(
            this, "Overwrite Session",
            QString("Session named \"%1\" already exists. Do you want to "
                    "overwrite it?")
                .arg(QFileInfo(selectedPath).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (choice != QMessageBox::Yes)
            return;
    }

    // Save the session
    QString currentSessionPath
        = m_session_dir.filePath(m_session_name + ".json");
    if (!QFile::copy(currentSessionPath, selectedPath))
    {
        QMessageBox::critical(this, "Save As Session",
                              "Failed to save session.");
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
        OpenFileInNewTab(path, [this]()
        {
            int index = m_tab_widget->indexOf(m_startup_widget);
            if (index != -1)
                m_tab_widget->tabCloseRequested(index);
        });
    });
    int index = m_tab_widget->addTab(m_startup_widget, "Startup");
    m_tab_widget->setCurrentIndex(index);
    m_statusbar->setFileName("Start Page");
}

// Update actions and stuff for system tabs
void
Lektra::updateActionsAndStuffForSystemTabs() noexcept
{
    m_statusbar->hidePageInfo(true);
    updateUiEnabledState();
    m_statusbar->setFileName("Start Page");
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

void
Lektra::initCommands() noexcept
{
    // Selection
    m_command_manager.reg("selection_copy",
                          "Copy current selection to clipboard",
                          [this](const QStringList &) { Selection_copy(); });
    m_command_manager.reg("selection_cancel",
                          "Cancel and clear current selection",
                          [this](const QStringList &) { Selection_cancel(); });
    m_command_manager.reg("selection_last", "Reselect the last text selection",
                          [this](const QStringList &)
    { ReselectLastTextSelection(); });

    // Toggles
    m_command_manager.reg("presentation_mode", "Toggle presentation mode",
                          [this](const QStringList &)
    { Toggle_presentation_mode(); });
    m_command_manager.reg("fullscreen", "Toggle fullscreen",
                          [this](const QStringList &) { ToggleFullscreen(); });
    m_command_manager.reg("command_palette", "Open command palette",
                          [this](const QStringList &)
    { Show_command_picker(); });
    m_command_manager.reg("tabs", "Toggle tab bar",
                          [this](const QStringList &) { ToggleTabBar(); });
    m_command_manager.reg("menubar", "Toggle menu bar",
                          [this](const QStringList &) { ToggleMenubar(); });
    m_command_manager.reg("statusbar", "Toggle status bar",
                          [this](const QStringList &) { TogglePanel(); });
    m_command_manager.reg("focus_mode", "Toggle focus mode",
                          [this](const QStringList &) { ToggleFocusMode(); });
    m_command_manager.reg("visual_line_mode", "Toggle visual line mode",
                          [this](const QStringList &)
    { Toggle_visual_line_mode(); });
#ifdef ENABLE_LLM_SUPPORT
    m_command_manager.reg("llm_widget", "Toggle LLM assistant widget",
                          [this](const QStringList &) { ToggleLLMWidget(); });
#endif

    // Link hints
    m_command_manager.reg("link_hint_visit", "Open link using keyboard hint",
                          [this](const QStringList &) { VisitLinkKB(); });
    m_command_manager.reg("link_hint_copy", "Copy link URL using keyboard hint",
                          [this](const QStringList &) { CopyLinkKB(); });

    // Page navigation
    m_command_manager.reg("page_first", "Go to first page",
                          [this](const QStringList &) { FirstPage(); });
    m_command_manager.reg("page_last", "Go to last page",
                          [this](const QStringList &) { LastPage(); });
    m_command_manager.reg("page_next", "Go to next page",
                          [this](const QStringList &) { NextPage(); });
    m_command_manager.reg("page_prev", "Go to previous page",
                          [this](const QStringList &) { PrevPage(); });
    m_command_manager.reg("page_goto", "Jump to a specific page number",
                          [this](const QStringList &) { Goto_page(); });

    // Marks
    m_command_manager.reg("mark_set", "Set a named mark at current position",
                          [this](const QStringList &) { SetMark(); });
    m_command_manager.reg("mark_delete", "Delete a named mark",
                          [this](const QStringList &) { DeleteMark(); });
    m_command_manager.reg("mark_goto", "Jump to a named mark",
                          [this](const QStringList &) { GotoMark(); });

    // Scrolling
    m_command_manager.reg("scroll_down", "Scroll down",
                          [this](const QStringList &) { ScrollDown(); });
    m_command_manager.reg("scroll_up", "Scroll up",
                          [this](const QStringList &) { ScrollUp(); });
    m_command_manager.reg("scroll_left", "Scroll left",
                          [this](const QStringList &) { ScrollLeft(); });
    m_command_manager.reg("scroll_right", "Scroll right",
                          [this](const QStringList &) { ScrollRight(); });

    // Rotation
    m_command_manager.reg("rotate_clock", "Rotate page clockwise",
                          [this](const QStringList &) { RotateClock(); });
    m_command_manager.reg("rotate_anticlock", "Rotate page counter-clockwise",
                          [this](const QStringList &) { RotateAnticlock(); });

    // Location history
    m_command_manager.reg("location_prev", "Go back in location history",
                          [this](const QStringList &) { GoBackHistory(); });
    m_command_manager.reg("location_next", "Go forward in location history",
                          [this](const QStringList &) { GoForwardHistory(); });

    // Zoom
    m_command_manager.reg("zoom_in", "Zoom in",
                          [this](const QStringList &) { ZoomIn(); });
    m_command_manager.reg("zoom_out", "Zoom out",
                          [this](const QStringList &) { ZoomOut(); });
    m_command_manager.reg("zoom_reset", "Reset zoom to default",
                          [this](const QStringList &) { ZoomReset(); });
    m_command_manager.reg("zoom_set", "Set zoom to a specific level",
                          [this](const QStringList &) { Zoom_set(); });

    // Splits
    m_command_manager.reg("split_horizontal", "Split view horizontally",
                          [this](const QStringList &) { VSplit(); });
    m_command_manager.reg("split_vertical", "Split view vertically",
                          [this](const QStringList &) { HSplit(); });
    m_command_manager.reg("split_close", "Close current split",
                          [this](const QStringList &) { Close_split(); });
    m_command_manager.reg("split_focus_right", "Focus split to the right",
                          [this](const QStringList &) { Focus_split_right(); });
    m_command_manager.reg("split_focus_left", "Focus split to the left",
                          [this](const QStringList &) { Focus_split_left(); });
    m_command_manager.reg("split_focus_up", "Focus split above",
                          [this](const QStringList &) { Focus_split_up(); });
    m_command_manager.reg("split_focus_down", "Focus split below",
                          [this](const QStringList &) { Focus_split_down(); });
    m_command_manager.reg(
        "split_close_others", "Close all splits except current",
        [this](const QStringList &) { Close_other_splits(); });

    // Portal
    m_command_manager.reg("portal", "Create or focus portal",
                          [this](const QStringList &)
    { Create_or_focus_portal(); });

    // File operations
    m_command_manager.reg("file_open_tab", "Open file in new tab",
                          [this](const QStringList &) { OpenFileInNewTab(); });
    m_command_manager.reg("file_open_vsplit", "Open file in vertical split",
                          [this](const QStringList &) { OpenFileVSplit(); });
    m_command_manager.reg("file_open_hsplit", "Open file in horizontal split",
                          [this](const QStringList &) { OpenFileHSplit(); });
    m_command_manager.reg("file_open_dwim", "Open file (do what I mean)",
                          [this](const QStringList &) { OpenFileDWIM(); });
    m_command_manager.reg("file_close", "Close current file",
                          [this](const QStringList &) { CloseFile(); });
    m_command_manager.reg("file_save", "Save current file",
                          [this](const QStringList &) { SaveFile(); });
    m_command_manager.reg("file_save_as", "Save current file as a new name",
                          [this](const QStringList &) { SaveAsFile(); });
    m_command_manager.reg("file_encrypt", "Encrypt current document",
                          [this](const QStringList &) { EncryptDocument(); });
    m_command_manager.reg("file_decrypt", "Decrypt current document",
                          [this](const QStringList &) { DecryptDocument(); });
    m_command_manager.reg("file_reload", "Reload current file from disk",
                          [this](const QStringList &) { reloadDocument(); });
    m_command_manager.reg("file_properties", "Show file properties",
                          [this](const QStringList &) { FileProperties(); });
    m_command_manager.reg("files_recent", "Show recently opened files",
                          [this](const QStringList &)
    { Show_recent_files_picker(); });

    // Annotation modes
    m_command_manager.reg("annot_edit_mode", "Toggle annotation select mode",
                          [this](const QStringList &) { ToggleAnnotSelect(); });
    m_command_manager.reg("annot_popup_mode", "Toggle annotation popup mode",
                          [this](const QStringList &) { ToggleAnnotPopup(); });
    m_command_manager.reg("annot_rect_mode", "Toggle rectangle annotation mode",
                          [this](const QStringList &) { ToggleAnnotRect(); });
    m_command_manager.reg("annot_highlight_mode", "Toggle text highlight mode",
                          [this](const QStringList &)
    { ToggleTextHighlight(); });

    // Selection modes
    m_command_manager.reg(
        "selection_mode_text", "Switch to text selection mode",
        [this](const QStringList &) { ToggleTextSelection(); });
    m_command_manager.reg(
        "selection_mode_region", "Switch to region selection mode",
        [this](const QStringList &) { ToggleRegionSelect(); });

    // Fit modes
    m_command_manager.reg("fit_width", "Fit page to window width",
                          [this](const QStringList &) { Fit_width(); });
    m_command_manager.reg("fit_height", "Fit page to window height",
                          [this](const QStringList &) { Fit_height(); });
    m_command_manager.reg("fit_page", "Fit entire page in window",
                          [this](const QStringList &) { Fit_page(); });
    m_command_manager.reg("fit_auto", "Toggle automatic resize to fit",
                          [this](const QStringList &) { ToggleAutoResize(); });

    // Sessions
    m_command_manager.reg("session_save", "Save current session",
                          [this](const QStringList &) { SaveSession(); });
    m_command_manager.reg("session_save_as",
                          "Save current session under a new name",
                          [this](const QStringList &) { SaveAsSession(); });
    m_command_manager.reg("session_load", "Load a saved session",
                          [this](const QStringList &) { LoadSession(); });

    // Tabs
    m_command_manager.reg("tabs_close_left", "Close all tabs to the left",
                          [this](const QStringList &) { TabsCloseLeft(); });
    m_command_manager.reg("tabs_close_right", "Close all tabs to the right",
                          [this](const QStringList &) { TabsCloseRight(); });
    m_command_manager.reg("tabs_close_others", "Close all tabs except current",
                          [this](const QStringList &) { TabsCloseOthers(); });
    m_command_manager.reg("tab_move_right", "Move current tab right",
                          [this](const QStringList &) { TabMoveRight(); });
    m_command_manager.reg("tab_move_left", "Move current tab left",
                          [this](const QStringList &) { TabMoveLeft(); });
    m_command_manager.reg("tab_first", "Switch to first tab",
                          [this](const QStringList &) { Tab_first(); });
    m_command_manager.reg("tab_last", "Switch to last tab",
                          [this](const QStringList &) { Tab_last(); });
    m_command_manager.reg("tab_next", "Switch to next tab",
                          [this](const QStringList &) { Tab_next(); });
    m_command_manager.reg("tab_prev", "Switch to previous tab",
                          [this](const QStringList &) { Tab_prev(); });
    m_command_manager.reg("tab_close", "Close current tab",
                          [this](const QStringList &) { Tab_close(); });
    m_command_manager.reg("tab_goto", "Go to tab by number",
                          [this](const QStringList &) { Tab_goto(); });
    m_command_manager.reg("tab_1", "Switch to tab 1",
                          [this](const QStringList &) { Tab_goto(1); });
    m_command_manager.reg("tab_2", "Switch to tab 2",
                          [this](const QStringList &) { Tab_goto(2); });
    m_command_manager.reg("tab_3", "Switch to tab 3",
                          [this](const QStringList &) { Tab_goto(3); });
    m_command_manager.reg("tab_4", "Switch to tab 4",
                          [this](const QStringList &) { Tab_goto(4); });
    m_command_manager.reg("tab_5", "Switch to tab 5",
                          [this](const QStringList &) { Tab_goto(5); });
    m_command_manager.reg("tab_6", "Switch to tab 6",
                          [this](const QStringList &) { Tab_goto(6); });
    m_command_manager.reg("tab_7", "Switch to tab 7",
                          [this](const QStringList &) { Tab_goto(7); });
    m_command_manager.reg("tab_8", "Switch to tab 8",
                          [this](const QStringList &) { Tab_goto(8); });
    m_command_manager.reg("tab_9", "Switch to tab 9",
                          [this](const QStringList &) { Tab_goto(9); });

    // Pickers
    m_command_manager.reg("picker_outline", "Open document outline picker",
                          [this](const QStringList &) { Show_outline(); });
    m_command_manager.reg("picker_highlight_search", "Search within highlights",
                          [this](const QStringList &)
    { Show_highlight_search(); });

    // Search
    m_command_manager.reg("search", "Search document",
                          [this](const QStringList &) { Search(); });
    m_command_manager.reg("search_regex", "Search document using regex",
                          [this](const QStringList &) { Search_regex(); });
    m_command_manager.reg("search_next", "Jump to next search result",
                          [this](const QStringList &) { NextHit(); });
    m_command_manager.reg("search_prev", "Jump to previous search result",
                          [this](const QStringList &) { PrevHit(); });
    m_command_manager.reg("search_args", "Search with inline query argument",
                          [this](const QStringList &args)
    { search(args.join(" ")); });

    // Layout modes
    m_command_manager.reg("layout_single", "Single page layout",
                          [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::SINGLE); });
    m_command_manager.reg("layout_left_to_right",
                          "Horizontal (left to right) layout",
                          [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::LEFT_TO_RIGHT); });
    m_command_manager.reg("layout_top_to_bottom",
                          "Vertical (top to bottom) layout",
                          [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::TOP_TO_BOTTOM); });
    m_command_manager.reg("layout_book", "Book (two page spread) layout",
                          [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::BOOK); });

    // Miscellaneous
    m_command_manager.reg("set_dpr", "Set device pixel ratio",
                          [this](const QStringList &) { SetDPR(); });
    m_command_manager.reg(
        "open_containing_folder", "Open folder containing current file",
        [this](const QStringList &) { OpenContainingFolder(); });
    m_command_manager.reg("undo", "Undo last action",
                          [this](const QStringList &) { Undo(); });
    m_command_manager.reg("redo", "Redo last undone action",
                          [this](const QStringList &) { Redo(); });
    m_command_manager.reg(
        "highlight_selection", "Highlight current text selection",
        [this](const QStringList &) { TextHighlightCurrentSelection(); });
    m_command_manager.reg("invert_color", "Toggle inverted colour rendering",
                          [this](const QStringList &) { InvertColor(); });
    m_command_manager.reg("reshow_jump_marker", "Re-show the last jump marker",
                          [this](const QStringList &)
    { Reshow_jump_marker(); });
    m_command_manager.reg("reopen_last_closed_file", "Reopen last closed file",
                          [this](const QStringList &)
    { Reopen_last_closed_file(); });
    m_command_manager.reg("copy_page_image", "Copy current page as image",
                          [this](const QStringList &) { Copy_page_image(); });
#ifndef NDEBUG
    m_command_manager.reg("debug_command", "Run debug command",
                          [this](const QStringList &) { debug_command(); });
#endif

    // Help / About
    m_command_manager.reg("show_startup_widget", "Show startup screen",
                          [this](const QStringList &) { showStartupWidget(); });
    m_command_manager.reg("show_tutorial_file", "Open tutorial document",
                          [this](const QStringList &) { showTutorialFile(); });
    m_command_manager.reg("show_about", "Show about dialog",
                          [this](const QStringList &) { ShowAbout(); });
}

// Trims the recent files store to `num_recent_files` number of files
void
Lektra::trimRecentFilesDatabase() noexcept
{
    // If num_recent_files config entry has negative value,
    // retain all the recent files
    if (m_config.behavior.num_recent_files < 0)
        return;

    m_recent_files_store.trim(m_config.behavior.num_recent_files);
    if (!m_recent_files_store.save())
        qWarning() << "Failed to trim recent files store";
}

// Sets the DPR of the current document
void
Lektra::SetDPR() noexcept
{
    if (m_doc)
    {
        QInputDialog id;
        bool ok;
        float dpr = id.getDouble(
            this, "Set DPR", "Enter the Device Pixel Ratio (DPR) value: ", 1.0,
            0.0, 10.0, 2, &ok);
        if (ok)
            m_doc->setDPR(dpr);
        else
            QMessageBox::critical(this, "Set DPR", "Invalid DPR value");
    }
}

// Reload the document in place
void
Lektra::reloadDocument() noexcept
{
    if (m_doc)
    {
        m_doc->reloadDocument();
    }
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
        index = QInputDialog::getInt(this, "Go to Tab", "Enter tab number: ", 1,
                                     1, m_tab_widget->count());
    }

    if (index > 0 || index < m_tab_widget->count())
        m_tab_widget->setCurrentIndex(index - 1);
    else
        m_message_bar->showMessage("Invalid Tab Number");
}

// Close the current tab
void
Lektra::Tab_close(int tabno) noexcept
{
    int indexToClose = (tabno == -1) ? m_tab_widget->currentIndex() : tabno;

    if (!validTabIndex(indexToClose))
        return;

    // Get the container
    DocumentContainer *container = m_tab_widget->rootContainer(indexToClose);
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
            = m_tab_widget->rootContainer(currentIndex);
        if (currentContainer)
        {
            setCurrentDocumentView(currentContainer->view());
        }
    }
    else
    {
        setCurrentDocumentView(nullptr);
        showStartupWidget();
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

// Useful for updating the Navigation QMenu
void
Lektra::updatePageNavigationActions() noexcept
{
    const int page  = m_doc ? m_doc->pageNo() : -1;
    const int count = m_doc ? m_doc->numPages() : 0;

    m_actionFirstPage->setEnabled(page > 0);
    m_actionPrevPage->setEnabled(page > 0);
    m_actionNextPage->setEnabled(page >= 0 && page < count - 1);
    m_actionLastPage->setEnabled(page >= 0 && page < count - 1);
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

// Update selection mode actions (QAction) in QMenu based on current
// selection mode
void
Lektra::updateSelectionModeActions() noexcept
{
    if (!m_doc)
        return;

    switch (m_doc->selectionMode())
    {
        case GraphicsView::Mode::RegionSelection:
            m_actionRegionSelect->setChecked(true);
            break;
        case GraphicsView::Mode::TextSelection:
            m_actionTextSelect->setChecked(true);
            break;
        case GraphicsView::Mode::TextHighlight:
            m_actionTextHighlight->setChecked(true);
            break;
        case GraphicsView::Mode::AnnotSelect:
            m_actionAnnotEdit->setChecked(true);
            break;
        case GraphicsView::Mode::AnnotRect:
            m_actionAnnotRect->setChecked(true);
            break;
        case GraphicsView::Mode::AnnotPopup:
            m_actionAnnotPopup->setChecked(true);
            break;
        default:
            break;
    }
}

void
Lektra::ToggleFocusMode() noexcept
{
    if (!m_doc)
        return;

    setFocusMode(!m_focus_mode);
}

void
Lektra::setFocusMode(bool enable) noexcept
{
    m_focus_mode = enable;

    if (m_focus_mode)
    {
        m_menuBar->setVisible(false);
        m_statusbar->setVisible(false);
        m_tab_widget->tabBar()->setVisible(false);
    }
    else
    {
        m_menuBar->setVisible(m_config.window.menubar);
        m_statusbar->setVisible(m_config.statusbar.visible);
        updateTabbarVisibility();
    }
}

void
Lektra::updateTabbarVisibility() noexcept
{
    // Let tab widget manage visibility itself based on auto-hide property
    m_tab_widget->tabBar()->setVisible(true); // initially show
    if (m_tab_widget->tabBarAutoHide() && m_tab_widget->count() < 2)
        m_tab_widget->tabBar()->setVisible(false);
}

void
Lektra::search(const QString &term) noexcept
{
    if (m_doc)
        m_doc->Search(term, false);
}

void
Lektra::searchInPage(const int pageno, const QString &term) noexcept
{
    if (m_doc)
        m_doc->SearchInPage(pageno, term);
}

void
Lektra::Search() noexcept
{
    if (m_doc)
    {
        m_search_bar->setVisible(true);
        m_search_bar->focusSearchInput();
    }
}

void
Lektra::Search_regex() noexcept
{
    if (m_doc)
    {
        m_search_bar->setVisible(true);
        m_search_bar->setRegexMode(true);
        m_search_bar->focusSearchInput();
    }
}

void
Lektra::setSessionName(const QString &name) noexcept
{
    m_session_name = name;
    m_statusbar->setSessionName(name);
}

void
Lektra::openSessionFromArray(const QJsonArray &sessionArray) noexcept
{
    for (const QJsonValue &val : sessionArray)
    {
        const QJsonObject tabObj     = val.toObject();
        const QJsonObject splitsNode = tabObj["splits"].toObject();

        // Legacy format — flat entry with file_path at top level
        if (splitsNode.isEmpty() && tabObj.contains("file_path"))
        {
            const QString filePath = tabObj["file_path"].toString();
            const int page         = tabObj["current_page"].toInt();
            const double zoom      = tabObj["zoom"].toDouble();
            const int fitMode      = tabObj["fit_mode"].toInt();
            const bool invert      = tabObj["invert_color"].toBool();

            if (filePath.isEmpty())
                continue;

            OpenFileInNewTab(filePath, [this, page, zoom, fitMode, invert]()
            {
                if (!m_doc)
                    return;
                if (invert)
                    m_doc->setInvertColor(true);
                m_doc->setFitMode(static_cast<DocumentView::FitMode>(fitMode));
                m_doc->setZoom(zoom);
                m_doc->GotoPage(page);
            });
            continue;
        }

        if (splitsNode.isEmpty())
            continue;

        // Recursive lambda to find the first file path in the splits tree
        // for this tab
        std::function<QString(const QJsonObject &)> firstFilePath
            = [&firstFilePath](const QJsonObject &node) -> QString
        {
            if (node["type"].toString() == "view")
                return node["file_path"].toString();

            const QJsonArray children = node["children"].toArray();
            for (const QJsonValue &child : children)
            {
                const QString path = firstFilePath(child.toObject());
                if (!path.isEmpty())
                    return path;
            }
            return {};
        };

        const QString startFile = firstFilePath(splitsNode);

        if (startFile.isEmpty())
            continue;

        OpenFileInNewTab(startFile, [this, splitsNode]()
        {
            int idx                      = m_tab_widget->currentIndex();
            DocumentContainer *container = m_tab_widget->rootContainer(idx);
            if (!container)
                return;

            DocumentView *rootView = container->view();
            if (!rootView)
                return;

            restoreSplitNode(container, rootView, splitsNode, nullptr);

            m_tab_widget->tabBar()->set_split_count(idx,
                                                    container->getViewCount());
        });
    }
}

void
Lektra::modeColorChangeRequested(const GraphicsView::Mode mode) noexcept
{
    // FIXME: Make this a function
    QColorDialog colorDialog(this);
    colorDialog.setOption(QColorDialog::ShowAlphaChannel, true);
    colorDialog.setWindowTitle("Select Color");
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

void
Lektra::ReselectLastTextSelection() noexcept
{
    if (m_doc)
        m_doc->ReselectLastTextSelection();
}

void
Lektra::SetLayoutMode(DocumentView::LayoutMode mode) noexcept
{
    if (m_doc)
        m_doc->setLayoutMode(mode);
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
        return;
    }
}

void
Lektra::Show_command_picker() noexcept
{
    if (!m_command_picker)
    {
        m_command_picker = new CommandPicker(m_config.command_palette,
                                             m_command_manager.commands(),
                                             m_config.shortcuts, this);
        m_command_picker->setKeybindings(m_picker_keybinds);
    }
    m_command_picker->launch();
}

#ifdef ENABLE_LLM_SUPPORT
void
Lektra::ToggleLLMWidget() noexcept
{
    m_llm_widget->setVisible(!m_llm_widget->isVisible());
}
#endif

void
Lektra::showTutorialFile() noexcept
{
#if defined(__linux__)
    const QString doc_path = QString("%1%2")
                                 .arg(APP_INSTALL_PREFIX)
                                 .arg("/share/doc/Lektra/tutorial.pdf");
    OpenFileInNewTab(doc_path);
#elif defined(__APPLE__) && defined(__MACH__)
    QMessageBox::warning(this, "Show Tutorial File",
                         "Not yet implemented for Macintosh");
#elif defined(_WIN64)
    QMessageBox::warning(this, "Show Tutorial File",
                         "Not yet implemented for Windows");
#endif
}

// void
// Lektra::SetMark() noexcept
// {
//     m_message_bar->showMessage("**SetMark**, Waiting for mark: ", -1);
// }

// void
// Lektra::GotoMark() noexcept
// {
//     m_message_bar->showMessage("**GotoMark**, Waiting for mark: ", -1);
//     // Wait for key input
// }

// void
// Lektra::DeleteMark() noexcept
// {
//     m_message_bar->showMessage("**GotoMark**, Waiting for mark: ", -1);
// }

// void
// Lektra::setMark(const QString &key, const int pageno,
//               const DocumentView::PageLocation location) noexcept
// {
// }

// void
// Lektra::gotoMark(const QString &key) noexcept
// {
// }

// void
// Lektra::deleteMark(const QString &key) noexcept
// {
// }

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

DocumentContainer *
Lektra::VSplit() noexcept
{
    int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return nullptr;

    // Get the container for this tab
    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
    if (!container)
        return nullptr;

    DocumentView *currentView = container->view();
    if (!currentView)
        return nullptr;

    // Perform vertical split (top/bottom)
    container->split(currentView, Qt::Vertical);
    m_tab_widget->tabBar()->set_split_count(currentTabIndex,
                                            container->getViewCount());
    return container;
}

DocumentContainer *
Lektra::HSplit() noexcept
{
    int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return nullptr;

    // Get the container for this tab
    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
    if (!container)
        return nullptr;

    DocumentView *currentView = container->view();
    if (!currentView)
        return nullptr;

    // Perform horizontal split (left/right)
    container->split(currentView, Qt::Horizontal);
    m_tab_widget->tabBar()->set_split_count(currentTabIndex,
                                            container->getViewCount());

    return container;
}

// Closes all splits except the current one in the current tab. If there is
// only one split, does nothing.
void
Lektra::Close_other_splits() noexcept
{
    const int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
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
Lektra::Close_split() noexcept
{
    const int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
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

void
Lektra::setCurrentDocumentView(DocumentView *view) noexcept
{
    if (!view || m_doc == view)
        return;

    if (m_doc)
        m_doc->setActive(false);
    view->setActive(true);

    m_doc = view;

    const int tabIndex = m_tab_widget->currentIndex();

    DocumentContainer *container = m_tab_widget->rootContainer(tabIndex);
    if (!container)
        return;

    m_tab_widget->tabBar()->setTabText(tabIndex, m_config.tabs.full_path
                                                     ? m_doc->filePath()
                                                     : m_doc->fileName());
    updateUiEnabledState();
    updatePageNavigationActions();
    updatePanel();
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
Lektra::CloseFile() noexcept
{
    if (m_doc)
    {
        int indexToClose = m_tab_widget->currentIndex();
        Tab_close(indexToClose);
    }
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

    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
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
                         std::function<void()> onAllDone) noexcept
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
            = [page, zoom, fitMode, invert, onAllDone](DocumentView *doc)
        {
            doc->setFitMode(static_cast<DocumentView::FitMode>(fitMode));
            doc->setZoom(zoom);
            doc->GotoPage(page - 1);
            if (invert)
                doc->setInvertColor(true);
            if (onAllDone)
                onAllDone();
        };

        if (path.isEmpty())
        {
            if (onAllDone)
                onAllDone();
            return;
        }

        // If this view already has the right file loaded, just apply state
        if (targetView->filePath() == path)
        {
            applyState(targetView);
            return;
        }

        targetView->openAsync(path);

        connect(targetView, &DocumentView::openFileFinished, this,
                [applyState](DocumentView *doc) { applyState(doc); },
                Qt::SingleShotConnection);

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
            if (onAllDone)
                onAllDone();
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

            restoreSplitNode(container, pane, child, [remaining, onAllDone]()
            {
                --(*remaining);
                if (*remaining == 0 && onAllDone)
                    onAllDone();
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
        DocumentContainer *container = m_tab_widget->rootContainer(i);
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
    DocumentView::PageLocation target{
        linkItem->gotoPageNo(), linkItem->location().x, linkItem->location().y};

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
            [newView, target](DocumentView *)
    {
        QTimer::singleShot(0, newView, [newView, target]()
        { newView->GotoLocation(target); });
    }, Qt::SingleShotConnection);
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

    DocumentView *newView = OpenFileVSplit(
        filePath.isEmpty() ? sourceView->filePath() : filePath);
    if (!newView)
        return nullptr;

    sourceView->set_portal(newView);
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
            pair->source->clear_portal();
            m_statusbar->setPortalMode(false);
        }
        PPRINT("PORTAL CLOSED");
    }, Qt::SingleShotConnection);

    return newView;
}

DocumentView *
Lektra::get_view_by_id(const DocumentView::Id id) const noexcept
{
    for (int i = 0; i < m_tab_widget->count(); ++i)
    {
        DocumentContainer *container = m_tab_widget->rootContainer(i);
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

// If a jump marker was shown for the current document view, re-show it
// (e.g. after a reload)
void
Lektra::Reshow_jump_marker() noexcept
{
    if (m_doc)
        m_doc->Reshow_jump_marker();
}

void
Lektra::Toggle_presentation_mode() noexcept
{
    if (!m_doc)
        return;

    // TODO: Implement presentation mode (probably just a special
    // full-screen mode with extra UI optimizations for it)
}

// Show a picker with the list of recent files from the recent files store,
// and allow the user to open a file from there.
void
Lektra::Show_recent_files_picker() noexcept
{
    const auto &entries = m_recent_files_store.entries();

    if (entries.empty())
    {
        QMessageBox::information(this, "Recent Files",
                                 "No recent files found.");
        return;
    }

    const QStringList recentFiles = m_recent_files_store.files();

    if (!m_recent_file_picker)
    {
        m_recent_file_picker = new RecentFilesPicker(this);
        m_recent_file_picker->setRecentFiles(recentFiles);
        m_recent_file_picker->setKeybindings(m_picker_keybinds);

        connect(m_recent_file_picker, &RecentFilesPicker::fileRequested, this,
                [this](const QString &file) { OpenFileInNewTab(file); });
    }

    m_recent_file_picker->launch();
}

#ifndef NDEBUG
void
Lektra::debug_command() noexcept
{
    m_message_bar->showMessage("TEST MESSAGE");
}
#endif

void
Lektra::Copy_page_image() noexcept
{
    if (!m_doc)
        return;

    m_doc->Copy_page_image();
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
        qWarning() << "Reopen_last_file: file no longer exists:"
                   << target->file_path;
        return;
    }

    const int savedPage  = target->page_number;
    const QString &fpath = target->file_path;

    OpenFileInNewTab(fpath, [this, savedPage]() { gotoPage(savedPage); });
}

void
Lektra::SetMark() noexcept
{
    if (!m_doc)
        return;

    const QString key = QInputDialog::getText(
        this, "Set Mark", "Enter mark key (a-z for local, A-Z for global):");

    if (key.isEmpty())
    {
        QMessageBox::critical(this, "Set Mark", "Mark key cannot be empty");
        return;
    }

    if (m_marks_manager->isGlobalKey(key))
        m_marks_manager->addGlobalMark(key, m_doc->id(),
                                       m_doc->CurrentLocation());
    else
        m_marks_manager->addLocalMark(key, m_doc->id(),
                                      m_doc->CurrentLocation());
}

void
Lektra::DeleteMark() noexcept
{
    if (!m_doc)
        return;

    const QStringList existingMarks = m_marks_manager->allKeys(m_doc->id());
    const QString key               = QInputDialog::getItem(
        this, "Delete Mark", "Mark to delete:", existingMarks, 0);

    if (key.isEmpty())
    {
        QMessageBox::critical(this, "Delete Mark", "Mark key cannot be empty");
        return;
    }

    if (m_marks_manager->isGlobalKey(key))
    {
        m_marks_manager->removeGlobalMark(key);
    }
    else
    {
        m_marks_manager->removeLocalMark(key, m_doc->id());
    }
}

void
Lektra::GotoMark() noexcept
{
    if (!m_doc)
        return;

    const QStringList existingMarks = m_marks_manager->allKeys(m_doc->id());
    const QString key               = QInputDialog::getItem(
        this, "Goto Mark", "Mark to go to:", existingMarks, 0);

    if (key.isEmpty())
    {
        QMessageBox::critical(this, "Goto Mark", "Mark key cannot be empty");
        return;
    }

    if (m_marks_manager->isGlobalKey(key))
    {
        const auto *mark = m_marks_manager->getGlobalMark(key);
        if (!mark)
            return;
        // Switch to the right document first, then jump
        DocumentView *view = get_view_by_id(mark->docId);
        if (view)
        {
            setCurrentDocumentView(view);
            view->GotoLocationWithHistory(mark->plocation);
        }
    }
    else
    {
        const auto *mark = m_marks_manager->getLocalMark(key, m_doc->id());
        if (!mark)
            return;
        m_doc->GotoLocationWithHistory(mark->plocation);
    }
}

void
Lektra::Toggle_visual_line_mode() noexcept
{
    if (!m_doc)
        return;

    bool newState = !m_doc->visual_line_mode();
    m_doc->set_visual_line_mode(newState);

    if (m_doc->visual_line_mode())
        m_statusbar->setMode(GraphicsView::Mode::VisualLine);
    else
        m_statusbar->setMode(m_doc->graphicsView()->getDefaultMode());
}
