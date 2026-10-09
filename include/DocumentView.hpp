#pragma once

#include "AboutDialog.hpp"
#include "ColorDialog.hpp"
#include "GraphicsImageItem.hpp"
#include "GraphicsScene.hpp"
#include "GraphicsView.hpp"
#include "JumpMarker.hpp"
#include "LinkHint.hpp"
#include "LinkHoverPreview.hpp"
#include "Model.hpp"
#include "PageLocation.hpp"
#include "ScrollBar.hpp"
#include "WaitingSpinnerWidget.hpp"

#ifdef WITH_SYNCTEX
extern "C"
{
    #include "synctex_parser.h"
    #include "synctex_parser_utils.h"
    #include "synctex_version.h"
}
#endif

#include "DispatchType.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QGraphicsItem>
#include <QHash>
#include <QQueue>
#include <QScrollBar>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <memory>
#include <qevent.h>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <vector>

#ifdef WITH_LUA
    #include "LuaCallback.hpp"
#endif

// Z-values for various overlay items
static constexpr int ZVALUE_PAGE            = 0;
static constexpr int ZVALUE_ANNOTATION      = 5;
static constexpr int ZVALUE_LINK            = 10;
static constexpr int ZVALUE_JUMP_MARKER     = 15;
static constexpr int ZVALUE_SEARCH_HITS     = 20;
static constexpr int ZVALUE_KB_LINK_OVERLAY = 25;
static constexpr int ZVALUE_TEXT_SELECTION  = 30;

// Zoom factor limits
static constexpr double MIN_ZOOM_FACTOR = 0.01;
static constexpr double MAX_ZOOM_FACTOR = 100.0;

// Bounds/step/default for ReflowFontSizeIncrease()/Decrease(), in points
// (MuPDF's layout_em). 11pt is FZ_DEFAULT_LAYOUT_EM.
static constexpr float kReflowFontSizeMin     = 6.0f;
static constexpr float kReflowFontSizeMax     = 36.0f;
static constexpr float kReflowFontSizeStep    = 1.0f;
static constexpr float kReflowFontSizeDefault = 11.0f;

struct Config;
class DocumentContainer;
class QMenu;

class DocumentView : public QWidget
{
    Q_OBJECT
public:
    using CallbackFn          = std::function<void(DocumentView *)>;
    using LektraCallbackFn    = std::function<void(void *)>;
    using Id                  = int;
    using SelectedAnnotations = std::vector<std::pair<int, Annotation *>>;

    // `config` is the global config (kept by reference). The view's local
    // options start as a copy of `inheritFrom` if given (e.g. the view a
    // split was made from, like vim's :split), else of `config`.
    DocumentView(const Config &config, const float dpr = 1.0f,
                 QWidget *parent = nullptr, bool thumbnailMode = false,
                 const Config *inheritFrom = nullptr) noexcept;

    DocumentView(const DocumentView &)            = delete;
    DocumentView &operator=(const DocumentView &) = delete;
    DocumentView(DocumentView &&)                 = delete;
    DocumentView &operator=(DocumentView &&)      = delete;
    DocumentView(DocumentView *sourceView)        = delete;

    ~DocumentView() noexcept;

    inline Id id() const noexcept
    {
        return m_id;
    }

    enum class LayoutMode
    {
        SINGLE = 0,
        HORIZONTAL,
        VERTICAL,
        BOOK,
        COUNT
    };

    enum class FitMode
    {
        Width = 0,
        Height,
        Window,
        // "Smart" fit modes ignore blank page margins: the tight content
        // bounding box (returned by Model::contentBBox for PDFs) is fit to
        // the viewport instead of the raw page rectangle, and the view is
        // centred on the content region so the margins get pushed off-screen.
        WidthSmart,
        HeightSmart,
        COUNT
    };

    // This view's local options: a copy of the global config taken when the
    // view is created, so changing it only affects this view. Model,
    // GraphicsView and the annotation items read from it too.
    inline const Config &config() const noexcept
    {
        return m_config;
    }

    inline Config &localConfig() noexcept
    {
        return m_config;
    }

    inline const Config &globalConfig() const noexcept
    {
        return m_global;
    }

    // Applies a change to this view's local options. `section` is the
    // top-level config section that changed (e.g. "page", "scrollbars");
    // several changes in one event-loop tick are applied together.
    void localConfigChanged(const QString &section) noexcept;

    void cancelAllRenders() noexcept;

    // The options a new view of this file type would start with: the global
    // ones, with the overrides of [filetype.<type>] on top.
    Config freshLocalConfig() const noexcept;

    inline void setSpacing(int spacing) noexcept
    {
        m_spacing = spacing;
    }

    inline int spacing() const noexcept
    {
        return m_spacing;
    }

    inline Model *model() const noexcept
    {
        return m_model;
    }

    inline GraphicsView *graphicsView() const noexcept
    {
        return m_gview;
    }

    inline GraphicsScene *graphicsScene() const noexcept
    {
        return m_gscene;
    }

    inline float rotation() const noexcept
    {
        return m_model->rotation();
    }
    inline FitMode fitMode() const noexcept
    {
        return m_fit_mode;
    }

    inline GraphicsView::Mode selectionMode() const noexcept
    {
        return m_gview->mode();
    }

    inline int pageNo() const noexcept
    {
        return m_pageno;
    }

    inline int numPages() const noexcept
    {
        return m_model->numPages();
    }

    inline Model::FileType fileType() const noexcept
    {
        return m_model->m_filetype;
    }

