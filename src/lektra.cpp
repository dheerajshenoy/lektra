#include "lektra.hpp"

#include "AboutDialog.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
#include "EditLastPagesWidget.hpp"
#include "FloatingOverlayWidget.hpp"
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
set_if_present(toml::node_view<toml::node> node, T &target)
{
    if (auto v = node.value<T>())
        target = *v;
}

static inline void
set_qstring_if_present(toml::node_view<toml::node> n, QString &dst)
{
    if (auto v = n.value<std::string>())
        dst = QString::fromStdString(*v);
}

static inline void
set_color_if_present(toml::node_view<toml::node> n, uint32_t &dst)
{
    if (auto s = n.value<std::string>())
    {
        uint32_t tmp = dst;
        if (parseHexColor(*s, tmp))
            dst = tmp;
    }
}

} // namespace

// Constructs the `lektra` class
lektra::lektra() noexcept
{
    setAttribute(Qt::WA_NativeWindow); // This is necessary for DPI updates
    setAcceptDrops(true);
    installEventFilter(this);
}

lektra::lektra(const QString &sessionName,
               const QJsonArray &sessionArray) noexcept
{
    setAttribute(Qt::WA_NativeWindow); // This is necessary for DPI updates
    setAcceptDrops(true);
    installEventFilter(this);
    construct();
    openSessionFromArray(sessionArray);
    setSessionName(sessionName);
    m_statusbar->setSessionName(sessionName);
}

// On-demand construction of `lektra` (for use with argparse)
void
lektra::construct() noexcept
{
    m_tab_widget     = new TabWidget();
    m_config_watcher = new QFileSystemWatcher(this);

    initActionMap();
    initConfig();
    initGui();
    updateGUIFromConfig();
    if (m_load_default_keybinding)
        initDefaultKeybinds();
    initMenubar();
    warnShortcutConflicts();
    initDB();
    trimRecentFilesDatabase();
    populateRecentFiles();
    initConnections();
    updateUiEnabledState();
    setMinimumSize(200, 150);
    this->show();
    resize(m_config.window.initial_size[0], m_config.window.initial_size[1]);
}

// Initialize the menubar related stuff
void
lektra::initMenubar() noexcept
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
                              this, &lektra::FileProperties);

    m_actionOpenContainingFolder = fileMenu->addAction(
        QString("Open Containing Folder\t%1")
            .arg(m_config.shortcuts["open_containing_folder"]),
        this, &lektra::OpenContainingFolder);
    m_actionOpenContainingFolder->setEnabled(false);

    m_actionSaveFile = fileMenu->addAction(
        QString("Save File\t%1").arg(m_config.shortcuts["file_save"]), this,
        &lektra::SaveFile);

    m_actionSaveAsFile = fileMenu->addAction(
        QString("Save As File\t%1").arg(m_config.shortcuts["file_save_as"]),
        this, &lektra::SaveAsFile);

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
        &lektra::Undo);
    m_actionRedo = editMenu->addAction(
        QString("Redo\t%1").arg(m_config.shortcuts["redo"]), this,
        &lektra::Redo);
    m_actionUndo->setEnabled(false);
    m_actionRedo->setEnabled(false);
    editMenu->addAction(
        QString("Last Pages\t%1").arg(m_config.shortcuts["edit_last_pages"]),
        this, &lektra::editLastPages);

    // --- View Menu ---
    m_viewMenu         = m_menuBar->addMenu("&View");
    m_actionFullscreen = m_viewMenu->addAction(
        QString("Fullscreen\t%1").arg(m_config.shortcuts["toggle_fullscreen"]),
        this, &lektra::ToggleFullscreen);
    m_actionFullscreen->setCheckable(true);
    m_actionFullscreen->setChecked(m_config.window.fullscreen);

    m_actionZoomIn = m_viewMenu->addAction(
        QString("Zoom In\t%1").arg(m_config.shortcuts["zoom_in"]), this,
        &lektra::ZoomIn);
    m_actionZoomOut = m_viewMenu->addAction(
        QString("Zoom Out\t%1").arg(m_config.shortcuts["zoom_out"]), this,
        &lektra::ZoomOut);

    m_viewMenu->addSeparator();

    m_fitMenu = m_viewMenu->addMenu("Fit");

    m_actionFitWidth = m_fitMenu->addAction(
        QString("Width\t%1").arg(m_config.shortcuts["fit_width"]), this,
        &lektra::Fit_width);

    m_actionFitHeight = m_fitMenu->addAction(
        QString("Height\t%1").arg(m_config.shortcuts["fit_height"]), this,
        &lektra::Fit_height);

    m_actionFitWindow = m_fitMenu->addAction(
        QString("Page\t%1").arg(m_config.shortcuts["fit_page"]), this,
        &lektra::Fit_page);

    m_fitMenu->addSeparator();

    // Auto Resize toggle (independent)
    m_actionAutoresize = m_viewMenu->addAction(
        QString("Auto Fit\t%1").arg(m_config.shortcuts["fit_auto"]), this,
        &lektra::ToggleAutoResize);
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

    layoutActionGroup->addAction(m_actionLayoutSingle);
    layoutActionGroup->addAction(m_actionLayoutLeftToRight);
    layoutActionGroup->addAction(m_actionLayoutTopToBottom);

    m_actionLayoutSingle->setCheckable(true);
    m_actionLayoutLeftToRight->setCheckable(true);
    m_actionLayoutTopToBottom->setCheckable(true);
    m_actionLayoutSingle->setChecked(m_config.layout.mode
                                     == DocumentView::LayoutMode::SINGLE);

    m_actionLayoutLeftToRight->setChecked(
        m_config.layout.mode == DocumentView::LayoutMode::LEFT_TO_RIGHT);
    m_actionLayoutTopToBottom->setChecked(
        m_config.layout.mode == DocumentView::LayoutMode::TOP_TO_BOTTOM);

    // --- Toggle Menu ---

    m_viewMenu->addSeparator();
    m_toggleMenu = m_viewMenu->addMenu("Show/Hide");

#ifdef ENABLE_LLM_SUPPORT
    m_actionToggleLLMWidget = m_toggleMenu->addAction(
        QString("LLM Widget\t%1").arg(m_config.shortcuts["toggle_llm_widget"]),
        this, &lektra::ToggleLLMWidget);
    m_actionToggleLLMWidget->setCheckable(true);
    m_actionToggleLLMWidget->setChecked(m_config.llm_widget.visible);
