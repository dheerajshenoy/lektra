#include "Lektra.hpp"

#include "AboutDialog.hpp"
#include "AppPaths.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
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
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeData>
#include <QObject>
#include <QProcess>
#include <QSplitter>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWindow>
#include <variant>

namespace
{

static inline QString
supportedFormats()
{
    return "PDF (*.pdf);;"
           "XPS (*.oxps *.xps);;"
           "CBZ (*.cbz *.cbt);;"
           "FB2 (*.fbz);;"
           "EPUB (*.epub);;"
           "FictionBook (*.fb2 *.fbz);;"
           "Mobi (*.mobi);;"
           "Images (*.jpg *.jpeg *.png *.tiff *.tif);;"
           "SVG (*.svg);;"
#ifdef HAS_DJVU
           "DjVu (*.djvu *.djv);;"
#endif
           "All Files (*.*)";
}

static inline void
set_title_format_if_present(toml::node_view<toml::node> n,
                            QString &title_format)
{
    if (auto v = n.value<std::string>())
    {
        QString window_title = QString::fromStdString(*v);
        window_title         = window_title.replace("{}", "%1");
        title_format         = window_title;
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

static inline void
set_picker_shared(toml::node_view<toml::node> picker, Config::Picker &target)
{
    set(picker["width"], target.width);
    set(picker["height"], target.height);
    set(picker["border"], target.border);
    set(picker["alternating_row_color"], target.alternating_row_color);

    if (auto picker_shadow = picker["shadow"])
    {
        set(picker_shadow["enabled"], target.shadow.enabled);
        set(picker_shadow["blur_radius"], target.shadow.blur_radius);
        set(picker_shadow["offset_x"], target.shadow.offset_x);
        set(picker_shadow["offset_y"], target.shadow.offset_y);
        set(picker_shadow["opacity"], target.shadow.opacity);
    }
}

template <typename T>
static inline void
inherit_picker_defaults(const Config::Picker &base, T &target)
{
    static_assert(std::is_base_of_v<Config::Picker, T>,
                  "Target must derive from Config::Picker");
    static_cast<Config::Picker &>(target) = base;
}

} // namespace

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

// On-demand construction of `Lektra` (for use with argparse)
void
Lektra::construct() noexcept
{
    initCommands();
    initDefaultKeybinds();
    initConfig();
    initGui();
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
    QMenu *fileMenu = m_menuBar->addMenu(tr("&File"));

    fileMenu->addAction(
        tr("Open File\t%1").arg(m_config.keybinds["file_open_tab"]), this,
        [&]() { OpenFileInNewTab(); });

    fileMenu->addAction(tr("Open File In VSplit\t%1")
                            .arg(m_config.keybinds["file_open_vsplit"]),
                        this, [&]() { OpenFileVSplit(); });

    fileMenu->addAction(tr("Open File In HSplit\t%1")
                            .arg(m_config.keybinds["file_open_hsplit"]),
                        this, [&]() { OpenFileHSplit(); });

    m_recentFilesMenu = fileMenu->addMenu(tr("Recent Files"));

    m_actionFileProperties = fileMenu->addAction(
        tr("File Properties\t%1").arg(m_config.keybinds["file_properties"]),
        this, &Lektra::FileProperties);

    m_actionOpenContainingFolder = fileMenu->addAction(
        tr("Open Containing Folder\t%1")
            .arg(m_config.keybinds["open_containing_folder"]),
        this, &Lektra::OpenContainingFolder);
    m_actionOpenContainingFolder->setEnabled(false);

    m_actionSaveFile = fileMenu->addAction(
        tr("Save File\t%1").arg(m_config.keybinds["file_save"]), this,
        [this]() { Lektra::SaveFile(); });

    m_actionSaveAsFile = fileMenu->addAction(
        tr("Save As File\t%1").arg(m_config.keybinds["file_save_as"]), this,
        [this]() { Lektra::SaveAsFile(); });

    QMenu *sessionMenu = fileMenu->addMenu(tr("Session"));

    m_actionSessionSave = sessionMenu->addAction(
        tr("Save\t%1").arg(m_config.keybinds["session_save"]), this,
        [&]() { SaveSession(); });
    m_actionSessionSaveAs = sessionMenu->addAction(
        tr("Save As\t%1").arg(m_config.keybinds["session_save_as"]), this,
        [&]() { SaveAsSession(); });
    m_actionSessionLoad = sessionMenu->addAction(
        tr("Load\t%1").arg(m_config.keybinds["session_load"]), this,
        [&]() { LoadSession(); });

    m_actionSessionSaveAs->setEnabled(false);

    m_actionCloseFile = fileMenu->addAction(
        tr("Close File\t%1").arg(m_config.keybinds["file_close"]), this,
        [this]() { Tab_close(); });

    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quit"), this, &QMainWindow::close);

    QMenu *editMenu = m_menuBar->addMenu(tr("&Edit"));
    m_actionUndo    = editMenu->addAction(
        tr("Undo\t%1").arg(m_config.keybinds["undo"]), this, &Lektra::Undo);
    m_actionRedo = editMenu->addAction(
        tr("Redo\t%1").arg(m_config.keybinds["redo"]), this, &Lektra::Redo);
    m_actionUndo->setEnabled(false);
    m_actionRedo->setEnabled(false);
    editMenu->addAction(
        tr("Last Pages\t%1").arg(m_config.keybinds["edit_last_pages"]), this,
        &Lektra::editLastPages);

    // --- View Menu ---
    m_viewMenu         = m_menuBar->addMenu(tr("&View"));
    m_actionFullscreen = m_viewMenu->addAction(
        tr("Fullscreen\t%1").arg(m_config.keybinds["fullscreen"]), this,
        &Lektra::ToggleFullscreen);
    m_actionFullscreen->setCheckable(true);
    m_actionFullscreen->setChecked(m_config.window.fullscreen);

    m_actionZoomIn = m_viewMenu->addAction(
        tr("Zoom In\t%1").arg(m_config.keybinds["zoom_in"]), this,
        &Lektra::ZoomIn);
    m_actionZoomOut = m_viewMenu->addAction(
        tr("Zoom Out\t%1").arg(m_config.keybinds["zoom_out"]), this,
        &Lektra::ZoomOut);

    m_viewMenu->addSeparator();

    m_fitMenu = m_viewMenu->addMenu(tr("Fit"));

    m_actionFitWidth = m_fitMenu->addAction(
        tr("Width\t%1").arg(m_config.keybinds["fit_width"]), this,
        &Lektra::Fit_width);

    m_actionFitHeight = m_fitMenu->addAction(
        tr("Height\t%1").arg(m_config.keybinds["fit_height"]), this,
        &Lektra::Fit_height);

    m_actionFitWindow = m_fitMenu->addAction(
        tr("Page\t%1").arg(m_config.keybinds["fit_page"]), this,
        &Lektra::Fit_page);

    m_fitMenu->addSeparator();

    // Auto Resize toggle (independent)
    m_actionAutoresize = m_viewMenu->addAction(
        tr("Auto Fit\t%1").arg(m_config.keybinds["fit_auto"]), this,
        &Lektra::ToggleAutoResize);
    m_actionAutoresize->setCheckable(true);
    m_actionAutoresize->setChecked(
        m_config.layout.auto_resize); // default on or off

    // --- Layout Menu ---

    m_viewMenu->addSeparator();
    m_layoutMenu                    = m_viewMenu->addMenu(tr("Layout"));
    QActionGroup *layoutActionGroup = new QActionGroup(this);
    layoutActionGroup->setExclusive(true);

    m_actionLayoutSingle = m_layoutMenu->addAction(
        tr("Single Page\t%1").arg(m_config.keybinds["layout_single"]), this,
        [&]() { SetLayoutMode(DocumentView::LayoutMode::SINGLE); });

    m_actionLayoutLeftToRight = m_layoutMenu->addAction(
        tr("Left to Right Page\t%1")
            .arg(m_config.keybinds["layout_horizontal"]),
        this, [&]() { SetLayoutMode(DocumentView::LayoutMode::HORIZONTAL); });

    m_actionLayoutTopToBottom = m_layoutMenu->addAction(
        tr("Top to Bottom Page\t%1").arg(m_config.keybinds["layout_vertical"]),
        this, [&]() { SetLayoutMode(DocumentView::LayoutMode::VERTICAL); });

    m_actionLayoutBook = m_layoutMenu->addAction(
        tr("Book\t%1").arg(m_config.keybinds["layout_book"]), this,
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
        m_config.layout.mode == DocumentView::LayoutMode::HORIZONTAL);
    m_actionLayoutTopToBottom->setChecked(
        m_config.layout.mode == DocumentView::LayoutMode::VERTICAL);
    m_actionLayoutBook->setChecked(m_config.layout.mode
                                   == DocumentView::LayoutMode::BOOK);

    // --- Toggle Menu ---

    m_viewMenu->addSeparator();
    m_toggleMenu = m_viewMenu->addMenu(tr("Show/Hide"));

#ifdef ENABLE_LLM_SUPPORT
    m_actionToggleLLMWidget = m_toggleMenu->addAction(
        tr("LLM Widget\t%1").arg(m_config.keybinds["llm_widget"]), this,
        &Lektra::ToggleLLMWidget);
    m_actionToggleLLMWidget->setCheckable(true);

    m_actionToggleLLMWidget->setChecked(m_config.llm_widget.visible);
#endif

    m_actionCommandPicker = m_toggleMenu->addAction(
        tr("Command Picker\t%1").arg(m_config.keybinds["command_picker"]), this,
        &Lektra::Show_command_picker);

    m_actionToggleOutline = m_toggleMenu->addAction(
        tr("Outline\t%1").arg(m_config.keybinds["picker_outline"]), this,
        &Lektra::ShowOutline);

    m_actionToggleHighlightAnnotSearch = m_toggleMenu->addAction(
        tr("Highlight Annotation Search\t%1")
            .arg(m_config.keybinds["picker_highlight_search"]),
        this, &Lektra::Show_highlight_search);

    m_actionToggleMenubar = m_toggleMenu->addAction(
        tr("Menubar\t%1").arg(m_config.keybinds["menubar"]), this,
        &Lektra::ToggleMenubar);
    m_actionToggleMenubar->setCheckable(true);
    m_actionToggleMenubar->setChecked(!m_menuBar->isHidden());

    m_actionToggleTabBar
        = m_toggleMenu->addAction(tr("Tabs\t%1").arg(m_config.keybinds["tabs"]),
                                  this, &Lektra::ToggleTabBar);
    m_actionToggleTabBar->setCheckable(true);
    m_actionToggleTabBar->setChecked(!m_tab_widget->tabBar()->isHidden());

    m_actionToggleStatusbar = m_toggleMenu->addAction(
        tr("Statusbar\t%1").arg(m_config.keybinds["statusbar"]), this,
        &Lektra::ToggleStatusbar);
    m_actionToggleStatusbar->setCheckable(true);
    m_actionToggleStatusbar->setChecked(!m_statusbar->isHidden());

    m_actionInvertColor = m_viewMenu->addAction(
        tr("Invert Color\t%1").arg(m_config.keybinds["invert_color"]), this,
        &Lektra::InvertColor);
    m_actionInvertColor->setCheckable(true);
    m_actionInvertColor->setChecked(m_config.behavior.invert_mode);

    // --- Tools Menu ---

    QMenu *toolsMenu = m_menuBar->addMenu(tr("Tools"));

    m_modeMenu = toolsMenu->addMenu(tr("Mode"));

    QActionGroup *modeActionGroup = new QActionGroup(this);
    modeActionGroup->setExclusive(true);

    m_actionRegionSelect = m_modeMenu->addAction(
        tr("Region Selection\t%1")
            .arg(m_config.keybinds["selection_mode_region"]),
        this, &Lektra::ToggleRegionSelect);
    m_actionRegionSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionRegionSelect);

    m_actionTextSelect = m_modeMenu->addAction(
        tr("Text Selection\t%1").arg(m_config.keybinds["selection_mode_text"]),
        this, &Lektra::ToggleTextSelection);
    m_actionTextSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionTextSelect);