    inline QString fileName() const noexcept
    {
        return QFileInfo(m_model->filePath()).fileName();
    }

    inline const QString &filePath() const noexcept
    {
        return m_model->filePath();
    }

    inline float dpr() const noexcept
    {
        return m_model->DPR();
    }

    inline bool invertColor() const noexcept
    {
        return m_model->invertColor();
    }

    inline bool fileOpenedSuccessfully() const noexcept
    {
        return m_model->success();
    }

    inline bool autoReload() const noexcept
    {
        return m_auto_reload;
    }

    inline void setAutoResize(bool state) noexcept
    {
        m_auto_resize = state;
    }

    inline bool autoResize() const noexcept
    {
        return m_auto_resize;
    }

    inline void Undo() noexcept
    {
        if (m_model && m_model->undoStack()->canUndo())
            m_model->undoStack()->undo();
    }

    inline void Redo() noexcept
    {
        if (m_model && m_model->undoStack()->canRedo())
            m_model->undoStack()->redo();
    }

    inline double zoom() noexcept
    {
        return m_current_zoom;
    }

    inline bool isModified() const noexcept
    {
        return m_is_modified;
    }

    inline bool canGoBack() const noexcept
    {
        return m_loc_history_index > 0;
    }

    inline bool canGoForward() const noexcept
    {
        return m_loc_history_index >= 0
               && m_loc_history_index + 1 < (int)m_loc_history.size();
    }

    inline LayoutMode layoutMode() const noexcept
    {
        return m_layout_mode;
    }

    inline void setContainer(DocumentContainer *container) noexcept
    {
        m_container = container;
    }

    inline DocumentContainer *container() const noexcept
    {
        return m_container;
    }

    // Moves this split out of its tab, at the page it shows: into a window of
    // its own or into a tab of its own. False if there was nothing to move
    // (it is the only split of the tab, or it has no file) or it failed.
    bool detachToWindow() noexcept;
    bool detachToTab() noexcept;

    inline void set_source(DocumentView *source) noexcept
    {
        m_source_view = source;
    }

    inline DocumentView *source() const noexcept
    {
        return m_source_view;
    }

    inline void clear_source() noexcept
    {
        m_source_view = nullptr;
    }

    inline bool is_portal() const noexcept
    {
        return m_source_view != nullptr;
    }

    // When set, this view is never recorded in the recent-files history
    // (checked alongside is_portal() everywhere a tab close/window close
    // would otherwise call insertFileToDB()).
    inline void setNoHistory(bool state) noexcept
    {
        m_no_history = state;
    }

    inline bool noHistory() const noexcept
    {
        return m_no_history;
    }

    inline DocumentView *portal() const noexcept
    {
        return m_portal_view;
    }

    inline void setActive(bool state) noexcept
    {
        m_gview->setActive(state);
        m_gview->update();
    }

    inline bool isActive() const noexcept
    {
        return m_gview->isActive();
    }

    inline void setSplitMaximized(bool state) noexcept
    {
        m_gview->setSplitMaximized(state);
        m_gview->update();
    }

    inline bool isSplitMaximized() const noexcept
    {
        return m_gview->isSplitMaximized();
    }

    inline bool visual_line_mode() const noexcept
    {
        return m_visual_line_mode;
    }

    inline bool caretMode() const noexcept
    {
        return m_caret_mode;
    }

    inline bool isThumbnailView() const noexcept
    {
        return m_thumbnail_mode;
    }

    inline void reloadFile() noexcept
    {
        tryReloadLater(0);
    }

    inline bool hasTextSelection() const noexcept
    {
        return (!m_selection_start.isNull() && m_selection_start_page >= 0
                && m_selection_end_page >= 0);
    }

    inline QString extractText(bool formatted) const noexcept
    {
        return pageText(m_pageno, formatted);
    }

    // The text of any page (0-based), "" if it has none.
    inline QString pageText(int pageno, bool formatted) const noexcept
    {
        return QString::fromStdString(
            m_model->getTextInPage(pageno, formatted));
    }

#ifdef WITH_LUA
    inline void addEventListener(DispatchType type, int handle, bool is_once,
                                 CallbackFn callback) noexcept
    {
        m_lua_event_dispatcher[type].push_back(
            {.ref = handle, .invoker = callback, .is_once = is_once});
    }

    inline void clearEventListeners(DispatchType type) noexcept
    {
        m_lua_event_dispatcher[type].clear();
    }

    enum class ContextMenuType
    {
        TextSelection = 0,
        RegionSelection
    };

    using MenuCallbackFn = std::function<void(DocumentView *, QMenu *)>;

    struct MenuCallback
    {
        int ref;
        MenuCallbackFn invoker;
        bool is_once = false;
    };

    inline void addContextMenuListener(ContextMenuType type, int handle,
                                       bool is_once,
                                       MenuCallbackFn callback) noexcept
    {
        m_lua_context_menu_dispatcher[type].push_back(
            {.ref = handle, .invoker = callback, .is_once = is_once});
    }

    inline void clearContextMenuListeners(ContextMenuType type) noexcept
    {
        m_lua_context_menu_dispatcher[type].clear();
    }

    void removeEventListener(DispatchType type, int handle) noexcept;
    void removeContextMenuListener(ContextMenuType type, int handle) noexcept;
#endif

#ifdef WITH_SYNCTEX
    void synctexLocateInDocument(const char *fileName, int line) noexcept;
    void synctexForwardSearch(const QString &texPath, int line,
                              int col) noexcept;
#endif

    void startGifPlayback() noexcept;
    void stopGifPlayback() noexcept;