#endif

    m_actionCommandPicker = m_toggleMenu->addAction(
        QString("Command Picker\t%1").arg(m_config.shortcuts["command_picker"]),
        this, &lektra::Show_command_picker);

    m_actionToggleOutline = m_toggleMenu->addAction(
        QString("Outline\t%1").arg(m_config.shortcuts["picker_outline"]), this,
        &lektra::Show_outline);
    m_actionToggleOutline->setCheckable(true);
    m_actionToggleOutline->setChecked(m_outline_picker
                                      && !m_outline_picker->isHidden());

    m_actionToggleHighlightAnnotSearch = m_toggleMenu->addAction(
        QString("Highlight Annotation Search\t%1")
            .arg(m_config.shortcuts["picker_highlight_search"]),
        this, &lektra::Show_highlight_search);
    m_actionToggleHighlightAnnotSearch->setCheckable(true);
    m_actionToggleHighlightAnnotSearch->setChecked(
        m_highlight_search_picker && !m_highlight_search_picker->isHidden());

    m_actionToggleMenubar = m_toggleMenu->addAction(
        QString("Menubar\t%1").arg(m_config.shortcuts["toggle_menubar"]), this,
        &lektra::ToggleMenubar);
    m_actionToggleMenubar->setCheckable(true);
    m_actionToggleMenubar->setChecked(!m_menuBar->isHidden());

    m_actionToggleTabBar = m_toggleMenu->addAction(
        QString("Tabs\t%1").arg(m_config.shortcuts["toggle_tabs"]), this,
        &lektra::ToggleTabBar);
    m_actionToggleTabBar->setCheckable(true);
    m_actionToggleTabBar->setChecked(!m_tab_widget->tabBar()->isHidden());

    m_actionTogglePanel = m_toggleMenu->addAction(
        QString("Statusbar\t%1").arg(m_config.shortcuts["toggle_statusbar"]),
        this, &lektra::TogglePanel);
    m_actionTogglePanel->setCheckable(true);
    m_actionTogglePanel->setChecked(!m_statusbar->isHidden());

    m_actionInvertColor = m_viewMenu->addAction(
        QString("Invert Color\t%1").arg(m_config.shortcuts["invert_color"]),
        this, &lektra::InvertColor);
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
        this, &lektra::ToggleRegionSelect);
    m_actionRegionSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionRegionSelect);

    m_actionTextSelect = m_modeMenu->addAction(
        QString("Text Selection\t%1")
            .arg(m_config.shortcuts["selection_mode_text"]),
        this, &lektra::ToggleTextSelection);
    m_actionTextSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionTextSelect);

    m_actionTextHighlight = m_modeMenu->addAction(
        QString("Text Highlight\t%1")
            .arg(m_config.shortcuts["annot_highlight_mode"]),
        this, &lektra::ToggleTextHighlight);
    m_actionTextHighlight->setCheckable(true);
    modeActionGroup->addAction(m_actionTextHighlight);

    m_actionAnnotRect
        = m_modeMenu->addAction(QString("Annotate Rectangle\t%1")
                                    .arg(m_config.shortcuts["annot_rect_mode"]),
                                this, &lektra::ToggleAnnotRect);
    m_actionAnnotRect->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotRect);

    m_actionAnnotEdit
        = m_modeMenu->addAction(QString("Edit Annotations\t%1")
                                    .arg(m_config.shortcuts["annot_edit_mode"]),
                                this, &lektra::ToggleAnnotSelect);
    m_actionAnnotEdit->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotEdit);

    m_actionAnnotPopup = m_modeMenu->addAction(
        QString("Annotate Popup\t%1")
            .arg(m_config.shortcuts["annot_popup_mode"]),
        this, &lektra::ToggleAnnotPopup);
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
        this, &lektra::EncryptDocument);
    m_actionEncrypt->setEnabled(false);

    m_actionDecrypt = toolsMenu->addAction(
        QString("Decrypt Document\t%1").arg(m_config.shortcuts["file_decrypt"]),
        this, &lektra::DecryptDocument);
    m_actionDecrypt->setEnabled(false);

    // --- Navigation Menu ---
    m_navMenu = m_menuBar->addMenu("&Navigation");

    m_navMenu->addAction(
        QString("StartPage\t%1").arg(m_config.shortcuts["show_startup_widget"]),
        this, &lektra::showStartupWidget);

    m_actionGotoPage = m_navMenu->addAction(
        QString("Goto Page\t%1").arg(m_config.shortcuts["page_goto"]), this,
        &lektra::Goto_page);

    m_actionFirstPage = m_navMenu->addAction(
        QString("First Page\t%1").arg(m_config.shortcuts["page_first"]), this,
        &lektra::FirstPage);

    m_actionPrevPage = m_navMenu->addAction(
        QString("Previous Page\t%1").arg(m_config.shortcuts["page_prev"]), this,
        &lektra::PrevPage);

    m_actionNextPage = m_navMenu->addAction(
        QString("Next Page\t%1").arg(m_config.shortcuts["page_next"]), this,
        &lektra::NextPage);
    m_actionLastPage = m_navMenu->addAction(
        QString("Last Page\t%1").arg(m_config.shortcuts["page_last"]), this,
        &lektra::LastPage);

    m_actionPrevLocation
        = m_navMenu->addAction(QString("Previous Location\t%1")
                                   .arg(m_config.shortcuts["location_prev"]),
                               this, &lektra::GoBackHistory);
    m_actionNextLocation = m_navMenu->addAction(
        QString("Next Location\t%1").arg(m_config.shortcuts["location_next"]),
        this, &lektra::GoForwardHistory);

    // QMenu *markMenu = m_navMenu->addMenu("Marks");

    // m_actionSetMark = markMenu->addAction(
    //     QString("Set Mar\t%1").arg(m_config.shortcuts["set_mark"]), this,
    //     &lektra::SetMark);

    /* Help Menu */
    QMenu *helpMenu = m_menuBar->addMenu("&Help");
    m_actionAbout   = helpMenu->addAction(
        QString("About\t%1").arg(m_config.shortcuts["show_about"]), this,
        &lektra::ShowAbout);

    m_actionShowTutorialFile = helpMenu->addAction(
        QString("Open Tutorial File\t%1")
            .arg(m_config.shortcuts["show_tutorial_file"]),
        this, &lektra::showTutorialFile);
}

// Initialize the recent files store
void
lektra::initDB() noexcept
{
    m_recent_files_path = m_config_dir.filePath("last_pages.json");
    m_recent_files_store.setFilePath(m_recent_files_path);
    if (!m_recent_files_store.load())
        qWarning() << "Failed to load recent files store";
}

