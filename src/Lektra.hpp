#pragma once

#include "CommandPicker.hpp"
#include "Config.hpp"
#include "DocumentView.hpp"
#include "FloatingOverlayWidget.hpp"
#include "HighlightSearchPicker.hpp"
#include "MarkManager.hpp"
#include "MessageBar.hpp"
#include "TabBar.hpp"
// #include "OutlineWidget.hpp"
#include "OutlinePicker.hpp"
#include "PropertiesWidget.hpp"
#include "RecentFilesPicker.hpp"
#include "RecentFilesStore.hpp"
#include "SearchBar.hpp"
#include "StartupWidget.hpp"
#include "Statusbar.hpp"
#include "TabWidget.hpp"
#include "argparse.hpp"
#ifdef ENABLE_LLM_SUPPORT
#include "LLM/LLMWidget.hpp"
#endif
#include "CommandManager.hpp"

#include "toml.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenuBar>
#include <QShortcut>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>

class Lektra : public QMainWindow
{
    Q_OBJECT

public:
    Lektra() noexcept;
    Lektra(const QString &sessionName,
           const QJsonArray &sessionArray) noexcept; // load from session

    DocumentContainer *VSplit() noexcept;
    DocumentContainer *HSplit() noexcept;

    void Create_or_focus_portal() noexcept;
    void Close_split() noexcept;
    void Close_other_splits() noexcept;
    void Focus_split_up() noexcept;
    void Focus_split_down() noexcept;
    void Focus_split_left() noexcept;
    void Focus_split_right() noexcept;
    void Read_args_parser(argparse::ArgumentParser &argparser) noexcept;
    // bool OpenFile(DocumentView *view) noexcept;
    void Search() noexcept;
    void Search_regex() noexcept;
    void Search_in_page() noexcept;
    void Show_highlight_search() noexcept;
    void Show_command_picker() noexcept;

    void Toggle_visual_line_mode() noexcept;
    void Toggle_presentation_mode() noexcept;
    void ToggleAutoResize() noexcept;
    void ToggleFocusMode() noexcept;
    void ToggleMenubar() noexcept;
    void ToggleTabBar() noexcept;
    void TogglePanel() noexcept;
    void ToggleFullscreen() noexcept;
    void ToggleTextSelection() noexcept;
    void ToggleTextHighlight() noexcept;
    void ToggleRegionSelect() noexcept;
    void ToggleAnnotRect() noexcept;
    void ToggleAnnotSelect() noexcept;
    void ToggleAnnotPopup() noexcept;

    void FileProperties() noexcept;
    void SaveFile() noexcept;
    void SaveAsFile() noexcept;
    void CloseFile() noexcept;
    void Fit_width() noexcept;
    void Fit_height() noexcept;
    void Fit_page() noexcept;
    void Show_outline() noexcept;
    void InvertColor() noexcept;
    void TextSelectionMode() noexcept;
    void GoBackHistory() noexcept;
    void GoForwardHistory() noexcept;
    void LastPage() noexcept;
    void NextPage() noexcept;
    void OpenContainingFolder() noexcept;
    bool OpenFileDWIM(const QString &filename = QString()) noexcept;
    bool OpenFileInContainer(DocumentContainer *container,
                             const QString &filename               = QString(),
                             const std::function<void()> &callback = {},
                             DocumentView *targetView = nullptr) noexcept;
    void OpenFilesInVSplit(const std::vector<std::string> &files) noexcept;
    void OpenFilesInHSplit(const std::vector<std::string> &files) noexcept;