    void setAutoReload(bool state) noexcept;
    void setDPR(float dpr) noexcept;
    QString selectionText(bool formatted             = false,
                          std::string page_separator = "\n") const noexcept;

    // The jump locations of this view (what GoBackHistory and GoForwardHistory
    // walk through), and the index of the current one (-1: none).
    inline const std::vector<PageLocation> &locationHistory() const noexcept
    {
        return m_loc_history;
    }
    inline int locationHistoryIndex() const noexcept
    {
        return m_loc_history_index;
    }
    // Between a point of the canvas (scene) and a point of a page, in page
    // points from the top left of the page. The second one needs the page to
    // be shown, that is among the pages that are loaded.
    bool scenePosToPage(QPointF scenePos, int &pageno,
                        QPointF &pagePoint) const noexcept;
    bool pagePosToScene(int pageno, QPointF pagePoint,
                        QPointF &scenePos) const noexcept;

    bool pageAtScenePos(QPointF scenePos, int &outPageIndex,
                        GraphicsImageItem *&outPageItem) const noexcept;
    void setPortal(DocumentView *portal) noexcept;
    void clearPortal() noexcept;
    void set_visual_line_mode(bool state) noexcept;

    // Caret mode (accessibility feature, cf. Firefox/Okular "caret
    // browsing"): a keyboard-driven, character-granularity text cursor.
    // Movement/select-extend are separate commands (mirroring
    // scroll_left/right/up/down) so they get independent keybindings.
    void ToggleCaretMode() noexcept;
    void caretMoveLeft() noexcept;
    void caretMoveRight() noexcept;
    void caretMoveUp() noexcept;
    void caretMoveDown() noexcept;
    void caretMoveLineStart() noexcept;
    void caretMoveLineEnd() noexcept;
    void caretSelectLeft() noexcept;
    void caretSelectRight() noexcept;
    void caretSelectUp() noexcept;
    void caretSelectDown() noexcept;
    void FollowLink(const Model::LinkInfo &info) noexcept;
    void setInvertColor(bool invert) noexcept;
    void openAsync(const QString &filePath) noexcept;
    // Page to jump to once the file's first layout and render are done (see
    // handleOpenFileFinished()). GotoPage() can't be used earlier: on large
    // documents the page offsets it depends on are not cached yet.
    inline void setPendingPage(int pageno) noexcept
    {
        m_pending_page = pageno;
    }
    bool EncryptDocument() noexcept;
    bool DecryptDocument() noexcept;
    void ReselectLastTextSelection() noexcept;
    void positionPageItem(GraphicsImageItem *pageItem, int pageno) noexcept;
    void createAndAddPageItem(int pageno, QImage image, QSize fullSize = {},
                              QRect region = {}) noexcept;
    void renderImage() noexcept;
    void renderPages() noexcept;
    // Scroll-driven refresh: like renderPages(), but leaves alone visible
    // pages that are already rendered with identical render settings.
    void refreshVisiblePages() noexcept;
    void renderPagesImpl(bool skipCurrent) noexcept;
    void renderPage() noexcept;
    void handleTextHighlightRequested() noexcept;
    void handleTextCommentRequested() noexcept;
    void setFitMode(FitMode mode) noexcept;
    void GotoPage(int pageno) noexcept;
    void GotoLocation(const PageLocation &targetlocation) noexcept;
    void CenterOnLocation(const PageLocation &targetlocation) noexcept;
    void GotoPageWithHistory(int pageno) noexcept;
    void GotoLocationWithHistory(const PageLocation &targetlocation) noexcept;
    void GotoNextPage() noexcept;
    void GotoPrevPage() noexcept;
    void GotoFirstPage() noexcept;
    void GotoLastPage() noexcept;
    void setZoom(double factor, bool restoreLocation = true) noexcept;
    void setZoomAnchored(double factor, QPointF anchorScenePos) noexcept;
    enum class SearchScope
    {
        All,
        Below,
        Above,
    };
    void Search(const QString &term, bool useRegex) noexcept;
    void SearchInPage(int pageno, const QString &term) noexcept;
    void SearchCancel() noexcept;
    inline void setSearchScope(SearchScope scope) noexcept
    {
        m_search_scope = scope;
    }
    void ZoomIn() noexcept;
    void ZoomOut() noexcept;
    void ZoomReset() noexcept;