// Initialize the config related stuff
void
lektra::initConfig() noexcept
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

    /* tabs */
    auto ui_tabs = toml["tabs"];
    set_if_present(ui_tabs["visible"], m_config.tabs.visible);
    set_if_present(ui_tabs["auto_hide"], m_config.tabs.auto_hide);
    set_if_present(ui_tabs["closable"], m_config.tabs.closable);
    set_if_present(ui_tabs["movable"], m_config.tabs.movable);
    if (auto str = ui_tabs["elide_mode"])
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

    if (auto str = ui_tabs["location"])
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
    set_if_present(ui_tabs["full_path"], m_config.tabs.full_path);
    set_if_present(ui_tabs["lazy_load"], m_config.tabs.lazy_load);

    /* window */
    auto ui_window = toml["window"];
    set_if_present(ui_window["startup_tab"], m_config.window.startup_tab);
    set_if_present(ui_window["menubar"], m_config.window.menubar);
    set_if_present(ui_window["fullscreen"], m_config.window.fullscreen);

    if (ui_window["initial_size"].is_table())
    {
        int width{600}, height{400};

        const auto &size_table = *ui_window["initial_size"].as_table();

        if (auto toml_width = size_table["width"].value<int>())
            width = *toml_width;
        if (auto toml_height = size_table["height"].value<int>())
            height = *toml_height;

        if (width > 0 && height > 0 {
            m_config.window.initial_size = {width, height};
        }
    }

    if (m_config.window.fullscreen)
        this->showFullScreen();

    // Only override title format if key exists
    set_title_format_if_present(ui_window["window_title"],
                                m_config.window.title_format);

    /* statusbar */
    auto ui_statusbar = toml["statusbar"];
    set_if_present(ui_statusbar["visible"], m_config.statusbar.visible);

    if (auto padding_array = ui_statusbar["padding"].as_array();
        padding_array && padding_array->size() >= 4)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (auto v
                = padding_array->get(static_cast<size_t>(i))->value<int>())
                m_config.statusbar.padding[i] = *v;
        }
    }

    set_if_present(ui_statusbar["show_progress"],
                   m_config.statusbar.show_progress);
    set_if_present(ui_statusbar["file_name_only"],
                   m_config.statusbar.file_name_only);
    set_if_present(ui_statusbar["show_file_info"],
                   m_config.statusbar.show_file_info);
    set_if_present(ui_statusbar["show_page_number"],
                   m_config.statusbar.show_page_number);
    set_if_present(ui_statusbar["show_mode"], m_config.statusbar.show_mode);
    set_if_present(ui_statusbar["show_session_name"],
                   m_config.statusbar.show_session_name);

    /* layout */
    auto ui_layout = toml["layout"];

    if (auto str = ui_layout["mode"])
    {
        DocumentView::LayoutMode mode;

        if (str == "top_to_bottom")
            mode = DocumentView::LayoutMode::TOP_TO_BOTTOM;
        else if (str == "single")
            mode = DocumentView::LayoutMode::SINGLE;
        else if (str == "left_to_right")
            mode = DocumentView::LayoutMode::LEFT_TO_RIGHT;
        else
            mode = DocumentView::LayoutMode::TOP_TO_BOTTOM;

        m_config.layout.mode = mode;
    }
    if (auto str = ui_layout["initial_fit"])
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
    set_if_present(ui_layout["auto_resize"], m_config.layout.auto_resize);
    set_if_present(ui_layout["spacing"], m_config.layout.spacing);

    /* zoom */
    auto ui_zoom = toml["zoom"];
    set_if_present(ui_zoom["level"], m_config.zoom.level);
    set_if_present(ui_zoom["factor"], m_config.zoom.factor);

    /* selection */
    auto ui_selection = toml["selection"];
    set_if_present(ui_selection["drag_threshold"],
                   m_config.selection.drag_threshold);

    /* scrollbars */
    auto ui_scrollbars = toml["scrollbars"];
    set_if_present(ui_scrollbars["vertical"], m_config.scrollbars.vertical);
    set_if_present(ui_scrollbars["horizontal"], m_config.scrollbars.horizontal);
    set_if_present(ui_scrollbars["search_hits"],
                   m_config.scrollbars.search_hits);
    set_if_present(ui_scrollbars["auto_hide"], m_config.scrollbars.auto_hide);
    set_if_present(ui_scrollbars["size"], m_config.scrollbars.size);
    set_if_present(ui_scrollbars["hide_timeout"],
                   m_config.scrollbars.hide_timeout);

    /* command_palette */
    auto command_palette = toml["command_palette"];
    set_if_present(command_palette["height"], m_config.command_palette.height);
    set_if_present(command_palette["width"], m_config.command_palette.width);
    set_if_present(command_palette["vscrollbar"],
                   m_config.command_palette.vscrollbar);
    set_if_present(command_palette["show_grid"],
                   m_config.command_palette.show_grid);
    set_if_present(command_palette["show_shortcuts"],
                   m_config.command_palette.show_shortcuts);

    set_qstring_if_present(command_palette["placeholder_text"],
                           m_config.command_palette.placeholder_text);

    /* overlays */
    auto picker = toml["picker"];
    set_if_present(picker["border"], m_config.picker.border);

    auto picker_shadow = picker["shadow"];
    set_if_present(picker_shadow["enabled"], m_config.picker.shadow.enabled);
    set_if_present(picker_shadow["blur_radius"],
                   m_config.picker.shadow.blur_radius);
    set_if_present(picker_shadow["offset_x"], m_config.picker.shadow.offset_x);
    set_if_present(picker_shadow["offset_y"], m_config.picker.shadow.offset_y);
    set_if_present(picker_shadow["opacity"], m_config.picker.shadow.opacity);

    auto picker_keys = picker["keys"];

    // Member: m_pickers

    if (picker_keys.is_table())
    {
        const auto &keys = *picker_keys.as_table();
        const auto get   = [&](std::string_view field, QKeyCombination fallback)
        {
            const auto *node = keys.get(field);
            if (!node || !node->is_string())
                return fallback;
            const auto seq = QKeySequence::fromString(
                QString::fromStdString(std::string(node->as_string()->get())),
                QKeySequence::PortableText);
            return seq.isEmpty() ? fallback : seq[0];
        };

        m_picker_keybinds = Picker::Keybindings{
            .moveDown = get("down", Picker::Keybindings{}.moveDown),
            .pageDown = get("page_down", Picker::Keybindings{}.pageDown),
            .moveUp   = get("up", Picker::Keybindings{}.moveUp),
            .pageUp   = get("page_up", Picker::Keybindings{}.pageUp),
            .accept   = get("accept", Picker::Keybindings{}.accept),
            .dismiss  = get("dismiss", Picker::Keybindings{}.dismiss),
        };
    }

    /* markers */
    auto ui_markers = toml["markers"];
    set_if_present(ui_markers["jump_marker"], m_config.markers.jump_marker);

    /* links + link_hints */
    auto ui_links      = toml["links"];
    auto ui_link_hints = toml["link_hints"];

    set_if_present(ui_links["boundary"], m_config.links.boundary);
    set_if_present(ui_links["detect_urls"], m_config.links.detect_urls);
    set_qstring_if_present(ui_links["url_regex"], m_config.links.url_regex);
    set_if_present(ui_link_hints["size"], m_config.link_hints.size);

    /* outline */
    auto ui_outline = toml["outline"];

    /* highlight_search */
    auto ui_highlight_search = toml["highlight_search"];

#ifdef ENABLE_LLM_SUPPORT
    auto llm_widget = toml["llm_widget"];
    set_qstring_if_present(llm_widget["panel_position"],
                           m_config.llm_widget.panel_position);
    set_if_present(llm_widget["panel_width"], m_config.llm_widget.panel_width);
    set_if_present(llm_widget["visible"], m_config.llm_widget.visible);

    auto llm = toml["llm"];
    set_if_present(llm["provider"], m_config.llm.provider);
    set_if_present(llm["model"], m_config.llm.model);
    set_if_present(llm["max_tokens"], m_config.llm.max_tokens);