    void OpenFiles(const std::vector<std::string> &filenames) noexcept;
    void OpenFilesInNewTab(const std::vector<std::string> &files) noexcept;
    void OpenFilesInNewTab(const QList<QString> &files) noexcept;
    DocumentView *OpenFileInNewTab(const QString &filename = QString(),
                                   const std::function<void()> &callback
                                   = {}) noexcept;
    bool OpenFileInNewWindow(const QString &filename = QString(),
                             const std::function<void()> &callback
                             = {}) noexcept;
    void OpenFilesInNewWindow(const QStringList &filenames) noexcept;
    DocumentView *OpenFileVSplit(const QString &filename = QString(),
                                 const std::function<void()> &callback = {});
    DocumentView *OpenFileHSplit(const QString &filename = QString(),
                                 const std::function<void()> &callback = {});
    void PrevPage() noexcept;
    void FirstPage() noexcept;
    void Selection_copy() noexcept;
    void Selection_cancel() noexcept;
    void VisitLinkKB() noexcept;
    void CopyLinkKB() noexcept;
    void RotateClock() noexcept;
    void RotateAnticlock() noexcept;
    void ScrollDown() noexcept;
    void ScrollUp() noexcept;
    void ScrollLeft() noexcept;
    void ScrollRight() noexcept;
    void NextHit() noexcept;
    void GotoHit(int index) noexcept;
    void PrevHit() noexcept;
    void ZoomReset() noexcept;
    void ZoomIn() noexcept;
    void ZoomOut() noexcept;
    void Zoom_set() noexcept;
    void Goto_page() noexcept;
    void GotoLocation(int pageno, float x, float y) noexcept;
    void GotoLocation(const DocumentView::PageLocation &loc) noexcept;
    void LoadSession(QString name = QString()) noexcept;
    void SaveSession() noexcept;
    void SaveAsSession(const QString &name = QString()) noexcept;
    void Tab_goto(int index = -1) noexcept;
    void Tab_last() noexcept;
    void Tab_first() noexcept;
    void Tab_close(int tabno = -1) noexcept;
    void Tab_next() noexcept;
    void Tab_prev() noexcept;
    void TabMoveRight() noexcept;
    void TabMoveLeft() noexcept;
    void ReselectLastTextSelection() noexcept;
    void SetLayoutMode(DocumentView::LayoutMode mode) noexcept;
    void SetMark() noexcept;
    void GotoMark() noexcept;
    void DeleteMark() noexcept;
    void EncryptDocument() noexcept;
    void DecryptDocument() noexcept;
    void Undo() noexcept;
    void Redo() noexcept;
    void ShowAbout() noexcept;
    void TextHighlightCurrentSelection() noexcept;
    void TabsCloseLeft() noexcept;
    void TabsCloseRight() noexcept;
    void TabsCloseOthers() noexcept;
    void Reshow_jump_marker() noexcept;
    void Show_recent_files_picker() noexcept;
    void Copy_page_image() noexcept;
    void Reopen_last_closed_file() noexcept;

#ifdef ENABLE_LLM_SUPPORT
    void ToggleLLMWidget() noexcept;
#endif

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void dropEvent(QDropEvent *event) noexcept override;

private:
    enum class LinkHintMode
    {
        None = 0,
        Visit,
        Copy
    };

    inline bool validTabIndex(int index) const noexcept
    {
        return m_tab_widget && index >= 0 && index < m_tab_widget->count();
    }

    inline void ToggleSearchBar() noexcept
    {
        m_search_bar->setVisible(!m_search_bar->isVisible());
        if (m_search_bar->isVisible())
            m_search_bar->focusSearchInput();
    }

    void restoreSplitNode(DocumentContainer *container,
                          DocumentView *targetView, const QJsonObject &node,
                          std::function<void()> onAllDone) noexcept;
    void focusSplitHelper(DocumentContainer::Direction direction) noexcept;

    DocumentView *
    openFileSplitHelper(const QString &filename               = {},
                        const std::function<void()> &callback = {},
                        Qt::Orientation orientation           = Qt::Horizontal);