    // Text-size commands for reflowable documents (EPUB/FB2/MOBI): drive
    // MuPDF's layout_em, which actually re-paginates the document (more
    // text per page = fewer pages), unlike ZoomIn/ZoomOut which stay pure
    // raster scaling. No-op when Model::supports_reflow() is false.
    void ReflowFontSizeIncrease() noexcept;
    void ReflowFontSizeDecrease() noexcept;
    void ReflowFontSizeReset() noexcept;
    void NextHit() noexcept;
    void PrevHit() noexcept;
    void GotoHit(int index) noexcept;
    void ScrollLeft() noexcept;
    void ScrollRight() noexcept;
    void ScrollUp() noexcept;
    void ScrollDown() noexcept;
    void ScrollDown_HalfPage() noexcept;
    // Scrolls by (dx, dy) pixels, or to an absolute scroll position.
    void ScrollBy(int dx, int dy) noexcept;
    void ScrollTo(int x, int y) noexcept;
    [[nodiscard]] QPoint scrollPosition() const noexcept;
    [[nodiscard]] QPoint scrollMaximum() const noexcept;
    // Pages (0-based) currently on screen, in order.
    [[nodiscard]] std::vector<int> VisiblePages() noexcept;
    // Selects the text from `from` to `to` (page-space points), reading
    // order. Both pages must be rendered (visible or preloaded); returns
    // false otherwise, or if the format has no selectable text.
    bool SelectTextRange(const PageLocation &from,
                         const PageLocation &to) noexcept;
    void ScrollUp_HalfPage() noexcept;
    void RotateClock() noexcept;
    void RotateAnticlock() noexcept;
    void FlipH() noexcept;
    void FlipV() noexcept;
    void startRegionSelect(std::function<void(QRectF)> cb) noexcept;
    // Crops the already-rendered page pixmap to the given scene-space region
    // and returns it as a standalone QImage (empty if the region doesn't map
    // onto a rendered page). Shared by CopyRegionAsImage() and the Lua
    // region_select_image() API.
    QImage regionImage(QRectF area) noexcept;
    // The current page rendered as an image (about 150 dpi), for attaching to
    // a chat message. Null if it cannot be rendered.
    QImage currentPageImage() noexcept;
    // Writes pages (0-based, in the given order) to files.
    //  * Pictures (png, jpg, webp, bmp, tif, ...): one file per page, rendered
    //    at `dpi`. One name is a pattern (%d or %03d is the page number, or
    //    "-<number>" is put before the extension); several names are one for
    //    each page. The format is the extension, png when there is none.
    //  * pdf, text (txt), html, xhtml, cbz, docx, odt: one file with all the
    //    pages (one name). svg: one file per page.
    // With `split`, the formats that make one file for all the pages make one
    // file per page instead, named like pictures. Images and DjVu can only
    // be written as pictures or pdf. Nothing is
    // written unless everything is valid and, without `overwrite`, no file
    // exists: then `existing` (if given) is set. `written` gets the files.
    bool exportPages(const QStringList &names, const std::vector<int> &pages,
                     int dpi = 150, bool overwrite = true,
                     QStringList *written = nullptr, QString *error = nullptr,
                     bool *existing = nullptr, bool split = false) noexcept;
    // Decoded embedded image (native resolution) at a scene position, or a
    // null QImage if there isn't one there. Used to drag an image out of the
    // document (GraphicsView::setImageDragProvider()).
    QImage imageAt(QPointF scenePos) noexcept;
    QMap<int, Model::LinkInfo> LinkKB() noexcept;
    void ClearTextSelection() noexcept;
    void YankSelection(bool formatted = true) noexcept;
    void FileProperties() noexcept;
    void SaveFile() noexcept;
    void SaveAsFile() noexcept;
    void CloseFile() noexcept;
    void ToggleCommentMarkers() noexcept;
    void ToggleThumbnailPanel() noexcept;
    [[nodiscard]] GraphicsImageItem *pageItemAt(int pageno) const noexcept;
    void ToggleAutoResize() noexcept;
    // Crops every rendered page to its tight content bounding box (see
    // Model::contentBBox), hiding blank margins entirely rather than just
    // fitting zoom to the content region like the *Smart fit modes do.
    void ToggleTrimMargins() noexcept;
    inline bool isTrimMargins() const noexcept
    {
        return m_trim_margins;
    }
    void ToggleTextHighlight() noexcept;
    void ToggleRegionSelect() noexcept;
    void ToggleAnnotRect() noexcept;
    void ToggleAnnotSelect() noexcept;
    void ToggleAnnotPopup() noexcept;
    void ToggleTextSelection() noexcept;
    void NarrowToRegion() noexcept;
    void NarrowToPages(int startPage1, int endPage1) noexcept;
    void NarrowToSectionByTitle(const QString &title) noexcept;
    void WidenRegion() noexcept;
    // Zooms so `sceneRect` fills the viewport as closely as possible and
    // centers on it — unlike NarrowToRegion(), the rest of the page/document
    // stays reachable by scrolling, nothing is cropped out of view.
    void ZoomToRegion(QRectF sceneRect) noexcept;
    // Starts region selection; once the user drags out a rect, zooms to it
    // via ZoomToRegion().
    void ZoomToSelection() noexcept;
    inline bool isNarrowed() const noexcept
    {
        return m_is_narrow;
    }
    void GoBackHistory() noexcept;
    void GoForwardHistory() noexcept;
    void ClearKBHintsOverlay() noexcept;
    void UpdateKBHintsOverlay(const QString &input) noexcept;
    void NextSelectionMode() noexcept;
    void NextFitMode() noexcept;
    void setLayoutMode(const LayoutMode &mode) noexcept;
    void addToHistory(const PageLocation &location) noexcept;
    PageLocation CurrentLocation() noexcept;
    void Reshow_jump_marker() noexcept;
    void Copy_page_image() noexcept;
    void rotateHelper() noexcept;
signals:
    void openFileInNewTabRequested(const QString &filePath,
                                   const LektraCallbackFn &cb);
    void allRendersFinished();
    void linkPreviewRequested(DocumentView *view,
                              const BrowseLinkItem *linkItem);
    void ctrlLinkClickRequested(DocumentView *view,
                                const BrowseLinkItem *linkItem);
    void linkOpenInNewTabRequested(DocumentView *view,
                                   const BrowseLinkItem *linkItem);
    void linkOpenVSplitRequested(DocumentView *view,
                                 const BrowseLinkItem *linkItem);
    void linkOpenHSplitRequested(DocumentView *view,
                                 const BrowseLinkItem *linkItem);
    // Fired for an External link's URI (from a click or from keyboard
    // link-hint following) — the receiver (Lektra) decides whether it's a
    // file:// URI pointing at something openable locally, falling back to
    // QDesktopServices::openUrl otherwise.
    void externalLinkRequested(const QString &uri);
    void requestFocus(DocumentView *view);
    void openFileFailed(DocumentView *doc);
    void openFileFinished(DocumentView *doc, Model::FileType filetype);
    void searchBarSpinnerShow(bool state);
    void pageChanged(int pageno);
    void zoomChanged(double factor);
    void fitModeChanged(FitMode mode);
    void rotationChanged(float angle);
    void selectionModeChanged(GraphicsView::Mode mode);
    void statusbarNameChanged(const QString &name);
    void fileNameChanged(const QString &name);
    void searchCountChanged(int count);
    void searchIndexChanged(int index);
    void searchClearRequested();
    void totalPageCountChanged(int total);
    void clipboardContentChanged(const QString &content);
    void insertToDBRequested(const QString &filepath, int pageno);
    void highlightColorChanged(const QColor &color);
    void autoResizeActionUpdate(bool state);
    void currentPageChanged(int pageno);
    void modifiedChanged(bool modified);
    void narrowModeChanged(bool narrowed);
    void narrowPageRangeChanged(int from1, int to1, int total);
    void historyChanged();
    void closed();

private slots:
    void handle_password_required() noexcept;
    void handle_wrong_password() noexcept;
    void handleLinkCtrlClickRequested(QPointF scenePos) noexcept;
    void handleLinkPreviewRequested(QPointF scenePos) noexcept;
    void handleLinkMiddleClickRequested(QPointF scenePos) noexcept;
    void handleTextSelection(QPointF start, QPointF end) noexcept;
    void handleClickSelection(int clickType, QPointF scenePos) noexcept;
    void handleSearchResults(
        const QMap<int, std::vector<Model::SearchHit>> &results) noexcept;
    void handlePartialSearchResults(
        const QMap<int, std::vector<Model::SearchHit>> &results) noexcept;
    void handleAnnotSelectRequested(QRectF area) noexcept;
    void handleAnnotSelectRequested(QPointF area) noexcept;
    void handleAnnotSelectClearRequested() noexcept;
    void handleRegionSelectRequested(QRectF area) noexcept;
    void handleAnnotRectRequested(QRectF area) noexcept;
    void handleAnnotPopupRequested(QPointF scenePos) noexcept;
    void handleHScrollValueChanged(int value) noexcept;
    void handleVScrollValueChanged(int value) noexcept;
    void handleReloadRequested(int pageno = -1) noexcept;
    void handleReloadPasswordRequired() noexcept;
    void handleDeferredResize() noexcept;
    void handleDocumentRelayouted() noexcept;

#ifdef WITH_SYNCTEX
    void handleSynctexJumpRequested(QPointF scenePos) noexcept;
#endif
    void handleOpenFileFinished() noexcept;
    void handleOpenFileFailed() noexcept;

protected:
    void handleContextMenuRequested(const QPoint &globalPos,
                                    bool *handled) noexcept;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;

private:
    // A page as a picture for exporting (see exportPages()).
    QImage renderPageForExport(int pageno, int dpi) noexcept;
    struct HitRef
    {
        int page;
        int indexInPage;
    };