#endif

    /* colors */
    auto colors = toml["colors"];
    set_color_if_present(colors["accent"], m_config.colors.accent);
    set_color_if_present(colors["background"], m_config.colors.background);
    set_color_if_present(colors["search_match"], m_config.colors.search_match);
    set_color_if_present(colors["search_index"], m_config.colors.search_index);
    set_color_if_present(colors["link_hint_bg"], m_config.colors.link_hint_bg);
    set_color_if_present(colors["link_hint_fg"], m_config.colors.link_hint_fg);
    set_color_if_present(colors["selection"], m_config.colors.selection);
    set_color_if_present(colors["highlight"], m_config.colors.highlight);
    set_color_if_present(colors["jump_marker"], m_config.colors.jump_marker);
    set_color_if_present(colors["annot_rect"], m_config.colors.annot_rect);
    set_color_if_present(colors["annot_popup"], m_config.colors.annot_popup);
    set_color_if_present(colors["page_background"],
                         m_config.colors.page_background);
    set_color_if_present(colors["page_foreground"],
                         m_config.colors.page_foreground);

    /* rendering */
    auto rendering = toml["rendering"];
    set_if_present(rendering["dpi"], m_config.rendering.dpi);

    if (m_config.rendering.dpi <= 72.0f)
        m_config.rendering.dpi = 72.0f; // minimum DPI is 72

    set_if_present(rendering["antialiasing"], m_config.rendering.antialiasing);
    set_if_present(rendering["text_antialiasing"],
                   m_config.rendering.text_antialiasing);
    set_if_present(rendering["smooth_pixmap_transform"],
                   m_config.rendering.smooth_pixmap_transform);
    set_if_present(rendering["antialiasing_bits"],
                   m_config.rendering.antialiasing_bits);
    set_if_present(rendering["icc_color_profile"],
                   m_config.rendering.icc_color_profile);

    // If DPR is specified in config, use that (can be scalar or map)
    if (rendering["dpr"])
    {
        if (rendering["dpr"].is_value())
        {
            if (auto v = rendering["dpr"].value<float>())
            {
                m_config.rendering.dpr                                     = *v;
                m_screen_dpr_map[QGuiApplication::primaryScreen()->name()] = *v;
            }
        }
        else if (rendering["dpr"].is_table())
        {
            // Only build a map if table exists; else leave default
            auto dpr_table = rendering["dpr"];
            if (auto t = dpr_table.as_table())
            {
                // Start from current map (if you want table to "add/override")
                // or clear it (if you want table to "replace").
                // Here: replace, because that's what your old code effectively
                // did.
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

    auto split = toml["split"];

    set_if_present(split["focus_follows_mouse"],
                   m_config.split.focus_follows_mouse);
    set_if_present(split["dim_inactive"], m_config.split.dim_inactive);
    set_if_present(split["dim_inactive_opacity"],
                   m_config.split.dim_inactive_opacity);

    auto behavior = toml["behavior"];

#ifdef HAS_SYNCTEX
    set_qstring_if_present(behavior["synctex_editor_command"],
                           m_config.behavior.synctex_editor_command);
#endif

    set_if_present(behavior["preload_pages"], m_config.behavior.preload_pages);
    set_if_present(behavior["confirm_on_quit"],
                   m_config.behavior.confirm_on_quit);
    set_if_present(behavior["undo_limit"], m_config.behavior.undo_limit);
    set_if_present(behavior["remember_last_visited"],
                   m_config.behavior.remember_last_visited);
    set_if_present(behavior["always_open_in_new_window"],
                   m_config.behavior.always_open_in_new_window);
    set_if_present(behavior["page_history"],
                   m_config.behavior.page_history_limit);
    set_if_present(behavior["invert_mode"], m_config.behavior.invert_mode);
    set_if_present(behavior["auto_reload"], m_config.behavior.auto_reload);
    set_if_present(behavior["config_auto_reload"],
                   m_config.behavior.config_auto_reload);
    set_if_present(behavior["recent_files"], m_config.behavior.recent_files);
    set_if_present(behavior["num_recent_files"],
                   m_config.behavior.num_recent_files);
    set_if_present(behavior["cache_pages"], m_config.behavior.cache_pages);

    if (toml.contains("keybindings"))
    {
        m_load_default_keybinding = false;
        auto keys                 = toml["keybindings"];

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
lektra::initDefaultKeybinds() noexcept
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
        {"undo", "u"},
        {"redo", "Ctrl+r"},
        {"invert_color", "i"},
        {"toggle_menubar", "Ctrl+Shift+m"},
        {"toggle_command_palette", "Ctrl+Shift+P"},
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
lektra::warnShortcutConflicts() noexcept
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
    if (m_message_bar)
        m_message_bar->showMessage(message, 6.0f);
}

// Initialize the GUI related Stuff
void
lektra::initGui() noexcept
{
    QWidget *widget = new QWidget();
    m_layout        = new QVBoxLayout();
    m_layout->setContentsMargins(0, 0, 0, 0);

    // Panel
    m_statusbar = new Statusbar(m_config, this);
    m_statusbar->hidePageInfo(true);
    m_statusbar->setMode(GraphicsView::Mode::TextSelection);
    m_statusbar->setSessionName("");

    m_search_bar = new SearchBar(this);
    // m_search_bar_overlay = new FloatingOverlayWidget(this);

    m_search_bar->setVisible(false);

    // m_search_bar_overlay->setFrameStyle(makeOverlayFrameStyle(m_config));
    // m_search_bar_overlay->setContentWidget(m_outline_widget);
    // m_search_bar_overlay->setContentWidget(m_search_bar);
    // m_search_bar_overlay->setVisible(false);

    m_message_bar = new MessageBar(this);
    m_message_bar->setVisible(false);

    widget->setLayout(m_layout);
    m_tab_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    this->setCentralWidget(widget);

    m_menuBar = this->menuBar(); // initialize here so that the config
                                 // visibility works

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
}

// Updates the UI elements checking if valid
// file is open or not
void
lektra::updateUiEnabledState() noexcept
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
lektra::setupKeybinding(const QString &action, const QString &key) noexcept
{
    auto it = m_actionMap.find(action);
    if (it != m_actionMap.end())
    {
        QShortcut *shortcut = new QShortcut(QKeySequence(key), this);
        connect(shortcut, &QShortcut::activated, [it]() { it.value()({}); });
    }

#ifndef NDEBUG
    qDebug() << "Keybinding set:" << action << "->" << key;
#endif

    m_config.shortcuts[action] = key;
}

// Toggles the fullscreen mode
void
lektra::ToggleFullscreen() noexcept
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
lektra::TogglePanel() noexcept
{
    bool shown = !m_statusbar->isHidden();
    m_statusbar->setHidden(shown);
    m_actionTogglePanel->setChecked(!shown);
}

// Toggles the menubar
void
lektra::ToggleMenubar() noexcept
{
    bool shown = !m_menuBar->isHidden();
    m_menuBar->setHidden(shown);
    m_actionToggleMenubar->setChecked(!shown);
}

// Shows the about page
void
lektra::ShowAbout() noexcept
{
    AboutDialog *abw = new AboutDialog(this);
    abw->show();
}

// Reads the arguments passed with `lektra` from the
// commandline
void
lektra::Read_args_parser(argparse::ArgumentParser &argparser) noexcept
{

    if (argparser.is_used("version"))
    {
        qInfo() << "lektra version: " << APP_VERSION;
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
            QString pdfPath = match.captured(1);
            pdfPath.replace(QLatin1Char('~'), homeDir);
            QString texPath = match.captured(2);
            texPath.replace(QLatin1Char('~'), homeDir);
            int line   = match.captured(3).toInt();
            int column = match.captured(4).toInt();
            Q_UNUSED(line);
            Q_UNUSED(column);
            OpenFileInNewTab(pdfPath);
            // synctexLocateInPdf(texPath, line, column); TODO:
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
lektra::populateRecentFiles() noexcept
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
        connect(fileAction, &QAction::triggered, this, [&, path, page]()
        {
            OpenFileInNewTab(path);
            gotoPage(page);
        });

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
lektra::editLastPages() noexcept
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
            &lektra::populateRecentFiles);
}

// Helper function to open last visited file
void
lektra::openLastVisitedFile() noexcept
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
lektra::ZoomOut() noexcept
{
    if (m_doc)
        m_doc->ZoomOut();
}

// Zoom in the file
void
lektra::ZoomIn() noexcept
{
    if (m_doc)
        m_doc->ZoomIn();
}

void
lektra::Zoom_set() noexcept
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
lektra::ZoomReset() noexcept
{
    if (m_doc)
        m_doc->ZoomReset();
}

// Go to a particular page (asks user with a dialog)
void
lektra::Goto_page() noexcept
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

    int pageno = QInputDialog::getInt(
        this, "Goto Page", QString("Enter page number (1 to %1)").arg(total),
        1);

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
lektra::gotoPage(int pageno) noexcept
{
    if (m_doc)
    {
        m_doc->GotoPageWithHistory(pageno - 1);
    }
}

void
lektra::GotoLocation(int pageno, float x, float y) noexcept
{
    if (m_doc)
        m_doc->GotoLocation({pageno, x, y});
}

void
lektra::GotoLocation(const DocumentView::PageLocation &loc) noexcept
{
    if (m_doc)
        m_doc->GotoLocation(loc);
}

// Goes to the next search hit
void
lektra::NextHit() noexcept
{
    if (m_doc)
        m_doc->NextHit();
}

void
lektra::GotoHit(int index) noexcept
{
    if (m_doc)
        m_doc->GotoHit(index);
}

// Goes to the previous search hit
void
lektra::PrevHit() noexcept
{
    if (m_doc)
        m_doc->PrevHit();
}

// Scrolls left in the file
void
lektra::ScrollLeft() noexcept
{
    if (m_doc)
        m_doc->ScrollLeft();
}

// Scrolls right in the file
void
lektra::ScrollRight() noexcept
{
    if (m_doc)
        m_doc->ScrollRight();
}

// Scrolls up in the file
void
lektra::ScrollUp() noexcept
{
    if (m_doc)
        m_doc->ScrollUp();
}

// Scrolls down in the file
void
lektra::ScrollDown() noexcept
{
    if (m_doc)
        m_doc->ScrollDown();
}

// Rotates the file in clockwise direction
void
lektra::RotateClock() noexcept
{
    if (m_doc)
        m_doc->RotateClock();
}

// Rotates the file in anticlockwise direction
void
lektra::RotateAnticlock() noexcept
{
    if (m_doc)
        m_doc->RotateAnticlock();
}

// Shows link hints for each visible link to visit link
// using the keyboard
void
lektra::VisitLinkKB() noexcept
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
lektra::CopyLinkKB() noexcept
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
lektra::Selection_cancel() noexcept
{
    if (m_doc)
        m_doc->ClearTextSelection();
}

// Copies the text selection (if any) to the clipboard
void
lektra::Selection_copy() noexcept
{
    if (m_doc)
        m_doc->YankSelection();
}

bool
lektra::OpenFileDWIM(const QString &filename) noexcept
{
    // If already open anywhere, just focus it
    // if (DocumentView *existing = findOpenView(filename))
    // {
    //     DocumentContainer *container = existing->container();
    //     m_tab_widget->setCurrentIndex(m_tab_widget->indexOf(container));
    //     container->focusView(existing);
    //     return true;
    // }

    if (m_tab_widget->count() == 0)
        return OpenFileInNewTab(filename);

    DocumentContainer *container
        = m_tab_widget->rootContainer(m_tab_widget->currentIndex());

    if (!container)
        return OpenFileInNewTab(filename);

    DocumentView *view = container->view();

    // Empty view — reuse it
    if (!view || view->filePath().isEmpty())
        return OpenFileInContainer(container, filename);

    // Multiple splits — replace current pane
    if (container->getViewCount() > 1)
        return OpenFileInContainer(container, filename);

    // Single view with file — open in new tab
    return OpenFileInNewTab(filename);
}

bool
lektra::OpenFileInContainer(DocumentContainer *container,
                            const QString &filename,
                            const std::function<void()> &callback) noexcept
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
                                           callback);
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

    DocumentView *view = container->view();
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
lektra::OpenFiles(const std::vector<std::string> &filenames) noexcept
{
    for (const std::string &f : filenames)
        OpenFileInNewTab(QString::fromStdString(f));
}

void
lektra::OpenFilesInVSplit(const std::vector<std::string> &files) noexcept
{
#ifndef NDEBUG
    qDebug() << "lektra::OpenFilesInVSplit(): Opening files in vertical split:"
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
lektra::OpenFilesInHSplit(const std::vector<std::string> &files) noexcept
{
#ifndef NDEBUG
    qDebug()
        << "lektra::OpenFilesInHSplit(): Opening files in horizontal split:"
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
lektra::OpenFilesInNewTab(const std::vector<std::string> &files) noexcept
{
    const bool was_batch_opening = m_batch_opening;
    m_batch_opening              = true;
    for (const std::string &s : files)
        OpenFileInNewTab(QString::fromStdString(s));
    m_batch_opening = was_batch_opening;
}

// Opens multiple files given a list of file paths
void
lektra::OpenFilesInNewTab(const QStringList &files) noexcept
{
    const bool was_batch_opening = m_batch_opening;
    m_batch_opening              = true;
    for (const QString &file : files)
        OpenFileInNewTab(file);
    m_batch_opening = was_batch_opening;
}

// Opens a file given the DocumentView pointer
// bool
// lektra::OpenFile(DocumentView *view) noexcept
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

DocumentView::Id
lektra::OpenFileInNewTab(const QString &filename,
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
        return false;
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
        if (m_doc == closedView)
        {
            int currentTabIndex = m_tab_widget->currentIndex();
            DocumentContainer *currentContainer
                = m_tab_widget->rootContainer(currentTabIndex);
            if (currentContainer)
                setCurrentDocumentView(currentContainer->view());
        }
    });

    connect(container, &DocumentContainer::currentViewChanged, container,
            [this, container](DocumentView *newView)
    {
        DocumentContainer *current
            = m_tab_widget->rootContainer(m_tab_widget->currentIndex());

        if (!current)
            return;

        if (container == current)
            setCurrentDocumentView(newView);
    });

    // Initialize connections for the initial view
    initTabConnections(view);

    // Set DPR BEFORE opening the file to ensure correct resolution rendering
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

    return true;
}

DocumentView::Id
lektra::openFileSplitHelper(const QString &filename,
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
                return OpenFileVSplit(selected.first(), callback);
        }
        return false;
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
        return false;

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

    return false;
}

DocumentView::Id
lektra::OpenFileVSplit(const QString &filename,
                       const std::function<void()> &callback)
{
    return openFileSplitHelper(filename, callback, Qt::Vertical);
}

DocumentView::Id
lektra::OpenFileHSplit(const QString &filename,
                       const std::function<void()> &callback)
{
    return openFileSplitHelper(filename, callback, Qt::Horizontal);
}

void
lektra::OpenFilesInNewWindow(const QStringList &filenames) noexcept
{
    if (filenames.empty())
        return;

    for (const QString &file : filenames)
    {
        OpenFileInNewWindow(file);
    }
}

DocumentView::Id
lektra::OpenFileInNewWindow(const QString &filePath,
                            const std::function<void()> &callback) noexcept
{
    if (filePath.isEmpty())
    {
        QStringList files;
        files = QFileDialog::getOpenFileNames(
            this, "Open File", "", "PDF Files (*.pdf);; All Files (*)");
        if (files.empty())
            return false;
        else if (files.size() > 1)
        {
            OpenFilesInNewWindow(files);
            return true;
        }
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
lektra::FileProperties() noexcept
{
    if (m_doc)
        m_doc->FileProperties();
}

// Saves the current file
void
lektra::SaveFile() noexcept
{
    if (m_doc)
        m_doc->SaveFile();
}

// Saves the current file as a new file
void
lektra::SaveAsFile() noexcept
{
    if (m_doc)
        m_doc->SaveAsFile();
}

// Fit the document to the width of the window
void
lektra::Fit_width() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Width);
}

// Fit the document to the height of the window
void
lektra::Fit_height() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Height);
}