    void setCurrentDocumentView(DocumentView *view) noexcept;
    void centerMouseInDocumentView(DocumentView *view) noexcept;
    DocumentView *findOpenView(const QString &path) const noexcept;
    void construct() noexcept;
    void SetDPR() noexcept;
    void initDB() noexcept;
    void initMenubar() noexcept;
    void initGui() noexcept;
    void initConfig() noexcept;
    void initDefaultKeybinds() noexcept;
    void warnShortcutConflicts() noexcept;
    void setupKeybinding(const QString &action, const QString &key) noexcept;
    void populateRecentFiles() noexcept;
    void updateUiEnabledState() noexcept;
    void editLastPages() noexcept;
    void openLastVisitedFile() noexcept;
    void initConnections() noexcept;
    void initTabConnections(DocumentView *) noexcept;
    void initCommands() noexcept;
    void trimRecentFilesDatabase() noexcept;
    void reloadDocument() noexcept;
    void handleTabDataRequested(int index, TabBar::TabData *outData) noexcept;
    void handleTabDropReceived(const TabBar::TabData &data) noexcept;
    void handleTabDetached(int index, const QPoint &globalPos) noexcept;
    void handleTabDetachedToNewWindow(int index,
                                      const TabBar::TabData &data) noexcept;
    void handleCtrlLinkClickRequested(DocumentView *view,
                                      const BrowseLinkItem *linkItem) noexcept;

    void gotoPage(int pageno) noexcept;
    void setFocusMode(bool state) noexcept;
    void search(const QString &term = {}) noexcept;
    void searchInPage(const int pageno, const QString &term = {}) noexcept;
    void writeSessionToFile() noexcept;

    // private helpers
#ifndef NDEBUG
    void debug_command() noexcept;
#endif