    enum Direction
    {
        LEFT = 0,
        RIGHT,
        UP,
        DOWN
    };

    // Total scene-axis extent across all pages
    inline double totalPageExtent() const noexcept
    {
        return m_page_offsets.empty() ? 0.0 : m_page_offsets.back();
    }

    double pageXOffset(int pageno, double pageW, double sceneW) const noexcept;
    double pageOffset(int pageno) const noexcept;
    double pageStride(int pageno) const noexcept;
    void CopyTextFromRegion(QRectF area) noexcept;
    void CopyRegionAsImage(QRectF area) noexcept;
    void CopyRegionAsImageAtDPI(QRectF area) noexcept;
    void SaveRegionAsImage(QRectF area) noexcept;
    void OpenRegionInViewer(QRectF area,
                            bool withDefaultViewer = false) noexcept;
    void applyNarrow(QRectF sceneRect) noexcept;
    void refreshNarrowVisuals() noexcept;
    void remapNarrowForRotation(bool clockwise) noexcept;
    QRectF narrowSceneRect() const noexcept;
    void onFileReloadRequested(const QString &path) noexcept;
    void tryReloadLater(int attempt) noexcept;

    void initGui() noexcept;
    void setModified(bool state) noexcept;
    void requestPageRender(int pageno, bool force = false,
                           bool visible = true) noexcept;
    void startNextRenderJob() noexcept;
    void clearLinksForPage(int pageno) noexcept;
    void clearAnnotationsForPage(int pageno) noexcept;
    void clearSearchItemsForPage(int pageno) noexcept;
    void clearVisibleAnnotations() noexcept;
    void clearVisiblePages() noexcept;
    void clearVisibleLinks() noexcept;
    void renderPageFromImage(int pageno, QImage image, QSize fullSize = {},
                             QRect region = {}) noexcept;
    // Tells the render job which part of the page is on screen, so a page that
    // is too big to render whole is only rendered where it is looked at.
    void setRenderClip(Model::RenderJob &job) const noexcept;
    // True when a partially rendered page no longer covers what is visible.
    bool regionNeedsRefresh(int pageno) const noexcept;
    void renderLinks(int pageno, const std::vector<Model::RenderLink> &links,
                     bool append = false) noexcept;
    void renderAnnotations(
        const int pageno,
        const std::vector<Model::RenderAnnotation> &annots) noexcept;
    void buildFlatSearchHitIndex() noexcept;
    int getClosestHitIndex(bool above = false) noexcept;
    // Page numbers, sorted and without duplicates: cheaper to build and search
    // than a std::set for the handful of pages involved on every scroll tick.
    using PageList = std::vector<int>;
    static bool inPageList(const PageList &pages, int pageno) noexcept
    {
        return std::binary_search(pages.begin(), pages.end(), pageno);
    }
    void removeUnusedPageItems(const PageList &visiblePages) noexcept;
    void clearDocumentItems() noexcept;
    void ensureVisiblePagePlaceholders() noexcept;
    void updateCurrentPage() noexcept;
    void updateCurrentHitHighlight() noexcept;
    void scrollToCurrentHit() noexcept;
    void zoomHelper(const PageLocation &loc = {-1, 0, 0}) noexcept;
    void repositionPages();
    // Restores the viewport position after a synchronous zoom relayout: finds
    // where the page-local point (relX, relY) on anchorPage now sits in the
    // scene and centers the view so it lands back at viewportRatio's
    // position in the viewport. Used by setZoomAnchored() for both the
    // SINGLE and multi-page layout branches so zoom-anchoring behaves
    // identically across layout modes.
    void restoreZoomAnchor(int anchorPage, double relX, double relY,
                           const QPointF &viewportRatio) noexcept;
    // The page whose slot along the scrolling direction holds the point, even
    // when the point is in a margin or a gap (-1 if there are no pages yet).
    int nearestPageToScenePos(QPointF scenePos) const noexcept;
    // Where the zoom commands zoom around: the mouse cursor if it is over the
    // view (and zoom.anchor_to_mouse is on), the centre of the view otherwise.
    QPointF zoomCommandAnchor() const noexcept;
    void cachePageStride() noexcept;
    void updateSceneRect() noexcept;
    // For reflowable documents (EPUB/FB2/MOBI), re-paginate to the current
    // viewport size at the given font size (layout_em) — saving the
    // current reading position as a fraction (m_relayout_saved_fraction)
    // so handleDocumentRelayouted() can restore roughly the same place
    // once the new pagination is live. Only called from
    // ReflowFontSizeIncrease()/Decrease(); no-op for non-reflowable
    // formats (Model::supports_reflow() gates it).
    void applyReflow(float em) noexcept;
    void initConnections() noexcept;
    void resetConnections() noexcept;
    // openFileFailed/passwordRequired/wrongPassword — connected once in the
    // constructor, but resetConnections() (called by CloseFile(), which
    // openAsync() runs before every open attempt, including the very first)
    // does a blanket m_model->disconnect(this) that severs them. Re-run
    // right after CloseFile() in openAsync() so a failed/password-protected
    // open still reaches DocumentView instead of silently going nowhere.
    void connectModelFailureSignals() noexcept;
    QGraphicsPathItem *ensureSearchItemForPage(int pageno) noexcept;