// Fit the document to the window
void
lektra::Fit_page() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Window);
}

// Toggle auto-resize mode
void
lektra::ToggleAutoResize() noexcept
{
    if (m_doc)
        m_doc->ToggleAutoResize();
}

// Show or hide the outline panel
void
lektra::Show_outline() noexcept
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
        m_outline_picker = new OutlinePicker(this);
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
lektra::Show_highlight_search() noexcept
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
lektra::InvertColor() noexcept
{
    if (m_doc)
    {
        m_doc->setInvertColor(!m_doc->invertColor());
        m_actionInvertColor->setChecked(!m_actionInvertColor->isChecked());
    }
}

// Toggle text highlight mode
void
lektra::ToggleTextHighlight() noexcept
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
lektra::ToggleTextSelection() noexcept
{
    if (m_doc)
        m_doc->ToggleTextSelection();
}

// Toggle rectangle annotation mode
void
lektra::ToggleAnnotRect() noexcept
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
lektra::ToggleAnnotSelect() noexcept
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
lektra::ToggleAnnotPopup() noexcept
{
    if (m_doc)
    {
        if (m_doc->fileType() == Model::FileType::PDF)
            m_doc->ToggleAnnotPopup();
        else
            QMessageBox::information(this, "Toggle Annot Popup",
                                     "Not a PDF file to annotate");
    }
}

// Toggle region select mode
void
lektra::ToggleRegionSelect() noexcept
{
    if (m_doc)
        m_doc->ToggleRegionSelect();
}

// Go to the first page
void
lektra::FirstPage() noexcept
{
    if (m_doc)
        m_doc->GotoFirstPage();
    updatePageNavigationActions();
}

// Go to the previous page
void
lektra::PrevPage() noexcept
{
    if (m_doc)
        m_doc->GotoPrevPage();
    updatePageNavigationActions();
}

// Go to the next page
void
lektra::NextPage() noexcept
{
    if (m_doc)
        m_doc->GotoNextPage();
    updatePageNavigationActions();
}

// Go to the last page
void
lektra::LastPage() noexcept
{
    if (m_doc)
        m_doc->GotoLastPage();

    updatePageNavigationActions();
}

// Go back in the page history
void
lektra::GoBackHistory() noexcept
{
    if (m_doc)
        m_doc->GoBackHistory();
}

// Go forward in the page history
void
lektra::GoForwardHistory() noexcept
{
    if (m_doc)
        m_doc->GoForwardHistory();
}

// Highlight text annotation for the current selection
void
lektra::TextHighlightCurrentSelection() noexcept
{
    if (m_doc)
        m_doc->TextHighlightCurrentSelection();
}