    DocumentView *get_view_by_id(const DocumentView::Id) const noexcept;
    void handleFileNameChanged(const QString &name) noexcept;
    void handleCurrentTabChanged(int index) noexcept;
    void openInExplorerForIndex(int index) noexcept;
    void filePropertiesForIndex(int index) noexcept;
    void updateMenuActions() noexcept;
    void updatePanel() noexcept;
    QStringList getSessionFiles() noexcept;
    void insertFileToDB(const QString &fname, int pageno) noexcept;
    void clearKBHintsOverlay() noexcept;
    void handleFSFileChanged(const QString &filePath) noexcept;
    void onFileReloadTimer() noexcept;
    void showStartupWidget() noexcept;
    void updateActionsAndStuffForSystemTabs() noexcept;
    void updatePageNavigationActions() noexcept;
    void updateSelectionModeActions() noexcept;
    void updateTabbarVisibility() noexcept;
    void setSessionName(const QString &name) noexcept;
    void openSessionFromArray(const QJsonArray &sessionArray) noexcept;
    void modeColorChangeRequested(const GraphicsView::Mode mode) noexcept;
    void handleEscapeKeyPressed() noexcept;
    void showTutorialFile() noexcept;
    void setMark(const QString &key, const int pageno,
                 const DocumentView::PageLocation location) noexcept;
    void gotoMark(const QString &key) noexcept;
    void deleteMark(const QString &key) noexcept;
    bool handleLinkHintEvent(QEvent *event) noexcept;
    bool handleTabContextMenu(QObject *object, QEvent *event) noexcept;
    QDir m_config_dir, m_session_dir;
    Statusbar *m_statusbar{nullptr};
    QMenuBar *m_menuBar{nullptr};
    QMenu *m_fitMenu{nullptr};
    QMenu *m_recentFilesMenu{nullptr};
    QMenu *m_navMenu{nullptr};
    QMenu *m_toggleMenu{nullptr};
    QMenu *m_viewMenu{nullptr};
    QMenu *m_layoutMenu{nullptr};
    QAction *m_actionCommandPicker{nullptr};
    QAction *m_actionShowTutorialFile{nullptr};
    QAction *m_actionLayoutSingle{nullptr};
    QAction *m_actionLayoutLeftToRight{nullptr};
    QAction *m_actionLayoutTopToBottom{nullptr};
    QAction *m_actionLayoutBook{nullptr};
    QAction *m_actionEncrypt{nullptr};
    QAction *m_actionDecrypt{nullptr};
    QMenu *m_modeMenu{nullptr};
    QAction *m_actionUndo{nullptr};
    QAction *m_actionRedo{nullptr};
    QAction *m_actionToggleTabBar{nullptr};
    QAction *m_actionFullscreen{nullptr};
    QAction *m_actionZoomIn{nullptr};
    QAction *m_actionInvertColor{nullptr};
    QAction *m_actionFileProperties{nullptr};
    QAction *m_actionOpenContainingFolder{nullptr};
    QAction *m_actionSaveFile{nullptr};
    QAction *m_actionSaveAsFile{nullptr};
    QAction *m_actionCloseFile{nullptr};
    QAction *m_actionZoomOut{nullptr};
    QAction *m_actionFitWidth{nullptr};
    QAction *m_actionFitHeight{nullptr};
    QAction *m_actionFitWindow{nullptr};
    QAction *m_actionAutoresize{nullptr};
    QAction *m_actionToggleMenubar{nullptr};
    QAction *m_actionToggleVisualLineMode{nullptr};
    QAction *m_actionTogglePanel{nullptr};
    QAction *m_actionToggleOutline{nullptr};
    QAction *m_actionToggleHighlightAnnotSearch{nullptr};
    QAction *m_actionGotoPage{nullptr};
    QAction *m_actionFirstPage{nullptr};
    QAction *m_actionPrevPage{nullptr};
    QAction *m_actionNextPage{nullptr};
    QAction *m_actionLastPage{nullptr};
    QAction *m_actionPrevLocation{nullptr};
    QAction *m_actionNextLocation{nullptr};
    QAction *m_actionAbout{nullptr};
    QAction *m_actionTextHighlight{nullptr};
    QAction *m_actionAnnotRect{nullptr};
    QAction *m_actionAnnotPopup{nullptr};
    QAction *m_actionTextSelect{nullptr};
    QAction *m_actionRegionSelect{nullptr};
    QAction *m_actionAnnotEdit{nullptr};
    QAction *m_actionSessionLoad{nullptr};
    QAction *m_actionSessionSave{nullptr};
    QAction *m_actionSessionSaveAs{nullptr};
    QAction *m_actionSetMark{nullptr};
    QAction *m_actionGotoMark{nullptr};
    QAction *m_actionDeleteMark{nullptr};
#ifdef ENABLE_LLM_SUPPORT
    QAction *m_actionToggleLLMWidget{nullptr};
#endif
    QTabWidget *m_side_panel_tabs{nullptr};
    Config m_config;
    float m_dpr{1.0f};
    QMap<QString, float> m_screen_dpr_map; // DPR per screen
    QString m_config_file_path;
    QString m_lockedInputBuffer; // Used for link hints and waiting input event
                                 // like for marks etc.
    bool m_link_hint_mode{false}, m_focus_mode{false},
        m_load_default_keybinding{true};
    bool m_batch_opening{false};
    StartupWidget *m_startup_widget{nullptr};
    LinkHintMode m_link_hint_current_mode{LinkHintMode::None};
    QMap<int, Model::LinkInfo> m_link_hint_map;
    DocumentView *m_doc{nullptr};
    TabWidget *m_tab_widget{nullptr};
    QVBoxLayout *m_layout{nullptr};
    QClipboard *m_clipboard{QGuiApplication::clipboard()};
    RecentFilesStore m_recent_files_store;
    QString m_recent_files_path;
    QString m_session_name;
    MessageBar *m_message_bar{nullptr};
    SearchBar *m_search_bar{nullptr};
    DocumentView *create_portal(DocumentView *sourceView,
                                const QString &filePath) noexcept;

    std::vector<Picker *> m_pickers;
    CommandPicker *m_command_picker{nullptr}; // Technically a picker
    Picker::Keybindings m_picker_keybinds{};
    OutlinePicker *m_outline_picker{nullptr};
    HighlightSearchPicker *m_highlight_search_picker{nullptr};
    RecentFilesPicker *m_recent_file_picker{nullptr};
    MarkManager *m_marks_manager{nullptr};

    // Used for lifetime management of portal-source
    struct PortalPair
    {
        DocumentView *source;
        DocumentView *portal;
    };

    CommandManager m_command_manager;

#ifdef ENABLE_LLM_SUPPORT
    // LLM Support
    LLMWidget *m_llm_widget{nullptr};
#endif
};