    PageList getPreloadPages(const std::set<int> &visiblePages) noexcept;
    const std::set<int> &getVisiblePages() noexcept;
    void invalidateVisiblePagesCache() noexcept;
    void removePageItem(int pageno) noexcept;
    void createAndAddPlaceholderPageItem(int pageno) noexcept;
    void prunePendingRenders(const PageList &visiblePages) noexcept;
    void renderSearchHitsForPage(int pageno) noexcept;
    void renderSearchHitsInScrollbar() noexcept;
    void clearSearchHits() noexcept;
    void filterHitsToNarrow(
        QMap<int, std::vector<Model::SearchHit>> &results) const noexcept;
    QGraphicsPathItem *m_current_search_hit_item{nullptr};
    QSizeF pageSceneSize(int pageno) const noexcept;
    // Full page dims, or (m_trim_margins) the tight content-bbox dims —
    // points, pre-scale/rotation. Drop-in replacement for
    // Model::page_dimension_pts() at every layout site so trimmed pages are
    // actually laid out smaller, not just rendered smaller and stretched
    // back up.
    Model::PageDimension pageDimensionsPts(int pageno) const noexcept;
    // Content-bbox rect mapped into the rendered image's own pixel space,
    // via Model::toPixelSpace() (the same transform the real render used).
    // Empty QRect means "no crop" (full page / bbox unavailable).
    QRect contentCropRectPixels(int pageno) const noexcept;
    std::vector<Annotation *> annotationsInArea(int pageno,
                                                QRectF area) noexcept;
    Annotation *annotationAtPoint(int pageno, QPointF point) noexcept;
    SelectedAnnotations getSelectedAnnotations() noexcept;
    void stopPendingRenders() noexcept;
    int pageAtAxisCoord(double coord) const noexcept;
    void updatePageLabels(int pageno, qreal xPos, qreal yPos, qreal pageW,
                          qreal pageH) noexcept;
    void visual_line_move(Direction direction) noexcept;
    void snapVisualLine(bool centerView = true) noexcept;