// Initialize all the connections for the `lektra` class
void
lektra::initConnections() noexcept
{
    connect(m_statusbar, &Statusbar::modeColorChangeRequested, this,
            [&](GraphicsView::Mode mode) { modeColorChangeRequested(mode); });

    connect(m_statusbar, &Statusbar::pageChangeRequested, this,
            &lektra::gotoPage);

    connect(m_config_watcher, &QFileSystemWatcher::fileChanged, this,
            &lektra::updateGUIFromConfig);
    QList<QScreen *> outputs = QGuiApplication::screens();
    connect(m_tab_widget, &TabWidget::currentChanged, this,
            &lektra::handleCurrentTabChanged);

    // Tab drag and drop connections for cross-window tab transfer
    connect(m_tab_widget, &TabWidget::tabDataRequested, this,
            &lektra::handleTabDataRequested);
    connect(m_tab_widget, &TabWidget::tabDropReceived, this,
            &lektra::handleTabDropReceived);
    connect(m_tab_widget, &TabWidget::tabDetached, this,
            &lektra::handleTabDetached);
    connect(m_tab_widget, &TabWidget::tabDetachedToNewWindow, this,
            &lektra::handleTabDetachedToNewWindow);

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
            [this](const QString &term)
    {
        if (m_doc)
            m_doc->Search(term);
    });

    connect(m_search_bar, &SearchBar::searchIndexChangeRequested, this,
            &lektra::GotoHit);
    connect(m_search_bar, &SearchBar::nextHitRequested, this, &lektra::NextHit);
    connect(m_search_bar, &SearchBar::prevHitRequested, this, &lektra::PrevHit);

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
            &lektra::updatePageNavigationActions);

    connect(m_highlight_search_picker,
            &HighlightSearchPicker::gotoLocationRequested, this,
            [this](int page, float x, float y)
    { m_doc->GotoLocationWithHistory({page, x, y}); });
}

// Handle when the file name is changed
void
lektra::handleFileNameChanged(const QString &name) noexcept
{
    m_statusbar->setFileName(name);
    this->setWindowTitle(name);
}

// Handle when the current tab is changed
void
lektra::handleCurrentTabChanged(int index) noexcept
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
lektra::handleTabDataRequested(int index, TabBar::TabData *outData) noexcept
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
lektra::handleTabDropReceived(const TabBar::TabData &data) noexcept
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
lektra::handleTabDetached(int index, const QPoint &globalPos) noexcept
{
    Q_UNUSED(globalPos);

    if (!validTabIndex(index))
        return;

    // Close the tab that was successfully moved to another window
    m_tab_widget->tabCloseRequested(index);
}

void
lektra::handleTabDetachedToNewWindow(int index,
                                     const TabBar::TabData &data) noexcept
{
    if (!validTabIndex(index))
        return;

    if (data.filePath.isEmpty())
        return;

    // Spawn a new lektra process with the file
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
lektra::closeEvent(QCloseEvent *e)
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
            this, "Confirm Quit", "Are you sure you want to quit lektra?",
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
lektra::ToggleTabBar() noexcept
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
lektra::eventFilter(QObject *object, QEvent *event)
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
lektra::dropEvent(QDropEvent *e) noexcept
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
lektra::handleTabContextMenu(QObject *object, QEvent *event) noexcept
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
lektra::handleLinkHintEvent(QEvent *event) noexcept
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
// lektra::handleGetInputEvent(QEvent *event) noexcept
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
lektra::openInExplorerForIndex(int index) noexcept
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
lektra::filePropertiesForIndex(int index) noexcept
{
    DocumentView *doc
        = qobject_cast<DocumentView *>(m_tab_widget->widget(index));
    // doc->setInvertColor(!doc->invertColor());
    if (doc)
        doc->FileProperties();
}

// Initialize connections on each tab addition
void
lektra::initTabConnections(DocumentView *docwidget) noexcept
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
            &lektra::handleFileNameChanged);

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
            &lektra::insertFileToDB);

    connect(docwidget, &DocumentView::ctrlLinkClickRequested, this,
            &lektra::handleCtrlLinkClickRequested);
}

// Insert file to store when tab is closed to track
// recent files
void
lektra::insertFileToDB(const QString &fname, int pageno) noexcept
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
lektra::updateMenuActions() noexcept
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
lektra::updatePanel() noexcept
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

        m_statusbar->setPortalMode(m_doc->container()->has_portal());
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
lektra::LoadSession(QString sessionName) noexcept
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

        // Create a new lektra window to load the session into if there's
        // document already opened in the current window
        if (m_tab_widget->count() > 0)
        {
            new lektra(sessionName, doc.array());
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
lektra::getSessionFiles() noexcept
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
lektra::SaveSession() noexcept
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
lektra::writeSessionToFile() noexcept
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
lektra::SaveAsSession(const QString &sessionPath) noexcept
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
        "lektra session files (*.json); All Files (*.*)");

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
lektra::showStartupWidget() noexcept
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
lektra::updateActionsAndStuffForSystemTabs() noexcept
{
    m_statusbar->hidePageInfo(true);
    updateUiEnabledState();
    m_statusbar->setFileName("Start Page");
}

// Undo operation
void
lektra::Undo() noexcept
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
lektra::Redo() noexcept
{
    if (m_doc && m_doc->model())
    {
        auto redoStack = m_doc->model()->undoStack();
        if (redoStack->canRedo())
            redoStack->redo();
    }
}

// Initialize the actions with corresponding functions
// to call
// Helper macro for actions that don't use arguments
#define ACTION_NO_ARGS(name, func)                                             \
    {name, [this](const QStringList &) { func(); }}

