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

// Initialize the menubar related stuff
void
Lektra::initMenubar() noexcept
{
    // --- File Menu ---
    QMenu *fileMenu = m_menuBar->addMenu(tr("&File"));

    QAction *actionOpenFile = fileMenu->addAction(
        tr("Open File\t%1").arg(m_config.keybinds["file_open_tab"].join(", ")),
        this, [&]() { OpenFilesInNewTab(); });

    QAction *actionOpenVSplit = fileMenu->addAction(
        tr("Open File In VSplit\t%1")
            .arg(m_config.keybinds["file_open_vsplit"].join(", ")),
        this, [&]() { OpenFilesInVSplit(); });

    QAction *actionOpenHSplit = fileMenu->addAction(
        tr("Open File In HSplit\t%1")
            .arg(m_config.keybinds["file_open_hsplit"].join(", ")),
        this, [&]() { OpenFilesInHSplit(); });

    m_recentFilesMenu = fileMenu->addMenu(tr("Recent Files"));

    m_actionFileProperties = fileMenu->addAction(
        tr("File Properties\t%1")
            .arg(m_config.keybinds["file_properties"].join(", ")),
        this, &Lektra::FileProperties);

    m_actionOpenContainingFolder = fileMenu->addAction(
        tr("Open Containing Folder\t%1")
            .arg(m_config.keybinds["open_containing_folder"].join(", ")),
        this, &Lektra::OpenContainingFolder);
    m_actionOpenContainingFolder->setEnabled(false);

    m_actionSaveFile = fileMenu->addAction(
        tr("Save File\t%1").arg(m_config.keybinds["file_save"].join(", ")),
        this, [this]() { Lektra::SaveFile(); });

    m_actionSaveAsFile = fileMenu->addAction(
        tr("Save As File\t%1")
            .arg(m_config.keybinds["file_save_as"].join(", ")),
        this, [this]() { Lektra::SaveAsFile(); });

    QMenu *sessionMenu = fileMenu->addMenu(tr("Session"));

    m_actionSessionSave = sessionMenu->addAction(
        tr("Save\t%1").arg(m_config.keybinds["session_save"].join(", ")), this,
        [&]() { SaveSession(); });
    m_actionSessionSaveAs = sessionMenu->addAction(
        tr("Save As\t%1").arg(m_config.keybinds["session_save_as"].join(", ")),
        this, [&]() { SaveAsSession(); });
    m_actionSessionLoad = sessionMenu->addAction(
        tr("Load\t%1").arg(m_config.keybinds["session_load"].join(", ")), this,
        [&]() { LoadSession(); });

    m_actionSessionSaveAs->setEnabled(false);

    m_actionCloseFile = fileMenu->addAction(
        tr("Close File\t%1").arg(m_config.keybinds["file_close"].join(", ")),
        this, [this]() { Tab_close(); });

    fileMenu->addSeparator();
    QAction *actionQuit
        = fileMenu->addAction(tr("Quit"), this, &QMainWindow::close);

    QMenu *editMenu = m_menuBar->addMenu(tr("&Edit"));
    m_actionUndo    = editMenu->addAction(
        tr("Undo\t%1").arg(m_config.keybinds["undo"].join(", ")), this,
        &Lektra::Undo);
    m_actionRedo = editMenu->addAction(
        tr("Redo\t%1").arg(m_config.keybinds["redo"].join(", ")), this,
        &Lektra::Redo);
    m_actionUndo->setEnabled(false);
    m_actionRedo->setEnabled(false);
    editMenu->addAction(
        tr("Last Pages\t%1")
            .arg(m_config.keybinds["edit_last_pages"].join(", ")),
        this, &Lektra::editLastPages);

    // --- View Menu ---
    m_viewMenu         = m_menuBar->addMenu(tr("&View"));
    m_actionFullscreen = m_viewMenu->addAction(
        tr("Fullscreen\t%1").arg(m_config.keybinds["fullscreen"].join(", ")),
        this, &Lektra::ToggleFullscreen);
    m_actionFullscreen->setCheckable(true);
    m_actionFullscreen->setChecked(m_config.window.fullscreen);

    m_actionZoomIn = m_viewMenu->addAction(
        tr("Zoom In\t%1").arg(m_config.keybinds["zoom_in"].join(", ")), this,
        &Lektra::ZoomIn);
    m_actionZoomOut = m_viewMenu->addAction(
        tr("Zoom Out\t%1").arg(m_config.keybinds["zoom_out"].join(", ")), this,
        &Lektra::ZoomOut);

    m_viewMenu->addSeparator();

    m_fitMenu = m_viewMenu->addMenu(tr("Fit"));

    m_actionFitWidth = m_fitMenu->addAction(
        tr("Width\t%1").arg(m_config.keybinds["fit_width"].join(", ")), this,
        &Lektra::Fit_width);

    m_actionFitHeight = m_fitMenu->addAction(
        tr("Height\t%1").arg(m_config.keybinds["fit_height"].join(", ")), this,
        &Lektra::Fit_height);

    m_actionFitWindow = m_fitMenu->addAction(
        tr("Page\t%1").arg(m_config.keybinds["fit_page"].join(", ")), this,
        &Lektra::Fit_page);

    m_fitMenu->addSeparator();

    // Auto Resize toggle — a modifier of the current fit, so it belongs
    // inside the Fit submenu next to Width / Height / Page.
    m_actionAutoresize = m_fitMenu->addAction(
        tr("Auto Fit\t%1").arg(m_config.keybinds["fit_auto"].join(", ")), this,
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
        tr("Single\t%1").arg(m_config.keybinds["layout_single"].join(", ")),
        this, [&]() { SetLayoutMode(DocumentView::LayoutMode::SINGLE); });

    m_actionLayoutLeftToRight = m_layoutMenu->addAction(
        tr("Horizontal\t%1")
            .arg(m_config.keybinds["layout_horizontal"].join(", ")),
        this, [&]() { SetLayoutMode(DocumentView::LayoutMode::HORIZONTAL); });

    m_actionLayoutTopToBottom = m_layoutMenu->addAction(
        tr("Vertical\t%1").arg(m_config.keybinds["layout_vertical"].join(", ")),
        this, [&]() { SetLayoutMode(DocumentView::LayoutMode::VERTICAL); });

    m_actionLayoutBook = m_layoutMenu->addAction(
        tr("Book\t%1").arg(m_config.keybinds["layout_book"].join(", ")), this,
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

    m_actionCommandPicker = m_toggleMenu->addAction(
        tr("Command Picker\t%1")
            .arg(m_config.keybinds["command_picker"].join(", ")),
        this, &Lektra::Show_command_picker);

    m_actionBookmarkPicker = m_toggleMenu->addAction(
        tr("Bookmark Picker\t%1")
            .arg(m_config.keybinds["bookmark_picker"].join(", ")),
        this, &Lektra::Show_bookmark_picker);

    m_actionToggleOutline = m_toggleMenu->addAction(
        tr("Outline\t%1").arg(m_config.keybinds["picker_outline"].join(", ")),
        this, &Lektra::ShowOutline);

    QAction *actionGenerateOutline = m_toggleMenu->addAction(
        tr("Generate Outline\t%1")
            .arg(m_config.keybinds["generate_outline"].join(", ")),
        this, &Lektra::GenerateOutline);

    QAction *actionExportOutline = m_toggleMenu->addAction(
        tr("Export Outline\t%1")
            .arg(m_config.keybinds["export_outline"].join(", ")),
        this, &Lektra::ExportOutline);

    QAction *actionLoadOutline = m_toggleMenu->addAction(
        tr("Load Outline from File\t%1")
            .arg(m_config.keybinds["load_outline"].join(", ")),
        this, &Lektra::LoadOutline);

    m_actionToggleHighlightAnnotSearch = m_toggleMenu->addAction(
        tr("Highlight Annotation Search\t%1")
            .arg(m_config.keybinds["picker_highlight_search"].join(", ")),
        this, &Lektra::Show_highlight_search);

    m_actionToggleMenubar = m_toggleMenu->addAction(
        tr("Menubar\t%1").arg(m_config.keybinds["menubar"].join(", ")), this,
        &Lektra::ToggleMenubar);
    m_actionToggleMenubar->setCheckable(true);
    m_actionToggleMenubar->setChecked(!m_menuBar->isHidden());

    m_actionToggleTabBar = m_toggleMenu->addAction(
        tr("Tabs\t%1").arg(m_config.keybinds["tabs"].join(", ")), this,
        &Lektra::ToggleTabBar);
    m_actionToggleTabBar->setCheckable(true);
    m_actionToggleTabBar->setChecked(!m_tab_widget->tabBar()->isHidden());

    m_actionToggleStatusbar = m_toggleMenu->addAction(
        tr("Statusbar\t%1").arg(m_config.keybinds["statusbar"].join(", ")),
        this, &Lektra::ToggleStatusbar);
    m_actionToggleStatusbar->setCheckable(true);
    m_actionToggleStatusbar->setChecked(!m_statusbar->isHidden());

    m_viewMenu->addSeparator();

    QAction *actionNarrowToRegion = m_viewMenu->addAction(
        tr("Narrow to Region\t%1")
            .arg(m_config.keybinds["narrow_to_region"].join(", ")),
        this, &Lektra::NarrowToRegion);

    QAction *actionWidenRegion = m_viewMenu->addAction(
        tr("Widen\t%1").arg(m_config.keybinds["widen_region"].join(", ")), this,
        &Lektra::WidenRegion);

    m_viewMenu->addSeparator();

    m_actionInvertColor = m_viewMenu->addAction(
        tr("Invert Color\t%1")
            .arg(m_config.keybinds["invert_color"].join(", ")),
        this, &Lektra::InvertColor);
    m_actionInvertColor->setCheckable(true);
    m_actionInvertColor->setChecked(m_config.behavior.invert_mode);

    m_actionHighContrast = m_viewMenu->addAction(
        tr("High Contrast\t%1")
            .arg(m_config.keybinds["high_contrast"].join(", ")),
        this, &Lektra::ToggleHighContrast);
    m_actionHighContrast->setCheckable(true);
    m_actionHighContrast->setChecked(m_config.behavior.high_contrast);

    // --- Tools Menu ---

    QMenu *toolsMenu = m_menuBar->addMenu(tr("Tools"));

    m_modeMenu = toolsMenu->addMenu(tr("Mode"));

    QActionGroup *modeActionGroup = new QActionGroup(this);
    modeActionGroup->setExclusive(true);

    m_actionRegionSelect = m_modeMenu->addAction(
        tr("Region Selection\t%1")
            .arg(m_config.keybinds["selection_mode_region"].join(", ")),
        this, &Lektra::ToggleRegionSelect);
    m_actionRegionSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionRegionSelect);

    m_actionTextSelect = m_modeMenu->addAction(
        tr("Text Selection\t%1")
            .arg(m_config.keybinds["selection_mode_text"].join(", ")),
        this, &Lektra::ToggleTextSelection);
    m_actionTextSelect->setCheckable(true);
    modeActionGroup->addAction(m_actionTextSelect);

    m_actionTextHighlight = m_modeMenu->addAction(
        tr("Text Highlight\t%1")
            .arg(m_config.keybinds["annot_highlight_mode"].join(", ")),
        this, &Lektra::ToggleTextHighlight);
    m_actionTextHighlight->setCheckable(true);
    modeActionGroup->addAction(m_actionTextHighlight);

    m_actionAnnotRect = m_modeMenu->addAction(
        tr("Annotate Rectangle\t%1")
            .arg(m_config.keybinds["annot_rect_mode"].join(", ")),
        this, &Lektra::ToggleAnnotRect);
    m_actionAnnotRect->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotRect);

    m_actionAnnotEdit = m_modeMenu->addAction(
        tr("Edit Annotations\t%1")
            .arg(m_config.keybinds["annot_edit_mode"].join(", ")),
        this, &Lektra::ToggleAnnotSelect);
    m_actionAnnotEdit->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotEdit);

    m_actionAnnotPopup = m_modeMenu->addAction(
        tr("Annotate Popup\t%1")
            .arg(m_config.keybinds["annot_popup_mode"].join(", ")),
        this, &Lektra::ToggleAnnotPopup);
    m_actionAnnotPopup->setCheckable(true);
    modeActionGroup->addAction(m_actionAnnotPopup);

    // TODO: Store visual line mode state in config
    m_actionVisualLineMode = m_modeMenu->addAction(
        tr("Visual Line Mode\t%1")
            .arg(m_config.keybinds["visual_line_mode"].join(", ")),
        this, &Lektra::ToggleVisualLineMode);
    m_actionVisualLineMode->setCheckable(true);
    modeActionGroup->addAction(m_actionVisualLineMode);

    m_actionNoneMode = m_modeMenu->addAction(
        tr("None\t%1").arg(m_config.keybinds["none_mode"].join(", ")), this,
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
        tr("Encrypt Document\t%1")
            .arg(m_config.keybinds["file_encrypt"].join(", ")),
        this, &Lektra::EncryptDocument);
    m_actionEncrypt->setEnabled(false);

    m_actionDecrypt = toolsMenu->addAction(
        tr("Decrypt Document\t%1")
            .arg(m_config.keybinds["file_decrypt"].join(", ")),
        this, &Lektra::DecryptDocument);
    m_actionDecrypt->setEnabled(false);

    // --- Navigation Menu ---
    m_navMenu = m_menuBar->addMenu(tr("&Navigation"));

    QAction *actionStartPage = m_navMenu->addAction(
        tr("StartPage\t%1")
            .arg(m_config.keybinds["show_startup_widget"].join(", ")),
        this, &Lektra::showStartupWidget);

    m_actionGotoPage = m_navMenu->addAction(
        tr("Goto Page\t%1").arg(m_config.keybinds["page_goto"].join(", ")),
        this, [this]() { Lektra::Goto_page(); });

    m_actionFirstPage = m_navMenu->addAction(
        tr("First Page\t%1").arg(m_config.keybinds["page_first"].join(", ")),
        this, &Lektra::FirstPage);

    m_actionPrevPage = m_navMenu->addAction(
        tr("Previous Page\t%1").arg(m_config.keybinds["page_prev"].join(", ")),
        this, &Lektra::PrevPage);

    m_actionNextPage = m_navMenu->addAction(
        tr("Next Page\t%1").arg(m_config.keybinds["page_next"].join(", ")),
        this, &Lektra::NextPage);
    m_actionLastPage = m_navMenu->addAction(
        tr("Last Page\t%1").arg(m_config.keybinds["page_last"].join(", ")),
        this, &Lektra::LastPage);

    m_actionPrevLocation = m_navMenu->addAction(
        tr("Previous Location\t%1")
            .arg(m_config.keybinds["location_prev"].join(", ")),
        this, &Lektra::GoBackHistory);
    m_actionNextLocation = m_navMenu->addAction(
        tr("Next Location\t%1")
            .arg(m_config.keybinds["location_next"].join(", ")),
        this, &Lektra::GoForwardHistory);

    QMenu *markMenu = m_navMenu->addMenu(tr("Marks"));

    m_actionSetMark = markMenu->addAction(
        tr("Set Mark\t%1").arg(m_config.keybinds["set_mark"].join(", ")), this,
        [this]() { Lektra::SetMark(); });

    m_actionGotoMark = markMenu->addAction(
        tr("Goto Mark\t%1").arg(m_config.keybinds["goto_mark"].join(", ")),
        this, [this]() { Lektra::GotoMark(); });

    m_actionDeleteMark = markMenu->addAction(
        tr("Delete Mark\t%1").arg(m_config.keybinds["delete_mark"].join(", ")),
        this, [this]() { Lektra::DeleteMark(); });

    /* Help Menu */
    QMenu *helpMenu = m_menuBar->addMenu(tr("&Help"));
    m_actionAbout   = helpMenu->addAction(
        tr("About\t%1").arg(m_config.keybinds["show_about"].join(", ")), this,
        &Lektra::ShowAbout);

    helpMenu->addAction(tr("Check for Updates"), this,
                        [this] { checkForUpdates(true); });
    helpMenu->addAction(tr("What's New"), this, &Lektra::showWhatsNew);

    m_actionShowTutorialFile = helpMenu->addAction(
        tr("Open Tutorial File\t%1")
            .arg(m_config.keybinds["show_tutorial_file"].join(", ")),
        this, &Lektra::showTutorialFile);

    helpMenu->addSeparator();
    m_actionDonate = helpMenu->addAction(tr("Donate / Support"), this,
                                         &Lektra::ShowDonate);

    // Icons are always assigned to actions below — the visibility is
    // controlled globally by Qt::AA_DontShowIconsInMenus. That way toggling
    // window.show_menu_icons at runtime (via `lektra.opt.window
    // .show_menu_icons`, say) takes effect immediately without needing to
    // re-run initMenubar.
    QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus,
                                   !m_config.window.show_menu_icons);

    // --- Standard-style icons on menu actions ---
    // Grouped in one block so a future theme change or icon reassignment
    // does not require touching every action's registration site above.
    // Icons always come from QStyle::SP_* — the same on every platform
    // (Windows/macOS/Linux, themed or not). Freedesktop `fromTheme()` names
    // were tried here previously but only resolve on Linux with a matching
    // icon theme installed; everywhere else (Windows, macOS, theme-less
    // Linux) they silently fall through anyway, so using SP_* directly
    // gives consistent, predictable icons across platforms instead of
    // "nicer on some Linux setups, generic everywhere else".
    auto ic = [this](QStyle::StandardPixmap p)
    {
        return style()->standardIcon(p);
    };
    auto th = [&ic](const char * /*themeName*/, QStyle::StandardPixmap fb)
    {
        return ic(fb);
    };

    // File
    actionOpenFile->setIcon(th("document-open", QStyle::SP_DialogOpenButton));
    actionOpenVSplit->setIcon(th("document-open", QStyle::SP_DialogOpenButton));
    actionOpenHSplit->setIcon(th("document-open", QStyle::SP_DialogOpenButton));
    m_actionFileProperties->setIcon(
        th("document-properties", QStyle::SP_FileDialogInfoView));
    m_actionOpenContainingFolder->setIcon(
        th("folder-open", QStyle::SP_DirOpenIcon));
    m_recentFilesMenu->setIcon(
        th("document-open-recent", QStyle::SP_FileDialogDetailedView));
    m_actionSaveFile->setIcon(th("document-save", QStyle::SP_DialogSaveButton));
    m_actionSaveAsFile->setIcon(
        th("document-save-as", QStyle::SP_DialogSaveButton));
    sessionMenu->setIcon(
        th("preferences-system-session", QStyle::SP_ComputerIcon));
    m_actionSessionSave->setIcon(th("document-save", QStyle::SP_DriveHDIcon));
    m_actionSessionSaveAs->setIcon(
        th("document-save-as", QStyle::SP_DriveHDIcon));
    m_actionSessionLoad->setIcon(th("document-open", QStyle::SP_DirLinkIcon));
    m_actionCloseFile->setIcon(
        th("window-close", QStyle::SP_DialogCloseButton));
    actionQuit->setIcon(th("application-exit", QStyle::SP_TitleBarCloseButton));

    // Edit
    m_actionUndo->setIcon(th("edit-undo", QStyle::SP_ArrowBack));
    m_actionRedo->setIcon(th("edit-redo", QStyle::SP_ArrowForward));

    // View
    m_actionFullscreen->setIcon(
        th("view-fullscreen", QStyle::SP_TitleBarMaxButton));
    m_actionZoomIn->setIcon(th("zoom-in", QStyle::SP_ArrowUp));
    m_actionZoomOut->setIcon(th("zoom-out", QStyle::SP_ArrowDown));
    m_actionFitWidth->setIcon(
        th("zoom-fit-width", QStyle::SP_DialogApplyButton));
    m_actionFitHeight->setIcon(
        th("zoom-fit-height", QStyle::SP_DialogApplyButton));
    m_actionFitWindow->setIcon(
        th("zoom-fit-best", QStyle::SP_DialogApplyButton));
    m_actionAutoresize->setIcon(th("view-restore", QStyle::SP_BrowserReload));
    // Narrow to Region is a "crop the visible area" action, not a zoom —
    // use the Freedesktop crop icon when the theme has one, and the shade
    // (collapse-to-titlebar) button as the SP_* fallback since it also
    // reads as "reduce visible area".
    actionNarrowToRegion->setIcon(
        th("image-crop", QStyle::SP_TitleBarShadeButton));
    actionWidenRegion->setIcon(
        th("view-restore", QStyle::SP_TitleBarUnshadeButton));
    m_actionInvertColor->setIcon(
        th("preferences-color", QStyle::SP_TitleBarShadeButton));

    // Toggle / outline
    // Submenu labels get an icon too (via QMenu::setIcon), so the Show/Hide
    // submenu picks up a visibility glyph.
    m_toggleMenu->setIcon(th("view-reveal", QStyle::SP_FileDialogListView));
    m_actionToggleMenubar->setIcon(
        th("open-menu-symbolic", QStyle::SP_TitleBarUnshadeButton));
    m_actionCommandPicker->setIcon(ic(QStyle::SP_FileDialogListView));
    m_actionBookmarkPicker->setIcon(
        th("bookmarks", QStyle::SP_FileDialogListView));
    m_actionToggleOutline->setIcon(
        th("view-list-tree", QStyle::SP_FileDialogDetailedView));
    actionGenerateOutline->setIcon(
        th("view-refresh", QStyle::SP_FileDialogNewFolder));
    actionExportOutline->setIcon(
        th("document-save", QStyle::SP_DialogSaveButton));
    actionLoadOutline->setIcon(
        th("document-open", QStyle::SP_DialogOpenButton));
    m_actionToggleHighlightAnnotSearch->setIcon(
        th("edit-find", QStyle::SP_FileDialogListView));

    // Tools > Mode — themed edit/select icons; SP_* fallbacks are weak
    // but not misleading.
    m_actionRegionSelect->setIcon(
        th("edit-select-all", QStyle::SP_FileDialogContentsView));
    m_actionTextSelect->setIcon(
        th("edit-select", QStyle::SP_FileDialogListView));
    m_actionTextHighlight->setIcon(
        th("format-text-underline", QStyle::SP_FileDialogListView));
    m_actionAnnotRect->setIcon(
        th("draw-rectangle", QStyle::SP_FileDialogContentsView));
    m_actionAnnotEdit->setIcon(
        th("document-edit", QStyle::SP_FileDialogListView));
    m_actionAnnotPopup->setIcon(
        th("insert-text", QStyle::SP_MessageBoxInformation));
    m_actionNoneMode->setIcon(th("edit-clear", QStyle::SP_DialogCancelButton));

    // Tools
    m_actionEncrypt->setIcon(
        th("document-encrypt", QStyle::SP_DialogSaveButton));
    m_actionDecrypt->setIcon(
        th("document-decrypt", QStyle::SP_DialogOpenButton));

    // Navigation — Freedesktop `go-*` icons render as directional arrows
    // in every mainstream theme; SP_Media* keep a play-transport metaphor
    // as fallback for non-themed environments.
    actionStartPage->setIcon(th("go-home", QStyle::SP_DirHomeIcon));
    m_actionGotoPage->setIcon(th("go-jump", QStyle::SP_ArrowRight));
    m_actionFirstPage->setIcon(th("go-first", QStyle::SP_MediaSkipBackward));
    m_actionPrevPage->setIcon(th("go-previous", QStyle::SP_MediaSeekBackward));
    m_actionNextPage->setIcon(th("go-next", QStyle::SP_MediaSeekForward));
    m_actionLastPage->setIcon(th("go-last", QStyle::SP_MediaSkipForward));
    // Location history reads as undo/redo for navigation — use the edit-*
    // themed icons (curved back/forward arrows in most themes) so it's
    // visually distinct from page nav.
    m_actionPrevLocation->setIcon(th("edit-undo", QStyle::SP_ArrowBack));
    m_actionNextLocation->setIcon(th("edit-redo", QStyle::SP_ArrowForward));

    // Marks
    m_actionSetMark->setIcon(th("bookmark-new", QStyle::SP_DialogOkButton));
    m_actionGotoMark->setIcon(th("go-jump", QStyle::SP_ArrowRight));
    m_actionDeleteMark->setIcon(th("edit-delete", QStyle::SP_TrashIcon));

    // Help
    m_actionAbout->setIcon(th("help-about", QStyle::SP_MessageBoxInformation));
    m_actionShowTutorialFile->setIcon(
        th("help-contents", QStyle::SP_MessageBoxQuestion));
    m_actionDonate->setIcon(
        th("emblem-favorite", QStyle::SP_MessageBoxInformation));
}