    // Caret mode internals. A "caret index" i is the gap before
    // m_caret_chars[i] (i in [0, m_caret_chars.size()]); the synthetic '\n'
    // entries buildTextCacheForPages() inserts at each line break are
    // skipped over automatically by the move helpers so every stop is a
    // real, visually distinct position.
    void ensureCaretCharsLoaded() noexcept;
    bool caretIsValidStop(int index) const noexcept;
    void caretLineRange(int index, int &lineStart, int &lineEnd) const noexcept;
    double caretCharCenterX(int charIndex) const noexcept;
    // Scene-space rect (thin sliver) for the caret at m_caret_index: left
    // edge of m_caret_chars[index] if that's a real character, otherwise the
    // right edge of the previous one (end-of-line / end-of-text). Empty if
    // it can't be resolved (no page item, empty page, etc).
    QRectF caretSceneRect(int pageno, int index) const noexcept;
    // Pure index-stepping, shared by the plain move and select-extend
    // commands: crosses a page boundary (updating m_caret_pageno/
    // m_caret_chars) but does not touch the selection anchor or render
    // anything — callers do that afterwards.
    void caretStepLeft() noexcept;
    void caretStepRight() noexcept;
    void caretMoveVertical(bool up) noexcept;
    void renderCaret() noexcept;
    void hideCaret() noexcept;
    void updateCaretSelection() noexcept;

#ifdef WITH_SYNCTEX
    void initSynctex() noexcept;
#endif

    // Declaration order matters: m_local_config is copied from m_global, and
    // m_config is bound to *m_local_config. Held through a pointer because
    // Config.hpp includes this header (for LayoutMode/FitMode).
    const Config &m_global;
    std::unique_ptr<Config> m_local_config;
    Config &m_config;
    QSet<QString> m_pending_config_sections;
    void applyLocalConfigChanges() noexcept;
    // Applies the [filetype.<type>] overrides of the opened document.
    void applyFiletypeOverrides(const QString &filePath) noexcept;
    void applyReflowStyle(bool atOpen) noexcept;
    QString m_reflow_style_key;
    std::string m_override_type;
    Id m_id               = 0;
    Model *m_model        = nullptr;
    GraphicsView *m_gview = nullptr;
    std::function<void(QRectF)> m_region_select_cb;
    GraphicsScene *m_gscene      = nullptr;
    FitMode m_fit_mode           = FitMode::COUNT;
    int m_pageno                 = -1;
    int m_spacing                = 10;
    double m_current_zoom        = MIN_ZOOM_FACTOR;
    bool m_auto_resize           = false;
    bool m_trim_margins          = false;
    int m_pending_page           = -1;
    bool m_awaiting_first_render = false;

    // Hover preview of internal links (see Config::Links::hover_preview).
    struct HoverRequest
    {
        int page = -1;
        float x  = 0.0f;
        float y  = 0.0f;
        QPoint globalPos;
        quint64 generation = 0;
    };
    LinkHoverPreview *m_hover_preview = nullptr;
    QTimer *m_hover_timer             = nullptr;
    QFutureWatcher<QImage> m_hover_watcher;
    HoverRequest m_hover_pending;  // waiting for the delay / a free worker
    HoverRequest m_hover_inflight; // being rendered
    bool m_has_hover_pending   = false;
    quint64 m_hover_generation = 0;
    void showLinkHoverPreview(const BrowseLinkItem *link,
                              const QPoint &globalPos) noexcept;
    void hideLinkHoverPreview() noexcept;
    void startHoverRender() noexcept;
    void ensureHoverSetup() noexcept;

    bool m_auto_reload                        = false;
    ScrollBar *m_hscroll                      = nullptr;
    ScrollBar *m_vscroll                      = nullptr;
    JumpMarker *m_jump_marker                 = nullptr;
    QTimer *m_scroll_page_update_timer        = nullptr;
    QTimer *m_resize_timer                    = nullptr;
    // Reading position (as a fraction of the page count) saved just before
    // Model::relayoutForViewport() is kicked off, consumed by
    // handleDocumentRelayouted() to restore roughly the same place under
    // the new pagination. See applyReflow().
    double m_relayout_saved_fraction          = 0.0;
    // Current font size (layout_em, points) for reflowable documents; only
    // meaningful once ReflowFontSizeIncrease()/Decrease() has been called
    // at least once. Reset to the default on every new document open.
    float m_reflow_em                         = kReflowFontSizeDefault;
    PageLocation m_pending_jump               = {-1, 0, 0};
    int m_search_index                        = -1;
    SearchScope m_search_scope                = SearchScope::All;
    int m_cached_hit_index                    = -2;
    GraphicsImageItem *m_cached_hit_page_item = nullptr;
    int m_selection_start_page                = -1;
    int m_selection_end_page                  = -1;
    int m_last_selection_page                 = -1;
    QGraphicsPathItem *m_selection_path_item  = nullptr;
    QTimer *m_hq_render_timer                 = nullptr;
    int m_loc_history_index                   = -1;
    bool m_is_modified                        = false;
    LayoutMode m_layout_mode                  = LayoutMode::VERTICAL;
    WaitingSpinnerWidget *m_spinner           = nullptr;
    bool m_visible_pages_dirty                = true;
    bool m_page_layout_stale                  = false;
    bool m_deferred_fit                       = false;
    bool m_scroll_to_hit_pending              = false;
    QFileSystemWatcher *m_file_watcher        = nullptr;
    bool m_reload_pending                     = false;
    qint64 m_last_reload_observed_size        = -1;
    DocumentContainer *m_container            = nullptr;
    // max cross-axis page size, cached by cachePageStride()
    double m_max_page_cross_extent            = 0.0;
    // thumbnail label height, cached by cachePageStride() to avoid recreating
    // QFont/QFontMetricsF on every zoom change
    double m_thumbnail_label_height           = 0.0;
    // Portal
    DocumentView *m_source_view               = nullptr;
    DocumentView *m_portal_view               = nullptr;
    bool m_no_history                         = false;
    // Narrow to region / pages. m_narrow_page is the first page; for a
    // region-narrow (single page) m_narrow_page_end == m_narrow_page and
    // m_narrow_local_normalized holds the local rect fractions. For a
    // pages-narrow, m_narrow_page_end > m_narrow_page and
    // m_narrow_local_normalized is (0,0,1,1) — full page on every page in
    // the range.
    bool m_is_narrow                          = false;
    int m_narrow_page                         = -1;
    int m_narrow_page_end                     = -1;
    QRectF m_narrow_local_normalized;
    QRectF
        m_layout_scene_rect; // full scene rect, unaffected by narrow override
    // Visual Line Mode
    QGraphicsPathItem *m_visual_line_item = nullptr;
    int m_visual_line_index               = -1;
    bool m_visual_line_mode               = false;
    // Caret Mode
    bool m_caret_mode                     = false;
    int m_caret_pageno                    = -1;
    int m_caret_index                     = -1;
    // Sticky horizontal column (page-point space) used by Up/Down, like a
    // text editor: unset (-1) until the first vertical move, then held
    // across moves until a horizontal move/click resets it.
    double m_caret_pref_x                 = -1.0;
    // Set only while extending a selection (Shift+caret move); -1 otherwise.
    int m_caret_anchor_index              = -1;
    int m_caret_anchor_pageno             = -1;
    std::vector<Model::CachedTextChar> m_caret_chars;
    QGraphicsPathItem *m_caret_item  = nullptr;
    QTimer *m_caret_blink_timer      = nullptr;
    bool m_thumbnail_mode            = false;
    int m_thumbnail_highlighted_page = -1;
#ifdef WITH_SYNCTEX
    synctex_scanner_p m_synctex_scanner = nullptr;
#endif

#ifdef WITH_LUA
    std::unordered_map<DispatchType, std::vector<LuaCallback<DocumentView>>>
        m_lua_event_dispatcher;
    void dispatchLuaEvent(DispatchType type) noexcept;
    bool removeLuaEventCallback(DispatchType type, int callbackRef) noexcept;
    std::unordered_map<ContextMenuType, std::vector<MenuCallback>>
        m_lua_context_menu_dispatcher;
    void applyLuaContextMenu(ContextMenuType type, QMenu *menu) noexcept;
#endif