// Initialize the action map
void
lektra::initActionMap() noexcept
{
    m_actionMap = {
        // Selection actions
        ACTION_NO_ARGS("selection_copy", Selection_copy),
        ACTION_NO_ARGS("selection_cancel", Selection_cancel),
        ACTION_NO_ARGS("selection_last", ReselectLastTextSelection),

        // Toggle actions
        ACTION_NO_ARGS("toggle_presentation_mode", Toggle_presentation_mode),
        ACTION_NO_ARGS("toggle_fullscreen", ToggleFullscreen),
        ACTION_NO_ARGS("toggle_command_palette", Show_command_picker),
        ACTION_NO_ARGS("toggle_tabs", ToggleTabBar),
        ACTION_NO_ARGS("toggle_menubar", ToggleMenubar),
        ACTION_NO_ARGS("toggle_statusbar", TogglePanel),
        ACTION_NO_ARGS("toggle_focus_mode", ToggleFocusMode),
// Actions without arguments
#ifdef ENABLE_LLM_SUPPORT
        ACTION_NO_ARGS("toggle_llm_widget", ToggleLLMWidget),
#endif

        // Link hint actions
        ACTION_NO_ARGS("link_hint_visit", VisitLinkKB),
        ACTION_NO_ARGS("link_hint_copy", CopyLinkKB),

        // Page navigation actions
        ACTION_NO_ARGS("page_first", FirstPage),
        ACTION_NO_ARGS("page_last", LastPage),
        ACTION_NO_ARGS("page_next", NextPage),
        ACTION_NO_ARGS("page_prev", PrevPage),
        ACTION_NO_ARGS("page_goto", Goto_page),

        // Scrolling actions
        ACTION_NO_ARGS("scroll_down", ScrollDown),
        ACTION_NO_ARGS("scroll_up", ScrollUp),
        ACTION_NO_ARGS("scroll_left", ScrollLeft),
        ACTION_NO_ARGS("scroll_right", ScrollRight),

        // Rotation actions
        ACTION_NO_ARGS("rotate_clock", RotateClock),
        ACTION_NO_ARGS("rotate_anticlock", RotateAnticlock),

        // Location actions
        ACTION_NO_ARGS("location_prev", GoBackHistory),
        ACTION_NO_ARGS("location_next", GoForwardHistory),

        // Zoom actions
        ACTION_NO_ARGS("zoom_in", ZoomIn),
        ACTION_NO_ARGS("zoom_out", ZoomOut),
        ACTION_NO_ARGS("zoom_reset", ZoomReset),
        ACTION_NO_ARGS("zoom_set", Zoom_set),

        // Split actions
        ACTION_NO_ARGS("split_horizontal", VSplit),
        ACTION_NO_ARGS("split_vertical", HSplit),
        ACTION_NO_ARGS("split_close", Close_split),
        ACTION_NO_ARGS("split_focus_right", Focus_split_right),
        ACTION_NO_ARGS("split_focus_left", Focus_split_left),
        ACTION_NO_ARGS("split_focus_up", Focus_split_up),
        ACTION_NO_ARGS("split_focus_down", Focus_split_down),
        ACTION_NO_ARGS("split_close_others", Close_other_splits),

        // Portal actions
        ACTION_NO_ARGS("portal_focus", Focus_portal),

        // File opening actions
        ACTION_NO_ARGS("file_open_tab", OpenFileInNewTab),
        ACTION_NO_ARGS("file_open_vsplit", OpenFileVSplit),
        ACTION_NO_ARGS("file_open_hsplit", OpenFileHSplit),
        ACTION_NO_ARGS("file_open_dwim", OpenFileDWIM),
        ACTION_NO_ARGS("file_close", CloseFile),
        ACTION_NO_ARGS("file_save", SaveFile),
        ACTION_NO_ARGS("file_save_as", SaveAsFile),
        ACTION_NO_ARGS("file_encrypt", EncryptDocument),
        ACTION_NO_ARGS("file_decrypt", DecryptDocument),
        ACTION_NO_ARGS("file_reload", reloadDocument),
        ACTION_NO_ARGS("file_properties", FileProperties),
        ACTION_NO_ARGS("files_recent", Show_recent_files_picker),

        // Annotation modes
        ACTION_NO_ARGS("annot_edit_mode", ToggleAnnotSelect),
        ACTION_NO_ARGS("annot_popup_mode", ToggleAnnotPopup),
        ACTION_NO_ARGS("annot_rect_mode", ToggleAnnotRect),
        ACTION_NO_ARGS("annot_popup_mode", ToggleAnnotPopup),
        ACTION_NO_ARGS("annot_highlight_mode", ToggleTextHighlight),

        // Selection modes
        ACTION_NO_ARGS("selection_mode_text", ToggleTextSelection),
        ACTION_NO_ARGS("selection_mode_region", ToggleRegionSelect),

        // Fit modes
        ACTION_NO_ARGS("fit_width", Fit_width),
        ACTION_NO_ARGS("fit_height", Fit_height),
        ACTION_NO_ARGS("fit_page", Fit_page),
        ACTION_NO_ARGS("fit_auto", ToggleAutoResize),

        // Session actions
        ACTION_NO_ARGS("session_save", SaveSession),
        ACTION_NO_ARGS("session_save_as", SaveAsSession),
        ACTION_NO_ARGS("session_load", LoadSession),

        // Tab actions
        ACTION_NO_ARGS("tabs_close_left", TabsCloseLeft),
        ACTION_NO_ARGS("tabs_close_right", TabsCloseRight),
        ACTION_NO_ARGS("tabs_close_others", TabsCloseOthers),
        ACTION_NO_ARGS("tab_move_right", TabMoveRight),
        ACTION_NO_ARGS("tab_move_left", TabMoveLeft),
        ACTION_NO_ARGS("tab_first", Tab_first),
        ACTION_NO_ARGS("tab_last", Tab_last),
        ACTION_NO_ARGS("tab_next", Tab_next),
        ACTION_NO_ARGS("tab_prev", Tab_prev),
        ACTION_NO_ARGS("tab_close", Tab_close),
        ACTION_NO_ARGS("tab_goto", [this]() { Tab_goto(); }),
        ACTION_NO_ARGS("tab_1", [this]() { Tab_goto(1); }),
        ACTION_NO_ARGS("tab_2", [this]() { Tab_goto(2); }),
        ACTION_NO_ARGS("tab_3", [this]() { Tab_goto(3); }),
        ACTION_NO_ARGS("tab_4", [this]() { Tab_goto(4); }),
        ACTION_NO_ARGS("tab_5", [this]() { Tab_goto(5); }),
        ACTION_NO_ARGS("tab_6", [this]() { Tab_goto(6); }),
        ACTION_NO_ARGS("tab_7", [this]() { Tab_goto(7); }),
        ACTION_NO_ARGS("tab_8", [this]() { Tab_goto(8); }),
        ACTION_NO_ARGS("tab_9", [this]() { Tab_goto(9); }),

        // Pickers
        ACTION_NO_ARGS("picker_outline", Show_outline),
        ACTION_NO_ARGS("picker_highlight_search", Show_highlight_search),

        // Search actions
        ACTION_NO_ARGS("search", Search),
        ACTION_NO_ARGS("search_next", NextHit),
        ACTION_NO_ARGS("search_prev", PrevHit),
        {"search_args",
         [this](const QStringList &args) { search(args.join(" ")); }},

        // Layout modes
        {"layout_single", [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::SINGLE); }},
        {"layout_left_to_right", [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::LEFT_TO_RIGHT); }},
        {"layout_top_to_bottom", [this](const QStringList &)
    { SetLayoutMode(DocumentView::LayoutMode::TOP_TO_BOTTOM); }},

        // Other actions
        ACTION_NO_ARGS("set_dpr", SetDPR),
        ACTION_NO_ARGS("open_containing_folder", OpenContainingFolder),
        ACTION_NO_ARGS("undo", Undo),
        ACTION_NO_ARGS("redo", Redo),
        ACTION_NO_ARGS("highlight_selection", TextHighlightCurrentSelection),
        ACTION_NO_ARGS("invert_color", InvertColor),
        ACTION_NO_ARGS("reshow_jump_marker", Reshow_jump_marker),

        // Help/About actions
        ACTION_NO_ARGS("show_startup_widget", showStartupWidget),
        ACTION_NO_ARGS("show_tutorial_file", showTutorialFile),
        ACTION_NO_ARGS("show_about", ShowAbout),

        // TODO: Implement these actions
        // ACTION_NO_ARGS("annot_next", Annot_next),
        // ACTION_NO_ARGS("annot_prev", Annot_prev),
        // ACTION_NO_ARGS("annot_export", Annot_export),
        // ACTION_NO_ARGS("save_selection_as_image", Save_selection_as_image),
        // ACTION_NO_ARGS("search_clear", Clear_search),
        // ACTION_NO_ARGS("reopen_last_closed_file", Reopen_last_closed_file),
        // ACTION_NO_ARGS("copy_page_image", Copy_page_image),

        // {"search_in_page",
        // [this](const QStringList &args) { SearchInPage(args.join("
        // ")); }},
    };
}

#undef ACTION_NO_ARGS

// Trims the recent files store to `num_recent_files` number of files
void
lektra::trimRecentFilesDatabase() noexcept
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
lektra::SetDPR() noexcept
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
lektra::reloadDocument() noexcept
{
    if (m_doc)
        m_doc->model()->reloadDocument();
}

// Go to the first tab
void
lektra::Tab_first() noexcept
{
    if (m_tab_widget->count() != 0)
    {
        m_tab_widget->setCurrentIndex(0);
    }
}

// Go to the last tab
void
lektra::Tab_last() noexcept
{
    int count = m_tab_widget->count();
    if (count != 0)
    {
        m_tab_widget->setCurrentIndex(count - 1);
    }
}

// Go to the next tab
void
lektra::Tab_next() noexcept
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
lektra::Tab_prev() noexcept
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
lektra::Tab_goto(int index) noexcept
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
lektra::Tab_close(int tabno) noexcept
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
lektra::TabMoveRight() noexcept
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
lektra::TabMoveLeft() noexcept
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
lektra::updatePageNavigationActions() noexcept
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
lektra::OpenContainingFolder() noexcept
{
    if (m_doc)
    {
        QString filepath = m_doc->fileName();
        QDesktopServices::openUrl(QUrl(QFileInfo(filepath).absolutePath()));
    }
}

// Encrypt the current document
void
lektra::EncryptDocument() noexcept
{
    if (m_doc)
    {
        m_doc->EncryptDocument();
    }
}

void
lektra::DecryptDocument() noexcept
{
    if (m_doc)
        m_doc->DecryptDocument();
}

// Update selection mode actions (QAction) in QMenu based on current
// selection mode
void
lektra::updateSelectionModeActions() noexcept
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
lektra::updateGUIFromConfig() noexcept
{
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
}

void
lektra::ToggleFocusMode() noexcept
{
    if (!m_doc)
        return;

    setFocusMode(!m_focus_mode);
}