// Updates the UI elements checking if valid
// file is open or not (and if it's PDF or not, for PDF-specific actions)
void
Lektra::updateUiEnabledState() noexcept
{
    const bool hasFile = (m_doc != nullptr);

    // Initialize defaults for when no file is open
    Model::FileType filetype = Model::FileType::NONE;
    bool isPDF               = false;
    bool hasTextLayer        = false;
    bool isImageDoc          = false;

    // Only query the model if the document exists
    if (hasFile)
    {
        auto *model = m_doc->model();
        filetype    = model->fileType();
        isPDF       = (filetype == Model::FileType::PDF);
        isImageDoc  = model->isImage();

        hasTextLayer = (filetype == Model::FileType::PDF
                        || filetype == Model::FileType::EPUB
                        || filetype == Model::FileType::FB2
                        || filetype == Model::FileType::MOBI
                        || filetype == Model::FileType::XPS);
    }

    // --- Visibility Logic ---
    // Rule: Most advanced tools are hidden if it's an image or no file is
    // open
    const bool showAdvancedTools = hasFile && !isImageDoc;

    auto setAdvancedVisible = [&](auto *widget)
    {
        if (widget)
            widget->setVisible(showAdvancedTools);
    };

    setAdvancedVisible(m_layoutMenu ? m_layoutMenu->menuAction() : nullptr);
    setAdvancedVisible(m_modeMenu ? m_modeMenu->menuAction() : nullptr);
    setAdvancedVisible(m_navMenu ? m_navMenu->menuAction() : nullptr);
    setAdvancedVisible(m_actionToggleOutline);
    setAdvancedVisible(m_actionToggleHighlightAnnotSearch);
    setAdvancedVisible(m_actionGotoPage);
    setAdvancedVisible(m_actionFitWidth);
    setAdvancedVisible(m_actionFitHeight);
    setAdvancedVisible(m_actionFitWindow);
    setAdvancedVisible(m_actionAutoresize);
    setAdvancedVisible(m_actionTextSelect);
    setAdvancedVisible(m_actionRegionSelect);
    setAdvancedVisible(m_actionTextHighlight);
    setAdvancedVisible(m_actionAnnotRect);
    setAdvancedVisible(m_actionAnnotEdit);
    setAdvancedVisible(m_actionAnnotPopup);
    setAdvancedVisible(m_actionVisualLineMode);
    setAdvancedVisible(m_actionNoneMode);
    setAdvancedVisible(m_actionGotoMark);
    setAdvancedVisible(m_actionSetMark);
    setAdvancedVisible(m_actionDeleteMark);

    // PDF-specific visibility
    if (m_actionEncrypt)
        m_actionEncrypt->setVisible(isPDF);
    if (m_actionDecrypt)
        m_actionDecrypt->setVisible(isPDF);

    // --- Enabled State Logic ---

    // Global actions
    m_actionOpenContainingFolder->setEnabled(hasFile);
    m_actionZoomIn->setEnabled(hasFile);
    m_actionZoomOut->setEnabled(hasFile);
    m_actionCloseFile->setEnabled(hasFile);
    m_fitMenu->setEnabled(hasFile);
    m_actionInvertColor->setEnabled(hasFile);
    m_actionInvertColor->setChecked(hasFile && m_doc->invertColor());
    m_actionHighContrast->setChecked(
        m_doc ? m_doc->config().behavior.high_contrast
              : m_config.behavior.high_contrast);
    m_actionSessionSave->setEnabled(hasFile);
    updateHistoryNavigationActions();
    m_actionSessionSaveAs->setEnabled(!m_session_name.isEmpty());

    // Navigation & Advanced (Disabled for Images)
    m_actionGotoPage->setEnabled(showAdvancedTools);
    m_actionFirstPage->setEnabled(showAdvancedTools);
    m_actionPrevPage->setEnabled(showAdvancedTools);
    m_actionNextPage->setEnabled(showAdvancedTools);
    m_actionLastPage->setEnabled(showAdvancedTools);
    // Mode menu: enabled for all file types (region select works for images
    // too); individual actions inside are guarded by their own capability
    // checks.
    if (m_modeMenu)
        m_modeMenu->setEnabled(hasFile);
    if (m_layoutMenu)
        m_layoutMenu->setEnabled(showAdvancedTools);
    if (m_navMenu)
        m_navMenu->setEnabled(showAdvancedTools);
    m_actionToggleOutline->setEnabled(showAdvancedTools);
    m_actionToggleHighlightAnnotSearch->setEnabled(showAdvancedTools);
    m_actionVisualLineMode->setEnabled(showAdvancedTools);
    m_actionRegionSelect->setEnabled(hasFile);
    m_actionSetMark->setEnabled(showAdvancedTools);
    m_actionGotoMark->setEnabled(showAdvancedTools);
    m_actionDeleteMark->setEnabled(showAdvancedTools);

    // Text Selection
    m_actionTextSelect->setEnabled(hasTextLayer && !isImageDoc);

    // PDF-only
    m_actionSaveFile->setEnabled(isPDF && m_doc->isModified());
    m_actionSaveAsFile->setEnabled(isPDF);
    m_actionEncrypt->setEnabled(isPDF);
    m_actionDecrypt->setEnabled(isPDF);
    m_actionAnnotRect->setEnabled(isPDF);
    m_actionAnnotEdit->setEnabled(isPDF);
    m_actionAnnotPopup->setEnabled(isPDF);
    m_actionTextHighlight->setEnabled(isPDF);
    m_actionFileProperties->setEnabled(hasFile);

    // Undo/Redo reset
    if (!hasFile)
    {
        m_actionUndo->setEnabled(false);
        m_actionRedo->setEnabled(false);
    }

    // --- Status Bar and Mode Handling ---
    if (hasFile && isImageDoc)
    {
        m_statusbar->setModeVisible(true);
        m_statusbar->setProgressVisible(false);
        m_statusbar->setPageInfoVisible(true);
        updateSelectionModeActions();
    }
    else
    {
        m_statusbar->setModeVisible(true);
        m_statusbar->setProgressVisible(true);
        if (hasFile)
        {
            updateSelectionModeActions();
        }
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
    AboutDialog *abw = new AboutDialog(this);
    abw->setAttribute(Qt::WA_DeleteOnClose);
    abw->open();
}

void
Lektra::ShowDonate() noexcept
{
    DonateDialog *dlg = new DonateDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

void
Lektra::populateBookmarks() noexcept
{
    QFile bookmarksFile(m_bookmarks_file_path);
    if (!bookmarksFile.exists())
        return;

    // Read the bookmarks json file
    if (!bookmarksFile.open(QIODevice::ReadOnly))
    {
        qWarning() << tr("Failed to open bookmarks file:")
                   << m_bookmarks_file_path;
        return;
    }

    QByteArray data = bookmarksFile.readAll();
    bookmarksFile.close();

    std::vector<Bookmark> bookmarks;

    // Parse the json data
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        qWarning() << tr("Failed to parse bookmarks file:")
                   << parseError.errorString();
        return;
    }

    if (!jsonDoc.isArray())
    {
        qWarning() << tr("Invalid bookmarks file format: expected an array");
        return;
    }

    QJsonArray jsonArray = jsonDoc.array();
    for (const QJsonValue &value : jsonArray)
    {
        if (!value.isObject())
            continue;

        QJsonObject obj = value.toObject();

        QString id       = obj["id"].toString();
        QString label    = obj["label"].toString();
        QString filePath = obj["file_path"].toString();
        PageLocation loc = PageLocation::fromJson(obj["location"].toArray());
        QDateTime created
            = QDateTime::fromString(obj["added_on"].toString(), Qt::ISODate);

        bookmarks.emplace_back(filePath, loc, created, id);
    }

    m_bookmark_manager.setBookmarks(bookmarks);
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

    // QFileIconProvider asks the platform for the icon associated with a
    // file's type — native shell association on Windows, Finder's icon on
    // macOS, the desktop's mime-type icon (application-pdf, image/vnd.djvu,
    // …) on Linux. Reused across entries since construction queries
    // platform icon-theme state.
    static QFileIconProvider iconProvider;

    m_recentFilesMenu->clear();
    for (const RecentFileEntry &entry : m_recent_files_store.entries())
    {
        if (entry.file_path.isEmpty())
            continue;
        const QString path  = entry.file_path;
        const int page      = entry.page_number;
        QAction *fileAction = new QAction(path, m_recentFilesMenu);

        QIcon typeIcon = iconProvider.icon(QFileInfo(path));
        fileAction->setIcon(typeIcon.isNull()
                                ? style()->standardIcon(QStyle::SP_FileIcon)
                                : typeIcon);

        connect(fileAction, &QAction::triggered, this, [this, path, page]()
        { OpenFileInNewTab(path, [this, page](void *) { gotoPage(page); }); });

        m_recentFilesMenu->addAction(fileAction);
    }

    if (m_recentFilesMenu->isEmpty())
        m_recentFilesMenu->setDisabled(true);
    else
        m_recentFilesMenu->setEnabled(true);
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

void
Lektra::updateHistoryNavigationActions() noexcept
{
    m_actionPrevLocation->setEnabled(m_doc && m_doc->canGoBack());
    m_actionNextLocation->setEnabled(m_doc && m_doc->canGoForward());
}

// Update selection mode actions (QAction) in QMenu based on current
// selection mode
void
Lektra::updateSelectionModeActions() noexcept
{
    if (!m_doc)
        return;

    const bool isImageDoc = m_doc->model()->isImage();

    if (isImageDoc)
    {
        m_actionNoneMode->setChecked(true);
        return;
    }

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
    // No-op if already in the requested state — otherwise a second enter
    // would overwrite the saved state with the (now-hidden) values and
    // exiting would leave everything hidden.
    if (m_focus_mode == enable)
        return;

    if (enable)
    {
        // Snapshot the real runtime state, not the config baseline, so
        // exiting restores exactly what the user had before entering
        // (including any manual bar toggles they made this session).
        m_focus_saved.menubar_visible   = !m_menuBar->isHidden();
        m_focus_saved.statusbar_visible = !m_statusbar->isHidden();
        m_focus_saved.tabbar_visible    = m_tab_widget->tabBar()->isVisible();

        m_menuBar->setVisible(false);
        m_statusbar->setVisible(false);
        m_tab_widget->tabBar()->setVisible(false);
    }
    else
    {
        m_menuBar->setVisible(m_focus_saved.menubar_visible);
        m_statusbar->setVisible(m_focus_saved.statusbar_visible);
        m_tab_widget->tabBar()->setVisible(m_focus_saved.tabbar_visible);
    }

    m_focus_mode = enable;
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
Lektra::TogglePresentationMode() noexcept
{
    if (!m_doc)
        return;

    if (!m_presentation.active)
    {
        // Save every piece of state we are about to override so we can
        // restore exactly what the user had. If they were already in
        // fullscreen / had a bar hidden, exiting should leave that alone.
        m_presentation.was_fullscreen    = isFullScreen();
        m_presentation.menubar_visible   = !m_menuBar->isHidden();
        m_presentation.statusbar_visible = !m_statusbar->isHidden();
        m_presentation.tabbar_visible    = m_tab_widget->tabBar()->isVisible();
        m_presentation.layout_mode       = m_doc->layoutMode();
        m_presentation.fit_mode          = m_doc->fitMode();

        // Enter: chrome-less fullscreen, single-page layout, fit-to-window.
        // Scrollbars are left to the fit-to-window mode — a page fully
        // fitted to the viewport won't need them, and the config-level
        // scrollbar policy is left untouched so exiting restores it too.
        if (!m_presentation.was_fullscreen)
            showFullScreen();
        m_menuBar->hide();
        m_statusbar->hide();
        m_tab_widget->tabBar()->hide();
        SetLayoutMode(DocumentView::LayoutMode::SINGLE);
        m_doc->setFitMode(DocumentView::FitMode::Window);
        setLaserPointerCursor(true);

        m_presentation.active = true;
    }
    else
    {
        // Restore the pre-presentation state.
        if (!m_presentation.was_fullscreen)
            showNormal();
        m_menuBar->setVisible(m_presentation.menubar_visible);
        m_statusbar->setVisible(m_presentation.statusbar_visible);
        m_tab_widget->tabBar()->setVisible(m_presentation.tabbar_visible);
        SetLayoutMode(m_presentation.layout_mode);
        if (m_presentation.fit_mode != DocumentView::FitMode::COUNT)
            m_doc->setFitMode(m_presentation.fit_mode);

        m_presentation.active = false;
        setLaserPointerCursor(false);
    }
}