    QHash<int, GraphicsImageItem *> m_page_items_hash;
    QHash<int, std::vector<BrowseLinkItem *>> m_page_links_hash;
    QHash<int, std::vector<Annotation *>> m_page_annotations_hash;
    QSet<int> m_pending_renders;
    QQueue<int> m_visible_render_queue;
    QQueue<int> m_render_queue;
    // Renders currently running on the model's pool, with their cancel token.
    QHash<int, std::shared_ptr<RenderCancel>> m_inflight_renders;
    // Pool slots held by running tasks (cancelled ones included, until they
    // actually return).
    int m_render_slots_used = 0;
    // How fast the view is being scrolled (px per ms, smoothed), to hold back
    // preloading while it is.
    QElapsedTimer m_scroll_clock;
    qint64 m_last_scroll_ms   = -1;
    int m_last_scroll_value   = 0;
    double m_scroll_speed     = 0;
    void noteScrollMotion(int value) noexcept;
    [[nodiscard]] bool isScrollingFast() const noexcept;
    // Drops queued and running renders for pages that are no longer wanted.
    void pruneRendersForScroll() noexcept;
    // LEKTRA_RENDER_TRACE: start of the current burst of render requests (-1
    // when idle) and how many were made in it.
    double m_trace_burst_start = -1;
    int m_trace_requests       = 0;
    void traceRenderResult(int pageno, const char *outcome, double cbStartMs,
                           const Model::PageRenderResult &result) noexcept;
    QSet<int> m_placeholder_pages;

    // Render settings a page's current GraphicsImageItem was rendered with;
    // lets scroll refreshes skip pages whose pixels would come out identical.
    struct PageRenderKey
    {
        double zoom          = 0.0;
        float rotation       = 0.0f;
        float dpr            = 0.0f;
        uint32_t fg          = 0;
        uint32_t bg          = 0;
        bool flip_h          = false;
        bool flip_v          = false;
        bool invert          = false;
        bool trim            = false;
        bool high_contrast   = false;
        bool dont_invert_img = false;

        bool operator==(const PageRenderKey &) const = default;
    };
    PageRenderKey currentPageRenderKey() const noexcept;
    QHash<int, PageRenderKey> m_page_render_keys;
    QSet<int> m_preload_pages;
    QMap<int, std::vector<Model::SearchHit>> m_search_hits;
    std::vector<HitRef> m_search_hit_flat_refs;
    QHash<int, QGraphicsPathItem *> m_search_items;
    // Bumped by clearSearchHits() / SearchCancel(). Snapshotted into
    // m_search_dispatched_gen right before m_model->search(); the result
    // handlers drop batches whose dispatched-gen no longer matches, so
    // late partials from a superseded search cannot repopulate m_search_hits
    // and stale "No matches" popups from cancelled searches never fire.
    qint64 m_search_gen            = 0;
    qint64 m_search_dispatched_gen = 0;
    QPointF m_selection_start, m_selection_end;
    QPointF m_last_selection_start, m_last_selection_end;
    std::vector<PageLocation> m_loc_history;
    QFutureWatcher<void> m_open_future_watcher;
    std::vector<LinkHint *> m_kb_link_hints;
    std::vector<double> m_page_offsets;
    std::set<int> m_visible_pages_cache;
    PageLocation m_old_jump_marker_loc = {-1, 0, 0};
    std::vector<Model::VisualLineInfo> m_visual_lines;
};