void
lektra::setFocusMode(bool enable) noexcept
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
lektra::updateTabbarVisibility() noexcept
{
    // Let tab widget manage visibility itself based on auto-hide property
    m_tab_widget->tabBar()->setVisible(true); // initially show
    if (m_tab_widget->tabBarAutoHide() && m_tab_widget->count() < 2)
        m_tab_widget->tabBar()->setVisible(false);
}

void
lektra::search(const QString &term) noexcept
{
    if (m_doc)
        m_doc->Search(term);
}

void
lektra::searchInPage(const int pageno, const QString &term) noexcept
{
    if (m_doc)
        m_doc->SearchInPage(pageno, term);
}

void
lektra::Search() noexcept
{
    if (m_doc)
    {
        // m_search_bar_overlay->show();
        // m_search_bar_overlay->raise();
        // m_search_bar_overlay->activateWindow();
        // m_search_bar_overlay->setVisible(true);
        m_search_bar->setVisible(true);
        m_search_bar->focusSearchInput();
    }
}

void
lektra::setSessionName(const QString &name) noexcept
{
    m_session_name = name;
    m_statusbar->setSessionName(name);
}

void
lektra::openSessionFromArray(const QJsonArray &sessionArray) noexcept
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
lektra::modeColorChangeRequested(const GraphicsView::Mode mode) noexcept
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
lektra::ReselectLastTextSelection() noexcept
{
    if (m_doc)
        m_doc->ReselectLastTextSelection();
}

void
lektra::SetLayoutMode(DocumentView::LayoutMode mode) noexcept
{
    if (m_doc)
        m_doc->setLayoutMode(mode);
}

// Handle Escape key press for the entire application
void
lektra::handleEscapeKeyPressed() noexcept
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
lektra::Show_command_picker() noexcept
{
    if (!m_command_picker)
    {
        m_command_picker = new CommandPicker(m_config, m_actionMap,
                                             m_config.shortcuts, this);
        m_command_picker->setKeybindings(m_picker_keybinds);
    }
    m_command_picker->launch();
}

#ifdef ENABLE_LLM_SUPPORT
void
lektra::ToggleLLMWidget() noexcept
{
    m_llm_widget->setVisible(!m_llm_widget->isVisible());
}
#endif

void
lektra::showTutorialFile() noexcept
{
#if defined(__linux__)
    const QString doc_path = QString("%1%2")
                                 .arg(APP_INSTALL_PREFIX)
                                 .arg("/share/doc/lektra/tutorial.pdf");
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
// lektra::SetMark() noexcept
// {
//     m_message_bar->showMessage("**SetMark**, Waiting for mark: ", -1);
// }

// void
// lektra::GotoMark() noexcept
// {
//     m_message_bar->showMessage("**GotoMark**, Waiting for mark: ", -1);
//     // Wait for key input
// }

// void
// lektra::DeleteMark() noexcept
// {
//     m_message_bar->showMessage("**GotoMark**, Waiting for mark: ", -1);
// }

// void
// lektra::setMark(const QString &key, const int pageno,
//               const DocumentView::PageLocation location) noexcept
// {
// }

// void
// lektra::gotoMark(const QString &key) noexcept
// {
// }

// void
// lektra::deleteMark(const QString &key) noexcept
// {
// }

void
lektra::TabsCloseLeft() noexcept
{
    const int currentIndex = m_tab_widget->currentIndex();
    if (currentIndex <= 0)
        return;

    for (int i = currentIndex - 1; i >= 0; --i)
        m_tab_widget->tabCloseRequested(i);
}

void
lektra::TabsCloseRight() noexcept
{
    const int currentIndex = m_tab_widget->currentIndex();
    const int ntabs        = m_tab_widget->count();

    if (currentIndex < 0 || currentIndex >= ntabs - 1)
        return;

    for (int i = ntabs - 1; i > currentIndex; --i)
        m_tab_widget->tabCloseRequested(i);
}

void
lektra::TabsCloseOthers() noexcept
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
lektra::VSplit() noexcept
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
lektra::HSplit() noexcept
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

// Closes all splits except the current one in the current tab. If there is only
// one split, does nothing.
void
lektra::Close_other_splits() noexcept
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
lektra::Close_split() noexcept
{
    int currentTabIndex = m_tab_widget->currentIndex();
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
        m_tab_widget->tabBar()->set_split_count(currentTabIndex,
                                                container->getViewCount());
    }
}

void
lektra::setCurrentDocumentView(DocumentView *view) noexcept
{
    if (m_doc == view)
        return;

    m_doc = view;

    if (!view)
    {
        updateUiEnabledState();
        updatePanel();
        return;
    }

    DocumentContainer *container
        = m_tab_widget->rootContainer(m_tab_widget->currentIndex());
    if (!container)
        return;

    if (container->view() != view)
        container->focusView(view);

    updateUiEnabledState();
    updatePageNavigationActions();
    updatePanel();
}

void
lektra::centerMouseInDocumentView(DocumentView *view) noexcept
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
lektra::CloseFile() noexcept
{
    if (m_doc)
    {
        int indexToClose = m_tab_widget->currentIndex();
        Tab_close(indexToClose);
    }
}

void
lektra::Focus_split_up() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Up);
}

void
lektra::Focus_split_down() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Down);
}

void
lektra::Focus_split_left() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Left);
}

void
lektra::Focus_split_right() noexcept
{
    focusSplitHelper(DocumentContainer::Direction::Right);
}

void
lektra::focusSplitHelper(DocumentContainer::Direction direction) noexcept
{
    const int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
    if (!container)
        return;

    container->focusSplit(direction);

    if (m_config.split.focus_follows_mouse)
        if (auto *view = container->view())
            centerMouseInDocumentView(view);
}

void
lektra::restoreSplitNode(DocumentContainer *container, DocumentView *targetView,
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
lektra::findOpenView(const QString &path) const noexcept
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
lektra::handleCtrlLinkClickRequested(DocumentView *view,
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

    // Open same file in a vertical split, jumped to target page
    DocumentContainer *container = view->container();
    if (!container)
        return;

    // Create the location target data (copy values, not pointers)
    DocumentView::PageLocation target{
        linkItem->gotoPageNo(), linkItem->location().x, linkItem->location().y};

    if (std::isnan(target.x))
        target.x = 0;
    if (std::isnan(target.y))
        target.y = 0;

    // Check if portal already exists
    if (container->has_portal())
    {
        DocumentView *portalView = container->portal();
        if (portalView)
            portalView->GotoLocation(target);
        return;
    }

    const QString filePath = view->filePath();

    // Capture by VALUE, not reference
    OpenFileVSplit(filePath, [this, container, target, filePath]()
    {
        if (!m_doc)
            return;

        // Go to the location
        m_doc->GotoLocation(target);

        // Find the newly created split's ID and set it as portal
        int currentTabIndex = m_tab_widget->currentIndex();
        DocumentContainer *currentContainer
            = m_tab_widget->rootContainer(currentTabIndex);

        if (currentContainer == container && m_doc)
        {
            container->set_portal(m_doc);
        }
    });
}

DocumentView *
lektra::get_view_by_id(const DocumentView::Id &id) const noexcept
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

// Focus the portal view in the current tab, if it exists. The portal view is a
// special view that can be set to follow the location of another view (e.g. for
// ctrl+click navigation)
void
lektra::Focus_portal() noexcept
{
    int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
    if (!container)
        return;

    if (container->has_portal())
    {
        DocumentView *portalView = container->portal();
        if (portalView)
            container->focusView(portalView);
    }
}

// If a jump marker was shown for the current document view, re-show it (e.g.
// after a reload)
void
lektra::Reshow_jump_marker() noexcept
{
    if (m_doc)
        m_doc->Reshow_jump_marker();
}

void
lektra::Toggle_presentation_mode() noexcept
{
    if (!m_doc)
        return;

    // TODO: Implement presentation mode (probably just a special full-screen
    // mode with extra UI optimizations for it)
}

// Show a picker with the list of recent files from the recent files store, and
// allow the user to open a file from there.
void
lektra::Show_recent_files_picker() noexcept
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