    m_actionTextHighlight = m_modeMenu->addAction(
        tr("Text Highlight\t%1").arg(m_config.keybinds["annot_highlight_mode"]),
        this, &Lektra::ToggleTextHighlight);
    m_actionTextHighlight->setCheckable(true);
    modeActionGroup->addAction(m_actionTextHighlight);

    m_actionAnnotRect = m_modeMenu->addAction(
        tr("Annotate Rectangle\t%1").arg(m_config.keybinds["annot_rect_mode"]),
        this, &Lektra::ToggleAnnotRect);
    m_actionAnnotRect->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotRect);

    m_actionAnnotEdit = m_modeMenu->addAction(
        tr("Edit Annotations\t%1").arg(m_config.keybinds["annot_edit_mode"]),
        this, &Lektra::ToggleAnnotSelect);
    m_actionAnnotEdit->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotEdit);

    m_actionAnnotPopup = m_modeMenu->addAction(
        tr("Annotate Popup\t%1").arg(m_config.keybinds["annot_popup_mode"]),
        this, &Lektra::ToggleAnnotPopup);
    m_actionAnnotPopup->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotPopup);

    // TODO: Store visual line mode state in config
    m_actionVisualLineMode = m_modeMenu->addAction(
        tr("Visual Line Mode\t%1").arg(m_config.keybinds["visual_line_mode"]),
        this, &Lektra::ToggleVisualLineMode);
    m_actionVisualLineMode->setCheckable(true);
    modeActionGroup->addAction(m_actionVisualLineMode);

    m_actionNoneMode = m_modeMenu->addAction(
        tr("None\t%1").arg(m_config.keybinds["none_mode"]), this,
        &Lektra::ToggleNoneMode);
    m_actionNoneMode->setCheckable(true);
    modeActionGroup->addAction(m_actionNoneMode);

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

        case GraphicsView::Mode::VisualLine:
            m_actionVisualLineMode->setChecked(true);
            break;

        case GraphicsView::Mode::None:
            m_actionNoneMode->setChecked(true);
            break;

        default:
            break;
    }

    m_actionEncrypt = toolsMenu->addAction(
        tr("Encrypt Document\t%1").arg(m_config.keybinds["file_encrypt"]), this,
        &Lektra::EncryptDocument);
    m_actionEncrypt->setEnabled(false);

    m_actionDecrypt = toolsMenu->addAction(
        tr("Decrypt Document\t%1").arg(m_config.keybinds["file_decrypt"]), this,
        &Lektra::DecryptDocument);
    m_actionDecrypt->setEnabled(false);

    // --- Navigation Menu ---
    m_navMenu = m_menuBar->addMenu(tr("&Navigation"));

    m_navMenu->addAction(
        tr("StartPage\t%1").arg(m_config.keybinds["show_startup_widget"]), this,
        &Lektra::showStartupWidget);

    m_actionGotoPage = m_navMenu->addAction(
        tr("Goto Page\t%1").arg(m_config.keybinds["page_goto"]), this,
        [this]() { Lektra::Goto_page(); });

    m_actionFirstPage = m_navMenu->addAction(
        tr("First Page\t%1").arg(m_config.keybinds["page_first"]), this,
        &Lektra::FirstPage);

    m_actionPrevPage = m_navMenu->addAction(
        tr("Previous Page\t%1").arg(m_config.keybinds["page_prev"]), this,
        &Lektra::PrevPage);

    m_actionNextPage = m_navMenu->addAction(
        tr("Next Page\t%1").arg(m_config.keybinds["page_next"]), this,
        &Lektra::NextPage);
    m_actionLastPage = m_navMenu->addAction(
        tr("Last Page\t%1").arg(m_config.keybinds["page_last"]), this,
        &Lektra::LastPage);

    m_actionPrevLocation = m_navMenu->addAction(
        tr("Previous Location\t%1").arg(m_config.keybinds["location_prev"]),
        this, &Lektra::GoBackHistory);
    m_actionNextLocation = m_navMenu->addAction(
        tr("Next Location\t%1").arg(m_config.keybinds["location_next"]), this,
        &Lektra::GoForwardHistory);

    QMenu *markMenu = m_navMenu->addMenu(tr("Marks"));

    m_actionSetMark = markMenu->addAction(
        tr("Set Mark\t%1").arg(m_config.keybinds["set_mark"]), this,
        [this]() { Lektra::SetMark(); });

    m_actionGotoMark = markMenu->addAction(
        tr("Goto Mark\t%1").arg(m_config.keybinds["goto_mark"]), this,
        [this]() { Lektra::GotoMark(); });

    m_actionDeleteMark = markMenu->addAction(
        tr("Delete Mark\t%1").arg(m_config.keybinds["delete_mark"]), this,
        [this]() { Lektra::DeleteMark(); });

    /* Help Menu */
    QMenu *helpMenu = m_menuBar->addMenu(tr("&Help"));
    m_actionAbout   = helpMenu->addAction(
        tr("About\t%1").arg(m_config.keybinds["show_about"]), this,
        &Lektra::ShowAbout);

    m_actionShowTutorialFile
        = helpMenu->addAction(tr("Open Tutorial File\t%1")
                                  .arg(m_config.keybinds["show_tutorial_file"]),
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
            this, tr("Error in configuration file"),
            tr("There are one or more error(s) in your config "
               "file:\n%1\n\nLoading default config.")
                .arg(e.what()));
        return;
    }

    // Misc
    if (auto misc = toml["misc"])
    {
        if (auto color_dialog_colors = misc["color_dialog_colors"].as_array())
        {
            m_config.misc.color_dialog_colors.clear();
            for (const auto &color_node : *color_dialog_colors)
            {
                if (auto color_str = color_node.value<std::string>())
                {
                    uint32_t color;
                    if (parseHexColor(*color_str, color))
                        m_config.misc.color_dialog_colors.push_back(
                            rgbaToQColor(color));
                }
            }
        }
    }

    // Portals
    if (auto portal = toml["portal"])
    {
        set_color(portal["border_color"], m_config.portal.border_color);
        set(portal["enabled"], m_config.portal.enabled);
        set(portal["border_width"], m_config.portal.border_width);
        set(portal["respect_parent"], m_config.portal.respect_parent);
        set(portal["dim_inactive"], m_config.portal.dim_inactive);
    }

    // Preview
    if (auto preview = toml["preview"])
    {
        set(preview["border_radius"], m_config.preview.border_radius);
        set(preview["close_on_click_outside"],
            m_config.preview.close_on_click_outside);
        if (preview["size_ratio"].is_table())
        {
            float width{0.6}, height{0.7};

            const auto &size_table = *preview["size_ratio"].as_table();

            if (auto toml_width = size_table["width"].value<int>())
                width = *toml_width;
            if (auto toml_height = size_table["height"].value<int>())
                height = *toml_height;

            if (width > 0 && height > 0)
            {
                m_config.preview.size_ratio = {width, height};
            }
        }
        set(preview["opacity"], m_config.preview.opacity);
    }

    if (auto thumbnail_panel = toml["thumbnail_panel"])
    {
        set(thumbnail_panel["show_page_numbers"],
            m_config.thumbnail.show_page_numbers);
        set(thumbnail_panel["panel_width"], m_config.thumbnail.panel_width);
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
        set_color(window["accent"], m_config.window.accent);
        set_color(window["bg"], m_config.window.bg);

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

        set_title_format_if_present(window["title_format"],
                                    m_config.window.title_format);
    }

    // Annotations
    if (auto annots = toml["annotations"])
    {

        if (auto highlight = annots["highlight"])
        {
            set_color(highlight["color"], m_config.annotations.highlight.color);
            set(highlight["hover_glow"],
                m_config.annotations.highlight.hover_glow);
            set(highlight["comment"], m_config.annotations.highlight.comment);
            set(highlight["comment_marker"],
                m_config.annotations.highlight.comment_marker);
            set(highlight["glow_width"],
                m_config.annotations.highlight.glow_width);
            set_color(highlight["glow_color"],
                      m_config.annotations.highlight.glow_color);
            set(highlight["comment_font_size"],
                m_config.annotations.highlight.comment_font_size);
        }

        if (auto rect = annots["rect"])
        {
            set_color(rect["color"], m_config.annotations.rect.color);
            set(rect["hover_glow"], m_config.annotations.rect.hover_glow);
            set(rect["comment"], m_config.annotations.rect.comment);
            set(rect["comment_marker"],
                m_config.annotations.rect.comment_marker);
            set(rect["glow_width"], m_config.annotations.rect.glow_width);
            set_color(rect["glow_color"], m_config.annotations.rect.glow_color);
            set(rect["comment_font_size"],
                m_config.annotations.rect.comment_font_size);
        }

        if (auto popup = annots["popup"])
        {
            set(popup["hover_glow"], m_config.annotations.popup.hover_glow);
            set(popup["comment"], m_config.annotations.popup.comment);
            set(popup["glow_width"], m_config.annotations.popup.glow_width);
            set_color(popup["glow_color"],
                      m_config.annotations.popup.glow_color);
            set(popup["comment_font_size"],
                m_config.annotations.popup.comment_font_size);
        }
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

        if (auto components = statusbar["components"])
        {
            if (auto mode = components["mode"])
            {
                set(mode["show"], m_config.statusbar.component.mode.show);
                set(mode["text"], m_config.statusbar.component.mode.text);
                set(mode["icon"], m_config.statusbar.component.mode.icon);
            }

            if (auto pagenumber = components["pagenumber"])
            {
                set(pagenumber["show"],
                    m_config.statusbar.component.pagenumber.show);
            }

            if (auto session = components["session"])
            {
                set(session["show"], m_config.statusbar.component.session.show);
            }

            if (auto zoom = components["zoom"])
            {
                set(zoom["show"], m_config.statusbar.component.zoom.show);
            }

            if (auto filename = components["filename"])
            {
                set(filename["show"],
                    m_config.statusbar.component.filename.show);
                set(filename["full_path"],
                    m_config.statusbar.component.filename.full_path);
            }

            if (auto progress = components["progress"])
            {
                set(progress["show"],
                    m_config.statusbar.component.progress.show);
            }
        }
    }

    // Layout
    if (auto layout = toml["layout"])
    {
        if (auto str = layout["mode"])
        {
            DocumentView::LayoutMode mode;

            if (str == "vertical")
                mode = DocumentView::LayoutMode::VERTICAL;
            else if (str == "single")
                mode = DocumentView::LayoutMode::SINGLE;
            else if (str == "horizontal")
                mode = DocumentView::LayoutMode::HORIZONTAL;
            else if (str == "book")
                mode = DocumentView::LayoutMode::BOOK;
            else
                mode = DocumentView::LayoutMode::VERTICAL;

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
        set(zoom["anchor_to_mouse"], m_config.zoom.anchor_to_mouse);
    }

    // Selection
    if (auto selection = toml["selection"])
    {
        set(selection["drag_threshold"], m_config.selection.drag_threshold);
        set(selection["copy_on_select"], m_config.selection.copy_on_select);
        set_color(selection["color"], m_config.selection.color);
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

    // Picker
    if (auto picker = toml["picker"])
    {
        set_picker_shared(picker, m_config.picker);

        // Picker.Keys
        if (auto picker_keys = picker["keys"])
        {
            if (picker_keys.is_table())
            {
                const auto &keys = *picker_keys.as_table();
                const Picker::Keybindings defaults{};

                const auto parse
                    = [](const std::string &s) -> std::optional<QKeyCombination>
                {
                    const auto seq = QKeySequence::fromString(
                        QString::fromStdString(s), QKeySequence::PortableText);
                    if (seq.isEmpty())
                        return std::nullopt;
                    return seq[0];
                };

                const auto get = [&](std::string_view field,
                                     const QList<QKeyCombination> &fallback)
                    -> QList<QKeyCombination>
                {
                    const auto *node = keys.get(field);
                    if (!node)
                        return fallback;

                    if (node->is_string())
                    {
                        if (auto kc
                            = parse(std::string(node->as_string()->get())))
                            return {*kc};
                        return fallback;
                    }
                    else if (node->is_array())
                    {
                        QList<QKeyCombination> result;
                        for (const auto &elem : *node->as_array())
                            if (auto s = elem.value<std::string>())
                                if (auto kc = parse(*s))
                                    result << *kc;
                        return result.isEmpty() ? fallback : result;
                    }

                    return fallback;
                };

                m_picker_keybinds = Picker::Keybindings{
                    .moveDown = get("down", defaults.moveDown),
                    .pageDown = get("page_down", defaults.pageDown),
                    .moveUp   = get("up", defaults.moveUp),
                    .pageUp   = get("page_up", defaults.pageUp),
                    .accept   = get("accept", defaults.accept),
                    .dismiss  = get("dismiss", defaults.dismiss),
                };
            }
        }
    }

    // Apply picker defaults to all picker-like sections.
    // Individual sections parsed below can still override these values.
    inherit_picker_defaults(m_config.picker, m_config.outline);
    inherit_picker_defaults(m_config.picker, m_config.highlight_search);
    inherit_picker_defaults(m_config.picker, m_config.command_palette);

    // Command Palette
    if (auto command_palette = toml["command_palette"])
    {
        set_picker_shared(command_palette, static_cast<Config::Picker &>(
                                               m_config.command_palette));

        set(command_palette["description"],
            m_config.command_palette.description);
        set(command_palette["vscrollbar"], m_config.command_palette.vscrollbar);
        // set(command_palette["show_grid"], m_config.command_palette.grid);
        // TODO: Implement grid in command palette
        set(command_palette["show_shortcuts"],
            m_config.command_palette.show_shortcuts);
        set(command_palette["placeholder_text"],
            m_config.command_palette.placeholder_text);
    }

    // Markers
    if (auto jump_marker = toml["jump_marker"])
    {
        set(jump_marker["enabled"], m_config.jump_marker.enabled);
        set_color(jump_marker["jump_marker"], m_config.jump_marker.color);
        set(jump_marker["fade_duration"], m_config.jump_marker.fade_duration);
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
        set_color(link_hints["bg"], m_config.link_hints.bg);
        set_color(link_hints["fg"], m_config.link_hints.fg);
    }

    // Outline
    if (auto outline = toml["outline"])
    {
        set_picker_shared(outline,
                          static_cast<Config::Picker &>(m_config.outline));

        set(outline["indent_width"], m_config.outline.indent_width);
        set(outline["show_page_numbers"], m_config.outline.show_page_number);
        set(outline["flat_menu"], m_config.outline.flat_menu);
    }

    // Highlight Search
    if (auto highlight_search = toml["highlight_search"])
    {
        set_picker_shared(highlight_search, static_cast<Config::Picker &>(
                                                m_config.highlight_search));
        set(highlight_search["flat_menu"], m_config.highlight_search.flat_menu);
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

    // Search
    if (auto search = toml["search"])
    {
        // set(search["case_sensitive"], m_config.search.case_sensitive);
        // set(search["whole_words"], m_config.search.whole_words);
        set(search["highlight_matches"], m_config.search.highlight_matches);
        set(search["progressive"], m_config.search.progressive);
        set_color(search["match_color"], m_config.search.match_color);
        set_color(search["index_color"], m_config.search.index_color);
    }

#ifdef HAS_SYNCTEX
    if (auto synctex = toml["synctex"])
    {
        set(synctex["enabled"], m_config.synctex.enabled);
        set(synctex["editor_command"], m_config.synctex.editor_command);
    }
#endif

    // Colors
    if (auto colors = toml["colors"])
    {
    }

    // Rendering
    if (auto rendering = toml["rendering"])
    {
        if (auto backend_str = rendering["backend"])
        {
            Config::Rendering::Backend backend{
                Config::Rendering::Backend::Raster};
            if (backend_str == "opengl")
            {
                backend = Config::Rendering::Backend::OpenGL;
            }
            else if (backend_str == "raster")
            {
                backend = Config::Rendering::Backend::Raster;
            }
            else if (backend_str == "auto")
            {
                backend = Config::Rendering::Backend::Auto;
            }
            else
            {
                qWarning() << "Unknown rendering backend in config:"
                           << QString::fromStdString(backend_str.value_or(""));
                qWarning() << "Falling back to `raster` backend.";
            }
            m_config.rendering.backend = backend;
        }

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
        set(behavior["preload_pages"], m_config.behavior.preload_pages);
        set(behavior["confirm_on_quit"], m_config.behavior.confirm_on_quit);
        set(behavior["undo_limit"], m_config.behavior.undo_limit);
        set(behavior["remember_last_visited"],
            m_config.behavior.remember_last_visited);
        set(behavior["always_open_in_new_window"],
            m_config.behavior.always_open_in_new_window);
        set(behavior["page_history"], m_config.behavior.page_history_limit);
        set(behavior["invert_mode"], m_config.behavior.invert_mode);
        set(behavior["dont_invert_images"],
            m_config.behavior.dont_invert_images);
        set(behavior["auto_reload"], m_config.behavior.auto_reload);
        set(behavior["recent_files"], m_config.behavior.recent_files);
        set(behavior["num_recent_files"], m_config.behavior.num_recent_files);
        set(behavior["cache_pages"], m_config.behavior.cache_pages);
    }

    if (auto keybindings = toml["keybindings"])
    {
        if (keybindings["load_defaults"].value_or(true))
            initDefaultKeybinds();

        for (auto &[action, value] : *keybindings.as_table())
        {
            if (value.is_value())
                setupKeybinding(
                    QString::fromStdString(std::string(action.str())),
                    QString::fromStdString(value.value_or<std::string>("")));
            else if (value.is_array())
            {
                QStringList keys;
                for (const auto &elem : *value.as_array())
                {
                    if (auto s = elem.value<std::string>())
                        keys << QString::fromStdString(*s);
                }
                setupKeybinding(
                    QString::fromStdString(std::string(action.str())), keys);
            }
        }
    }

    if (auto mbs = toml["mousebindings"])
    {
        for (auto &[action, value] : *mbs.as_table())
        {
            if (value.is_value())
            {
                setupMousebinding(
                    QString::fromStdString(std::string(action.str())),
                    QString::fromStdString(value.value_or<std::string>("")));
            }
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
        {"scroll_down_half_page", "Ctrl+d"},
        {"scroll_up_half_page", "Ctrl+u"},
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
        {"split_horizontal", "Ctrl+W,s"},
        {"split_vertical", "Ctrl+W,v"},
        {"split_focus_left", "Ctrl+W,h"},
        {"split_focus_right", "Ctrl+W,l"},
        {"split_focus_up", "Ctrl+W,k"},
        {"split_focus_down", "Ctrl+W,j"},
        {"split_close", "Ctrl+W,c"},
        {"files_recent", "Alt+Shift+o"},
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
    for (auto it = m_config.keybinds.constBegin();
         it != m_config.keybinds.constEnd(); ++it)
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
        conflicts.append(tr("%1 -> %2").arg(keyDisplay, actions));
    }

    if (conflicts.isEmpty())
        return;

    const int maxItems = 3;
    QString message;
    if (conflicts.size() <= maxItems)
    {
        message = tr("Shortcut conflict(s): %1").arg(conflicts.join("; "));
    }
    else
    {
        message = tr("Shortcut conflict(s): %1; and %2 more")
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
    widget->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_menuBar    = this->menuBar();
    m_tab_widget = new TabWidget(centralWidget());

    // Statusbar
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
        auto *it = m_command_manager->find(action);
        it->action(args);
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

    m_marks_manager = std::make_unique<MarkManager>(this);

    m_layout->setContentsMargins(0, 0, 0, 0);
    setContentsMargins(0, 0, 0, 0);
}

// Updates the UI elements checking if valid
// file is open or not (and if it's PDF or not, for PDF-specific actions)
void
Lektra::updateUiEnabledState() noexcept
{
    const bool hasFile = m_doc != nullptr;
    const Model::FileType filetype
        = hasFile ? m_doc->model()->fileType() : Model::FileType::NONE;

    const bool isPDF = (filetype == Model::FileType::PDF);

    // Text selection — supported for formats with a text layer
    const bool hasTextLayer
        = (filetype == Model::FileType::PDF || filetype == Model::FileType::EPUB
           || filetype == Model::FileType::FB2
           || filetype == Model::FileType::MOBI
           || filetype == Model::FileType::XPS);

    // Format-agnostic — enabled whenever any file is open
    m_actionOpenContainingFolder->setEnabled(hasFile);
    m_actionZoomIn->setEnabled(hasFile);
    m_actionZoomOut->setEnabled(hasFile);
    m_actionGotoPage->setEnabled(hasFile);
    m_actionFirstPage->setEnabled(hasFile);
    m_actionPrevPage->setEnabled(hasFile);
    m_actionNextPage->setEnabled(hasFile);
    m_actionLastPage->setEnabled(hasFile);
    m_actionCloseFile->setEnabled(hasFile);
    m_fitMenu->setEnabled(hasFile);
    m_modeMenu->setEnabled(hasFile);
    m_actionInvertColor->setEnabled(hasFile);
    m_actionPrevLocation->setEnabled(hasFile);
    m_actionNextLocation->setEnabled(hasFile);
    m_actionSessionSave->setEnabled(hasFile);
    m_actionSessionSaveAs->setEnabled(!m_session_name.isEmpty());
    m_actionSetMark->setEnabled(hasFile);
    m_actionGotoMark->setEnabled(hasFile);
    m_actionDeleteMark->setEnabled(hasFile);
    m_actionToggleHighlightAnnotSearch->setEnabled(hasFile);
    m_actionVisualLineMode->setEnabled(hasFile);
    m_actionRegionSelect->setEnabled(hasFile);

    // Selection actions enabled only if text layer is present
    m_actionTextSelect->setEnabled(hasTextLayer);

    // PDF-only
    m_actionSaveFile->setEnabled(isPDF);
    m_actionSaveAsFile->setEnabled(isPDF);
    m_actionEncrypt->setEnabled(isPDF);
    m_actionDecrypt->setEnabled(isPDF);
    m_actionAnnotRect->setEnabled(isPDF);
    m_actionAnnotEdit->setEnabled(isPDF);
    m_actionAnnotPopup->setEnabled(isPDF);
    m_actionTextHighlight->setEnabled(isPDF);
    m_actionFileProperties->setEnabled(isPDF);

    // Undo/redo managed by canUndoChanged/canRedoChanged signals —
    // only reset to false when no file is open
    if (!hasFile)
    {
        m_actionUndo->setEnabled(false);
        m_actionRedo->setEnabled(false);
    }

    updateSelectionModeActions();
}

// Helper function to construct `QShortcut` Qt shortcut
// from the config file
void
Lektra::setupKeybinding(const QString &action, const QString &key) noexcept
{
    setupKeybinding(action, QStringList{key});
}

void
Lektra::setupKeybinding(const QString &action, const QStringList &keys) noexcept
{
    const Command *command = m_command_manager->find(action);
    if (!command)
        return;

    const auto existing = findChildren<QShortcut *>(action);
    for (QShortcut *s : existing)
        delete s;

    for (const QString &key : keys)
    {
        if (key.isEmpty())
            continue;

        QShortcut *shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setObjectName(action);
        connect(shortcut, &QShortcut::activated,
                [command]() { command->action({}); });
#ifndef NDEBUG
        qDebug() << "Keybinding set:" << action << "->" << key;
#endif
        m_config.keybinds[action] = key;
    }
}

// Convert a mouse binding string from the config file into a MouseBindKey
// struct
Config::MouseBinding
get_mouse_bind_key(const QString &trigger) noexcept
{
    Config::MouseBinding binding;

    const QStringList parts = trigger.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return binding;

    const QString key = parts.last().trimmed();

    // Determine trigger type
    if (key.compare("LeftButton", Qt::CaseInsensitive) == 0)
        binding.button = Qt::LeftButton;
    else if (key.compare("RightButton", Qt::CaseInsensitive) == 0)
        binding.button = Qt::RightButton;
    else if (key.compare("MiddleButton", Qt::CaseInsensitive) == 0)
        binding.button = Qt::MiddleButton;
    else
    {
        qWarning() << "Unknown mouse trigger:" << key;
        return binding;
    }

    // Parse modifiers
    for (int i = 0; i < parts.size() - 1; ++i)
    {
        const QString mod = parts[i].trimmed();
        if (mod.compare("Ctrl", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::ControlModifier;
        else if (mod.compare("Shift", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::ShiftModifier;
        else if (mod.compare("Alt", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::AltModifier;
        else if (mod.compare("Meta", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::MetaModifier;
        else
        {
            qWarning() << "Unknown modifier in mouse binding:" << mod;
            return Config::MouseBinding{};
        }
    }

    return binding;
}

void
Lektra::setupMousebinding(const QString &action_str,
                          const QString &trigger) noexcept
{
#ifndef NDEBUG
    qDebug() << "Mousebinding set:" << action_str << "->" << trigger;
#endif

    Config::MouseBinding binding = get_mouse_bind_key(trigger);
    if (!binding.isValid())
    {
        qWarning() << tr("Invalid mouse binding for action") << action_str
                   << ":" << trigger;
        return;
    }

    if (action_str.isEmpty())
    {
        qWarning() << tr("Empty action for mouse binding with trigger:")
                   << trigger;
        return;
    }

    // Resolve action string to actual command
    GraphicsView::MouseAction action;

    if (action_str == "portal")
        action = GraphicsView::MouseAction::Portal;

    else if (action_str == "synctex_jump")
        action = GraphicsView::MouseAction::SynctexJump;

    else if (action_str == "preview")
        action = GraphicsView::MouseAction::Preview;
    else
    {
        qWarning() << tr("Unknown action for mouse binding:") << action_str;
        return;
    }

    binding.action = action;
    m_config.mousebinds.push_back(binding);
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

// Toggles the statusbar
void
Lektra::ToggleStatusbar() noexcept
{
    bool shown = !m_statusbar->isHidden();
    m_statusbar->setHidden(shown);
    m_actionToggleStatusbar->setChecked(!shown);
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
    static AboutDialog *abw = new AboutDialog(this);

    if (!abw)
    {
        abw = new AboutDialog(this);
        connect(abw, &QObject::destroyed, []() { abw = nullptr; });
    }

    abw->show();
    abw->raise();
    abw->activateWindow();
}

// Reads the arguments passed with `Lektra` from the
// commandline
void
Lektra::Read_args_parser(const argparse::ArgumentParser &argparser) noexcept
{

    if (argparser.is_used("version"))
    {
        QTextStream out(stdout);
        out << "Lektra version: " << APP_VERSION;
        exit(0);
    }

    if (argparser.is_used("check-config"))
    {
        QString config_file_path;

        try
        {
            config_file_path = QString::fromStdString(
                argparser.get<std::string>("--check-config"));
        }
        catch (const std::exception &e)
        {
            qWarning() << tr(
                "No config file path provided, using default path:")
                       << m_config_file_path;
            config_file_path = m_config_file_path;
        }

        checkConfigFile(config_file_path);
        exit(0);
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

        exit(0);
    }

    if (argparser.is_used("config"))
    {
        m_config_file_path
            = QString::fromStdString(argparser.get<std::string>("--config"));
    }

    // This creates the UI and applies the initial user config file settings
    this->construct();

    applyCommandLineOverrides(argparser);

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

#ifdef HAS_SYNCTEX
    if (argparser.is_used("synctex-forward"))
    {
        m_config.behavior._startpage_override = -1; // do not override the page

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
            qWarning() << tr("Invalid --synctex-forward format. Expected "
                             "file.pdf#file.tex:line:column");
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
            if (auto *c = m_command_manager->find(cmd_name))
                c->action(args);
            else
                err << tr("Unknown command from command line:") << cmd_name
                    << Qt::endl;
        }
    };

    if (argparser.is_used("files"))
    {
        auto files = argparser.get<std::vector<std::string>>("files");
        m_config.behavior.open_last_visited = false;
        const int pageOverride = m_config.behavior._startpage_override;

        if (!files.empty())
        {

            if (hsplit)
                OpenFilesInHSplit(files);
            else if (vsplit)
                OpenFilesInVSplit(files);
            else
            {
                if (files.size() == 1)
                {
                    // If only one file is passed, open it in a new tab and go
                    // to the specified page (if any)
                    OpenFileInNewTab(QString::fromStdString(files[0]),
                                     [pageOverride, this, runCliCommands]()
                    {
                        if (pageOverride > 0)
                            gotoPage(pageOverride);
                        runCliCommands();
                    });
                }
                else
                {
                    OpenFiles(files);
                    runCliCommands();
                }
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
        else
            mode = DocumentView::LayoutMode::VERTICAL;

        m_config.layout.mode = mode;
    }
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
            this, tr("Edit Last Pages"),
            tr("Couldn't find the recent files data. Maybe "
               "`remember_last_visited` option is turned off in the config "
               "file"));
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
Lektra::Zoom_set(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    double zoom;
    bool ok;

    if (args.isEmpty())
        zoom = QInputDialog::getDouble(
            this, tr("Set Zoom"), tr("Enter zoom level (e.g. 1.5 for 150%):"),
            m_doc->zoom(), 0.1, 10.0, 2, &ok);
    else
        zoom = args.at(0).toDouble(&ok);
    if (!ok)
        return;
    m_doc->setZoom(zoom);
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
Lektra::Goto_page(const QStringList &args) noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    int pageno;
    bool ok;
    int total = m_doc->model()->numPages();

    if (args.isEmpty())
    {
        if (total == 0)
        {
            QMessageBox::information(this, tr("Goto Page"),
                                     tr("This document has no pages"));
            return;
        }

        pageno = QInputDialog::getInt(
            this, tr("Goto Page"), tr("Enter page number (1 to %1)").arg(total),
            m_doc->pageNo() + 1, 0, m_doc->numPages(), 1, &ok);
        if (!ok)
            return;
    }
    else
    {
        pageno = args.at(0).toInt(&ok);
        if (!ok)
            return;
    }

    if (pageno <= 0 || pageno > total)
    {
        QMessageBox::critical(this, tr("Goto Page"),
                              tr("Page %1 is out of range").arg(pageno));
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
Lektra::GotoLocation(const PageLocation &loc) noexcept
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

void
Lektra::ScrollDown_HalfPage() noexcept
{
    if (m_doc)
        m_doc->ScrollDown_HalfPage();
}

void
Lektra::ScrollUp_HalfPage() noexcept
{
    if (m_doc)
        m_doc->ScrollUp_HalfPage();
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

    // Check if file exists
    if (!QFile(filename).exists())
    {
        QMessageBox::critical(
            this, tr("Error"),
            tr("The specified file does not exist:\n%1").arg(filename));
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
            [this, tabIndex](DocumentView *doc, Model::FileType)
    {
        if (validTabIndex(tabIndex))
            m_tab_widget->tabBar()->setTabText(tabIndex, m_config.tabs.full_path
                                                             ? doc->filePath()
                                                             : doc->fileName());
        updateStatusbar();
        updateUiEnabledState();
    }, Qt::SingleShotConnection);

    view->openAsync(filename);

    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());

    setCurrentDocumentView(view); // immediate, like OpenFileInNewTab

    // Restore saved page number after file loads (if remember_last_visited is
    // enabled)
    if (m_config.behavior.remember_last_visited)
    {
        const int savedPage = m_recent_files_store.pageNumber(filename);
        if (savedPage > 0)
        {
            connect(view, &DocumentView::openFileFinished, this,
                    [view, savedPage](DocumentView *, Model::FileType)
            { view->GotoPage(savedPage - 1); }, Qt::SingleShotConnection);
        }
    }

    if (callback)
    {
        connect(view, &DocumentView::openFileFinished, this,
                [callback](DocumentView *, Model::FileType) { callback(); },
                Qt::SingleShotConnection);
    }

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

DocumentView *
Lektra::OpenFileInNewTab(const QString &filename,
                         const std::function<void()> &callback) noexcept
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
                return OpenFileInNewTab(selected.first(), callback);
        }
        return nullptr;
    }

    // Check if file exists
    if (!QFile(filename).exists())
    {
        QMessageBox::critical(
            this, tr("Error"),
            tr("The specified file does not exist:\n%1").arg(filename));
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
    DocumentView *view = new DocumentView(m_config, m_dpr, this);

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

    // Save page number when a split view is closed
    connect(container, &DocumentContainer::viewClosed, this,
            [this](DocumentView *closedView)
    {
        if (m_config.behavior.remember_last_visited && closedView
            && !closedView->filePath().isEmpty() && !closedView->is_portal())
        {
            const int page = closedView->pageNo() + 1;
            insertFileToDB(closedView->filePath(), page > 0 ? page : 1);
        }
    });

    connect(container, &DocumentContainer::currentViewChanged, container,
            [this](DocumentView *newView) { setCurrentDocumentView(newView); });

    // Initialize connections for the initial view
    initTabConnections(view);

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

    // Restore saved page number after file loads (if remember_last_visited is
    // enabled)
    if (m_config.behavior.remember_last_visited)
    {
        const int savedPage = m_recent_files_store.pageNumber(filename);
        if (savedPage > 0)
        {
            connect(view, &DocumentView::openFileFinished, this,
                    [view, savedPage](DocumentView *, Model::FileType)
            { view->GotoPage(savedPage - 1); }, Qt::SingleShotConnection);
        }
    }

    if (callback)
    {
        connect(view, &DocumentView::openFileFinished, this,
                [callback](DocumentView *view, Model::FileType)
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

    if (!currentView || currentView == container->thumbnailView())
        return nullptr;

    DocumentView *newView
        = container->split(currentView, orientation, filename);

    m_tab_widget->tabBar()->set_split_count(tabIndex,
                                            container->getViewCount());

    // Restore saved page number after file loads (if remember_last_visited is
    // enabled)
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
        connect(newView, &DocumentView::openFileFinished, this,
                [callback](DocumentView *, Model::FileType) { callback(); },
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
Lektra::ShowOutline() noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    if (!m_doc->model()->getOutline())
    {
        QMessageBox::information(this, tr("Outline"),
                                 tr("This document has no outline"));
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
        m_outline_picker->setCurrentPage(m_doc->pageNo() + 1);
        m_outline_picker->launch();
        m_outline_picker->selectCurrentPage();
    }
}

// Show the highlight search panel
void
Lektra::Show_highlight_search() noexcept
{
    if (!m_doc && !m_doc->model()->supports_annotations())
        return;

    if (!m_highlight_search_picker)
    {
        m_highlight_search_picker
            = new HighlightSearchPicker(m_config.highlight_search, this);
        m_highlight_search_picker->setKeybindings(m_picker_keybinds);

        connect(m_highlight_search_picker,
                &HighlightSearchPicker::gotoLocationRequested, this,
                [this](int page, float x, float y)
        { GotoLocation(page, x, y); });
    }

    m_highlight_search_picker->setModel(m_doc->model());
    m_highlight_search_picker->launch();
}

void
Lektra::Show_annot_comment_search() noexcept
{
    if (!m_doc && !m_doc->model()->supports_annotations())
        return;

    if (!m_comment_search_picker)
    {
        m_comment_search_picker
            = new CommentSearchPicker(m_config.picker, this);
        m_comment_search_picker->setKeybindings(m_picker_keybinds);

        connect(m_comment_search_picker,
                &CommentSearchPicker::gotoLocationRequested, this,
                [this](int page, float x, float y)
        { GotoLocation(page, x, y); });
    }

    m_comment_search_picker->setModel(m_doc->model());
    m_comment_search_picker->launch();
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
            QMessageBox::information(this, tr("Toggle Text Highlight"),
                                     tr("Not a PDF file to annotate"));
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
            QMessageBox::information(this, tr("Toggle Annot Rect"),
                                     tr("Not a PDF file to annotate"));
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
            QMessageBox::information(this, tr("Toggle Annot Select"),
                                     tr("Not a PDF file to annotate"));
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
        QMessageBox::information(this, tr("Toggle Annot Popup"),
                                 tr("Not a PDF file to annotate"));
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
        m_doc->handleTextHighlightRequested();
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

        // Save page numbers for all views in this tab before closing
        if (m_config.behavior.remember_last_visited)
        {
            DocumentContainer *container = m_tab_widget->rootContainer(index);
#ifndef NDEBUG
            qDebug() << "tabCloseRequested: remember_last_visited enabled, "
                        "container:"
                     << container;
#endif
            if (container)
            {
                const auto views = container->getAllViews();
#ifndef NDEBUG
                qDebug() << "tabCloseRequested: found" << views.size()
                         << "views";
#endif
                for (DocumentView *view : views)
                {
#ifndef NDEBUG
                    qDebug()
                        << "tabCloseRequested: view:" << view
                        << "filePath:" << (view ? view->filePath() : "null")
                        << "is_portal:" << (view ? view->is_portal() : false);
#endif
                    if (view && !view->filePath().isEmpty()
                        && !view->is_portal())
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
    m_statusbar->setFilePath(name);
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

        // Update statusbar if needed
        updateStatusbar();
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

            if (m_config.behavior.remember_last_visited && !doc->is_portal())
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
                // Check if click is on the overlay background (not the inner
                // container)
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

    // Context menu for the tab widgets
    if (object == m_tab_widget || object == m_tab_widget->tabBar())
    {
        if (type == QEvent::ContextMenu)
            return handleTabContextMenu(object, event);
    }

    // Let other events pass through
    return QObject::eventFilter(object, event);
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
    menu.addAction(tr("Open Location"), this,
                   [this, index]() { openInExplorerForIndex(index); });
    menu.addAction(tr("File Properties"), this,
                   [this, index]() { filePropertiesForIndex(index); });
    menu.addSeparator();
    menu.addAction(tr("Move Tab to New Window"), this, [this, index]()
    {
        TabBar::TabData data;
        handleTabDataRequested(index, &data);
        if (!data.filePath.isEmpty())
            handleTabDetachedToNewWindow(index, data);
    });
    menu.addAction(tr("Close Tab"), this,
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
    connect(docwidget, &DocumentView::statusbarNameChanged, m_statusbar,
            &Statusbar::setFilePath);

    connect(docwidget, &DocumentView::openFileFinished, this,
            [this](DocumentView *doc, Model::FileType /* ft */)
    {
        // Only update the statusbar if this view is the currently active one.
        // If it's a background split, don't clobber the active view's info.
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
            }
            updateUiEnabledState();
        }
    });

    connect(docwidget, &DocumentView::openFileFailed, this,
            [this](DocumentView *doc)
    {
        const bool wasCurrentView = (m_doc == doc);
        doc->CloseFile();
        DocumentContainer *container = doc->container();
        if (!container)
            return;

        // If this is the only view in the container, remove the entire tab
        // Otherwise just close this view within the split
        if (container->getViewCount() <= 1)
        {
            const int tabIndex = m_tab_widget->indexOf(container);
            if (tabIndex != -1)
                m_tab_widget->removeTab(tabIndex);
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
        m_statusbar->setFilePath("");
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
        QMessageBox::information(this, tr("Load Session"),
                                 tr("No sessions found"));
        return;
    }

    if (sessionName.isEmpty())
    {
        bool ok;
        sessionName = QInputDialog::getItem(
            this, tr("Load Session"),
            tr("Session to load (existing sessions are listed): "),
            existingSessions, 0, true, &ok);
    }

    QFile file(m_session_dir.filePath(sessionName + ".json"));

    if (file.open(QIODevice::ReadOnly))
    {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);

        if (err.error != QJsonParseError::NoError)
        {
            QMessageBox::critical(this, tr("Session File Parse Error"),
                                  err.errorString());
#ifndef NDEBUG
            qDebug() << "JSON parse error:" << err.errorString();
#endif
            return;
        }

        if (!doc.isArray())
        {
            QMessageBox::critical(this, tr("Session File Parse Error"),
                                  tr("Session file root is not an array"));
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
            this, tr("Open Session"),
            tr("Could not open session: %1").arg(sessionName));
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
            QMessageBox::warning(this, tr("Session Directory"),
                                 tr("Unable to create sessions directory due "
                                    "to an unknown error."));
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
        QMessageBox::information(this, tr("Save Session"),
                                 tr("No files in session to save the session"));
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
            QMessageBox::information(this, tr("Save Session"),
                                     tr("Session name cannot be empty"));
            return;
        }

        if (m_session_name != sessionName)
        {
            // Ask for overwrite if session with same name exists
            if (existingSessions.contains(sessionName))
            {
                auto choice = QMessageBox::warning(
                    this, tr("Overwrite Session"),
                    tr("Session named \"%1\" already exists. Do you "
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
            this, tr("Save Session"),
            tr("Could not save session: %1").arg(m_session_name));
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
            this, tr("Save As Session"),
            tr("Cannot save session as you are not currently in a session"));
        return;
    }

    QStringList existingSessions = getSessionFiles();

    QString selectedPath = QFileDialog::getSaveFileName(
        this, tr("Save As Session"), m_session_dir.absolutePath(),
        tr("Lektra session files") + " " + "(*.json);" + tr("All Files") + " "
            + "(*.*)");

    if (selectedPath.isEmpty())
        return;

    if (QFile::exists(selectedPath))
    {
        auto choice = QMessageBox::warning(
            this, tr("Overwrite Session"),
            tr("Session named \"%1\" already exists. Do you want to "
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
        QMessageBox::critical(this, tr("Save As Session"),
                              tr("Failed to save session."));
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
    int index = m_tab_widget->addTab(m_startup_widget, tr("Startup"));
    m_tab_widget->setCurrentIndex(index);
    m_statusbar->setFilePath(tr("Start Page"));
}

// Update actions and stuff for system tabs
void
Lektra::updateActionsAndStuffForSystemTabs() noexcept
{
    m_statusbar->hidePageInfo(true);
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

    m_command_manager->reg("presentation_mode", tr("Toggle presentation mode"),
                           [this](const QStringList &)
    { TogglePresentationMode(); });
    m_command_manager->reg("fullscreen", tr("Toggle fullscreen"),
                           [this](const QStringList &) { ToggleFullscreen(); });
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

#ifdef ENABLE_LLM_SUPPORT
    m_command_manager->reg("llm_widget", tr("Toggle LLM assistant widget"),
                           [this](const QStringList &) { ToggleLLMWidget(); });
#endif

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

    // Rotation
    m_command_manager->reg("rotate_clock", tr("Rotate page clockwise"),
                           [this](const QStringList &) { RotateClock(); });
    m_command_manager->reg("rotate_anticlock",
                           tr("Rotate page counter-clockwise"),
                           [this](const QStringList &) { RotateAnticlock(); });

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
    m_command_manager->reg(
        "annot_highlight_mode", tr("Toggle text highlight mode"),
        [this](const QStringList &) { ToggleTextHighlight(); });
    m_command_manager->reg("none_mode", tr("Toggle none interaction mode"),
                           [this](const QStringList &) { ToggleNoneMode(); });

    // Selection modes
    m_command_manager->reg(
        "selection_mode_text", tr("Switch to text selection mode"),
        [this](const QStringList &) { ToggleTextSelection(); });
    m_command_manager->reg(
        "selection_mode_region", tr("Switch to region selection mode"),
        [this](const QStringList &) { ToggleRegionSelect(); });

    // Fit modes
    m_command_manager->reg("fit_width", tr("Fit page to window width"),
                           [this](const QStringList &) { Fit_width(); });
    m_command_manager->reg("fit_height", tr("Fit page to window height"),
                           [this](const QStringList &) { Fit_height(); });
    m_command_manager->reg("fit_page", tr("Fit entire page in window"),
                           [this](const QStringList &) { Fit_page(); });
    m_command_manager->reg("fit_auto", tr("Toggle automatic resize to fit"),
                           [this](const QStringList &) { ToggleAutoResize(); });

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
    m_command_manager->reg(
        "search_from_here", tr("Search from current position"),
        [this](const QStringList &args) { SearchFromHere(args); });
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

    // Miscellaneous
    m_command_manager->reg("preview", tr("Show the preview window"),
                           [this](const QStringList &)
    {
        if (m_preview_overlay)
        {
            m_preview_overlay->show();
        }
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
        qWarning() << tr("Failed to trim recent files store");
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

// Reload the document in place
void
Lektra::reloadDocument() noexcept
{
    if (m_doc)
    {
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
        index = QInputDialog::getInt(this, tr("Go to Tab"),
                                     tr("Enter tab number: "), 1, 1,
                                     m_tab_widget->count());
    }

    if (index > 0 || index < m_tab_widget->count())
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
Lektra::search(const QString &term, bool use_regex) noexcept
{
    if (m_doc)
        m_doc->Search(term, use_regex);
}

void
Lektra::searchCancel() noexcept
{
    if (m_doc)
    {
        m_doc->SearchCancel();
    }
}

void
Lektra::searchInPage(const int pageno, const QString &term) noexcept
{
    if (m_doc)
        m_doc->SearchInPage(pageno, term);
}

void
Lektra::Search(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    if (args.isEmpty())
    {
        if (m_doc->model()->supports_text_search())
        {
            m_search_bar->setVisible(true);
            m_search_bar->focusSearchInput();
        }
        else
        {
            QMessageBox::information(
                this, tr("Search Not Supported"),
                tr("The current document does not support text search."));
        }
    }
    else
    {
        m_doc->Search(args.at(0), false);
    }
}

void
Lektra::SearchRegex(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    if (args.isEmpty())
    {
        m_search_bar->setVisible(true);
        m_search_bar->setRegexMode(true);
        m_search_bar->focusSearchInput();
    }
    else
    {
        m_doc->Search(args.at(0), true);
    }
}

void
Lektra::SearchFromHere(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    if (args.isEmpty())
    {
        m_search_bar->setVisible(true);
        m_search_bar->focusSearchInput();
    }
    else
    {
        m_doc->SearchFromHere(args.at(0), false);
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
    ColorDialog colorDialog(m_config.misc.color_dialog_colors, this);
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
    }
}

void
Lektra::Show_command_picker() noexcept
{
    if (!m_command_picker)
    {
        m_command_picker = new CommandPicker(m_config.command_palette,
                                             m_command_manager->commands(),
                                             m_config.keybinds, this);
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
    const QString doc_path = AppPaths::appTutorialPath();
    if (!doc_path.isEmpty() && QFileInfo::exists(doc_path))
    {
        OpenFileInNewTab(doc_path);
        return;
    }

#if defined(__linux__) || defined(__APPLE__) && defined(__MACH__)
    QMessageBox::warning(this, tr("Show Tutorial File"),
                         tr("Tutorial file could not be found."));
#elif defined(_WIN64)
    QMessageBox::warning(this, "Show Tutorial File",
                         tr("Not yet implemented for Windows"));
#endif
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

void
Lektra::splitHelper(Qt::Orientation orientation) noexcept
{
    int currentTabIndex = m_tab_widget->currentIndex();
    if (!validTabIndex(currentTabIndex))
        return;

    // Get the container for this tab
    DocumentContainer *container = m_tab_widget->rootContainer(currentTabIndex);
    if (!container)
        return;

    DocumentView *currentView = container->view();
    if (!currentView || currentView == container->thumbnailView())
        return;

    // Perform vertical split (top/bottom)
    container->split(currentView, orientation);
    m_tab_widget->tabBar()->set_split_count(currentTabIndex,
                                            container->getViewCount());
}

void
Lektra::VSplit() noexcept
{
    splitHelper(Qt::Vertical);
}

void
Lektra::HSplit() noexcept
{
    splitHelper(Qt::Horizontal);
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
    updateStatusbar();
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
Lektra::CloseFile(const QString &filename) noexcept
{
    Q_UNUSED(filename);

    if (!m_doc)
        return;

    int indexToClose = m_tab_widget->currentIndex();
    Tab_close(indexToClose);
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

        // Check if file exists
        if (!QFile(path).exists())
        {
            QMessageBox::critical(
                this, tr("Error"),
                tr("The specified file does not exist:\n%1").arg(path));
            return;
        }

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

    auto navigateTo = [this, target]()
    {
        m_preview_view->setZoom(m_doc->zoom());
        m_preview_view->GotoLocation(target);
    };

    if (m_preview_view->filePath() != view->filePath())
    {
        connect(m_preview_view, &DocumentView::openFileFinished, this,
                [navigateTo](DocumentView *, Model::FileType)
        { QTimer::singleShot(0, navigateTo); }, Qt::SingleShotConnection);
        m_preview_view->openAsync(view->filePath());
    }
    else
    {
        navigateTo();
    }
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
Lektra::TogglePresentationMode() noexcept
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
        { OpenFileInNewTab(file, [this]() { m_doc->setFocus(); }); });
    }

    // Always update the recent files list before launching
    m_recent_file_picker->setRecentFiles(recentFiles);
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
        qWarning() << tr("Reopen_last_file: file no longer exists:")
                   << target->file_path;
        return;
    }

    const int savedPage  = target->page_number;
    const QString &fpath = target->file_path;

    OpenFileInNewTab(fpath, [this, savedPage]() { gotoPage(savedPage); });
}

void
Lektra::SetMark(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    QString key;

    if (args.isEmpty())
    {

        key = QInputDialog::getText(
            this, tr("Set Mark"),
            tr("Enter mark key (a-z for local, A-Z for global):"));

        if (key.isEmpty())
        {
            QMessageBox::critical(this, tr("Set Mark"),
                                  tr("Mark key cannot be empty"));
            return;
        }
    }
    else
    {
        key = args.at(0);
    }

    if (m_marks_manager->isGlobalKey(key))
        m_marks_manager->addGlobalMark(key, m_doc->id(),
                                       m_doc->CurrentLocation());
    else
        m_marks_manager->addLocalMark(key, m_doc->id(),
                                      m_doc->CurrentLocation());
}

void
Lektra::DeleteMark(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    QString key;
    if (args.isEmpty())
    {
        const QStringList existingMarks = m_marks_manager->allKeys(m_doc->id());
        key = QInputDialog::getItem(this, tr("Delete Mark"),
                                    tr("Mark to delete:"), existingMarks, 0);

        if (key.isEmpty())
        {
            QMessageBox::critical(this, tr("Delete Mark"),
                                  tr("Mark key cannot be empty"));
            return;
        }
    }
    else
    {
        key = args.at(0);
    }

    if (m_marks_manager->isGlobalKey(key))
        m_marks_manager->removeGlobalMark(key);
    else
        m_marks_manager->removeLocalMark(key, m_doc->id());
}

void
Lektra::GotoMark(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    QString key;

    if (args.isEmpty())
    {
        const QStringList existingMarks = m_marks_manager->allKeys(m_doc->id());
        key = QInputDialog::getItem(this, tr("Goto Mark"), tr("Mark to go to:"),
                                    existingMarks, 0);

        if (key.isEmpty())
        {
            QMessageBox::critical(this, tr("Goto Mark"),
                                  tr("Mark key cannot be empty"));
            return;
        }
    }
    else
    {
        key = args.at(0);
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
Lektra::ToggleVisualLineMode() noexcept
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

void
Lektra::ToggleNoneMode() noexcept
{
    if (!m_doc)
        return;

    bool oldState = m_doc->graphicsView()->mode() == GraphicsView::Mode::None;
    m_doc->set_visual_line_mode(!oldState);

    if (!oldState)
        m_statusbar->setMode(GraphicsView::Mode::None);
    else
        m_statusbar->setMode(m_doc->graphicsView()->getDefaultMode());
}

void
Lektra::ToggleCommentMarkers() noexcept
{
    if (!m_doc)
        return;

    // Toggle in config so that new documents also reflect the change
    m_config.annotations.highlight.comment_marker
        = !m_config.annotations.highlight.comment_marker;
    m_config.annotations.rect.comment_marker
        = !m_config.annotations.rect.comment_marker;

    m_doc->ToggleCommentMarkers();
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

void
Lektra::OpenConfigFile() noexcept
{
    if (QFile::exists(m_config_file_path))
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_config_file_path));
    }
    else
    {
        QMessageBox::critical(
            this, tr("Error"),
            tr("Config file not found at:\n%1").arg(m_config_file_path));
    }
}

void
Lektra::ToggleThumbnailPanel() noexcept
{
    if (!m_doc)
        return;

    m_doc->ToggleThumbnailPanel();
}

// Check if the config file exists and is a valid TOML file and contains only
// known keys. Print warnings for any issues found. Return true if no issues
// found, false otherwise.
bool
Lektra::checkConfigFile(const QString &path) const noexcept
{

    bool ok         = true;
    const auto warn = [&](const QString &msg)
    {
        std::cerr << "[lektra --check-config] " << msg.toStdString() << "\n";
        ok = false;
    };

    if (!QFile::exists(path))
    {
        warn(QString(tr("Config file not found at: %1")).arg(path));
        return ok;
    }

    toml::table toml;
    try
    {
        toml = toml::parse_file(path.toStdString());
    }
    catch (const std::exception &e)
    {
        warn(QString(tr("TOML parse error: %1")).arg(e.what()));
        return false;
    }
    std::cout << "[lektra --check-config] TOML syntax OK\n";

    static const QHash<QString, QSet<QString>> knownKeys = {
        {"page", {"bg", "fg"}},

        {"synctex", {"enabled", "editor_command"}},

        {"portal",
         {"border_color", "enabled", "border_width", "respect_parent",
          "dim_inactive"}},

        {"preview",
         {"border_radius", "close_on_click_outside", "size_ratio", "opacity"}},

        {"thumbnail_panel",
         {"show_page_numbers", "panel_width", "highlight_current_page",
          "vscrollbar", "hscrollbar"}},

        {"tabs",
         {"visible", "auto_hide", "closable", "movable", "elide_mode",
          "location", "full_path", "lazy_load"}},

        {"window",
         {"startup_tab", "menubar", "fullscreen", "accent", "bg",
          "initial_size", "title_format"}},

        {"annotations", {"highlight", "rect", "popup"}},

        {"statusbar",
         {"visible", "padding", "show_progress", "file_name_only",
          "show_file_info", "show_page_number", "show_mode",
          "show_session_name"}},

        {"layout", {"mode", "initial_fit", "auto_resize", "spacing"}},

        {"zoom", {"level", "factor", "anchor_to_mouse"}},

        {"selection", {"drag_threshold", "copy_on_select", "color"}},

        {"scrollbars",
         {"vertical", "horizontal", "search_hits", "auto_hide", "size",
          "hide_timeout"}},

        {"command_palette",
         {"description", "height", "width", "vscrollbar", "show_shortcuts",
          "border", "alternating_row_color", "placeholder_text", "shadow"}},

        {"picker",
         {"width", "height", "border", "alternating_row_color", "shadow",
          "keys"}},

        {"jump_marker", {"enabled", "color", "fade_duration"}},

        {"links", {"enabled", "boundary", "detect_urls", "url_regex"}},

        {"link_hints", {"size", "bg", "fg"}},

        {"outline",
         {"width", "height", "border", "alternating_row_color", "shadow",
          "indent_width", "show_page_numbers"}},

        {"highlight_search",
         {"width", "height", "border", "alternating_row_color", "shadow"}},

        {"search",
         {"highlight_matches", "progressive", "match_color", "index_color",
          "absolute_jump"}},

        {"rendering",
         {"backend", "antialiasing", "text_antialiasing",
          "smooth_pixmap_transform", "antialiasing_bits", "dpr", "cache_pages",
          "icc_color_profile"}},

        {"split",
         {"mouse_follows_focus", "focus_follows_mouse", "dim_inactive",
          "dim_inactive_opacity"}},

        {"behavior",
         {"preload_pages", "confirm_on_quit", "undo_limit",
          "remember_last_visited", "always_open_in_new_window",
          "page_history_limit", "invert_mode", "dont_invert_images",
          "auto_reload", "recent_files", "num_recent_files", "initial_mode",
          "open_last_visited", "file_name_only", "cache_pages"}},

#ifdef ENABLE_LLM_SUPPORT
        {"llm_widget", {"visible", "panel_position", "panel_width"}},

        {"llm", {"provider", "model", "max_tokens"}},
#endif
    };

    for (auto &[key, _] : toml)
    {
        const QString section = QString::fromStdString(std::string(key.str()));

        if (!knownKeys.contains(section))
        {
            warn(QString(tr("Unknown section: [%1]")).arg(section));
            continue;
        }

        const auto *table = toml.get(key.str());
        if (!table || !table->is_table())
            continue;

        for (auto &[k, _] : *table->as_table())
        {
            const QString field = QString::fromStdString(std::string(k.str()));
            if (!knownKeys[section].contains(field))
                warn(QString(tr("Unknown key '%1' in [%2]"))
                         .arg(field, section));
        }
    }

    if (ok)
        qInfo() << tr("[lektra --check-config] All keys valid. Config looks "
                      "good!\n");

    return ok;
}
