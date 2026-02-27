#pragma once

#include "AboutDialog.hpp"
#include "GraphicsImageItem.hpp"
#include "GraphicsPixmapItem.hpp"
#include "GraphicsScene.hpp"
#include "GraphicsView.hpp"
#include "JumpMarker.hpp"
#include "LinkHint.hpp"
#include "Model.hpp"
#include "ScrollBar.hpp"
#include "WaitingSpinnerWidget.hpp"

#include <QFutureWatcher>

#ifdef HAS_SYNCTEX
extern "C"
{
#include <synctex/synctex_parser.h>
#include <synctex/synctex_parser_utils.h>
#include <synctex/synctex_version.h>
}
#endif

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGraphicsItem>
#include <QHash>
#include <QQueue>
#include <QScrollBar>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <qevent.h>
#include <set>

// Z-values for various overlay items
#define ZVALUE_PAGE 0
#define ZVALUE_ANNOTATION 5
#define ZVALUE_LINK 10
#define ZVALUE_JUMP_MARKER 15
#define ZVALUE_SEARCH_HITS 20
#define ZVALUE_KB_LINK_OVERLAY 25
#define ZVALUE_TEXT_SELECTION 30

// Zoom factor limits
#define MIN_ZOOM_FACTOR 0.01
#define MAX_ZOOM_FACTOR 100.0

#define CSTR(x) x.toStdString().c_str()

struct Config;
class DocumentContainer; // Forward declaration to avoid circular dependency

class DocumentView : public QWidget
{
    Q_OBJECT
public:
    using Id = uint32_t;
    DocumentView(const Config &config, QWidget *parent = nullptr) noexcept;
    ~DocumentView() noexcept;

    inline Id id() const noexcept
    {
        return m_id;
    }

    enum class LayoutMode
    {
        SINGLE = 0,
        LEFT_TO_RIGHT,
        TOP_TO_BOTTOM,
        BOOK,
        COUNT
    };

    enum class FitMode
    {
        Width = 0,
        Height,
        Window,
        COUNT
    };

    struct PageLocation
    {
        int pageno;
        float x, y;
    };

    inline const Config &config() const noexcept
    {
        return m_config;
    }

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

    inline void setDPR(float dpr) noexcept
    {
        m_model->setDPR(dpr);
        renderPages();
    }

    inline QString fileName() const noexcept
    {
        return QFileInfo(m_model->filePath()).fileName();
    }

    inline QString filePath() const noexcept
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

    inline DocumentView *portal() const noexcept
    {
        return m_portal_view;
    }

    inline void set_portal(DocumentView *portal) noexcept
    {
        m_portal_view = portal;
        portal->set_source(this);
        portal->m_gview->setPortal(true);
    }

    inline void clear_portal() noexcept
    {
        if (m_portal_view)
        {
            m_portal_view->clear_source();
            m_portal_view->m_gview->setPortal(false);
            m_portal_view = nullptr;
        }
        // TODO: Maybe notify views that the portal was cleared
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

    inline bool visual_line_mode() const noexcept
    {
        return m_visual_line_mode;
    }

    void set_visual_line_mode(bool state) noexcept;
    void FollowLink(const Model::LinkInfo &info) noexcept;
    void setInvertColor(bool invert) noexcept;
    void openAsync(const QString &filePath) noexcept;
    bool EncryptDocument() noexcept;
    bool DecryptDocument() noexcept;
    void ReselectLastTextSelection() noexcept;
    void createAndAddPageItem(int pageno, const QImage &image) noexcept;

    void renderPages() noexcept;
    void renderPage() noexcept;
    void handleTextHighlightRequested() noexcept;

    void setFitMode(FitMode mode) noexcept;
    void GotoPage(int pageno) noexcept;
    void GotoLocation(const PageLocation &targetlocation) noexcept;
    void GotoPageWithHistory(int pageno) noexcept;
    void GotoLocationWithHistory(const PageLocation &targetlocation) noexcept;
    void GotoNextPage() noexcept;
    void GotoPrevPage() noexcept;
    void GotoFirstPage() noexcept;
    void GotoLastPage() noexcept;
    void setZoom(double factor) noexcept;
    void Search(const QString &term, bool useRegex) noexcept;
    void SearchInPage(int pageno, const QString &term) noexcept;
    void ZoomIn() noexcept;
    void ZoomOut() noexcept;
    void ZoomReset() noexcept;
    void NextHit() noexcept;
    void PrevHit() noexcept;
    void GotoHit(int index) noexcept;
    void ScrollLeft() noexcept;
    void ScrollRight() noexcept;
    void ScrollUp() noexcept;
    void ScrollDown() noexcept;
    void RotateClock() noexcept;
    void RotateAnticlock() noexcept;
    QMap<int, Model::LinkInfo> LinkKB() noexcept;
    void ClearTextSelection() noexcept;
    void YankSelection(bool formatted = true) noexcept;
    void FileProperties() noexcept;
    void SaveFile() noexcept;
    void SaveAsFile() noexcept;
    void CloseFile() noexcept;
    void ToggleAutoResize() noexcept;
    void ToggleTextHighlight() noexcept;
    void ToggleRegionSelect() noexcept;
    void ToggleAnnotRect() noexcept;
    void ToggleAnnotSelect() noexcept;
    void ToggleAnnotPopup() noexcept;
    void ToggleTextSelection() noexcept;
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
    void reloadDocument() noexcept;

signals:
    void ctrlLinkClickRequested(DocumentView *view,
                                const BrowseLinkItem *linkItem);
    void requestFocus(DocumentView *view);
    void openFileFailed(DocumentView *doc);
    void openFileFinished(DocumentView *doc);
    void searchBarSpinnerShow(bool state);
    void pageChanged(int pageno);
    void zoomChanged(double factor);
    void fitModeChanged(FitMode mode);
    void selectionModeChanged(GraphicsView::Mode mode);
    void panelNameChanged(const QString &name);
    void fileNameChanged(const QString &name);
    void searchCountChanged(int count);
    void searchIndexChanged(int index);
    void totalPageCountChanged(int total);
    void clipboardContentChanged(const QString &content);
    void insertToDBRequested(const QString &filepath, int pageno);
    void highlightColorChanged(const QColor &color);
    void autoResizeActionUpdate(bool state);
    void currentPageChanged(int pageno);
    void closed();

private slots:
    void handle_password_required() noexcept;
    void handle_wrong_password() noexcept;
    void handleLinkCtrlClickRequested(QPointF scenePos) noexcept;
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
    void handleDeferredResize() noexcept;

#ifdef HAS_SYNCTEX
    void handleSynctexJumpRequested(QPointF scenePos) noexcept;
#endif
    void handleOpenFileFinished() noexcept;

protected:
    void handleContextMenuRequested(const QPoint &globalPos,
                                    bool *handled) noexcept;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;

private:
    struct HitRef
    {
        int page;
        int indexInPage;
    };

    // Total scene-axis extent across all pages
    inline double totalPageExtent() const noexcept
    {
        return m_page_offsets.empty() ? 0.0 : m_page_offsets.back();
    }

    inline bool has_text_selection() const noexcept
    {
        return (!m_selection_start.isNull() && m_selection_start_page >= 0
                && m_selection_end_page >= 0);
    }

    double pageXOffset(int pageno, double pageW, double sceneW) const noexcept;
    double pageOffset(int pageno) const noexcept;
    double pageStride(int pageno) const noexcept;
    void CopyTextFromRegion(QRectF area) noexcept;
    void CopyRegionAsImage(QRectF area) noexcept;
    void SaveRegionAsImage(QRectF area) noexcept;
    void OpenRegionInExternalViewer(QRectF area) noexcept;
    void setAutoReload(bool state) noexcept;
    bool waitUntilReadableAsync() noexcept;
    void onFileReloadRequested(const QString &path) noexcept;
    void tryReloadLater(int attempt) noexcept;

    void initGui() noexcept;
    void setModified(bool state) noexcept;
    bool pageAtScenePos(QPointF scenePos, int &outPageIndex,
                        GraphicsImageItem *&outPageItem) const noexcept;
    void requestPageRender(int pageno, bool force = false) noexcept;
    void startNextRenderJob() noexcept;
    void clearLinksForPage(int pageno) noexcept;
    void clearAnnotationsForPage(int pageno) noexcept;
    void clearSearchItemsForPage(int pageno) noexcept;
    void clearVisibleAnnotations() noexcept;
    void clearVisiblePages() noexcept;
    void clearVisibleLinks() noexcept;
    void renderPageFromImage(int pageno, const QImage &image) noexcept;
    void renderLinks(int pageno, const std::vector<Model::RenderLink> &links,
                     bool append = false) noexcept;
    void renderAnnotations(
        const int pageno,
        const std::vector<Model::RenderAnnotation> &annots) noexcept;
    void buildFlatSearchHitIndex() noexcept;
    void removeUnusedPageItems(const std::set<int> &visiblePages) noexcept;
    void clearDocumentItems() noexcept;
    void ensureVisiblePagePlaceholders() noexcept;
    void updateCurrentPage() noexcept;
    void updateCurrentHitHighlight() noexcept;
    void scrollToCurrentHit() noexcept;
    void zoomHelper() noexcept;
    void rotateHelper() noexcept;
    void cachePageStride() noexcept;
    void updateSceneRect() noexcept;
    void initConnections() noexcept;
    void resetConnections() noexcept;

    std::set<int> getPreloadPages() noexcept;
    const std::set<int> &getVisiblePages() noexcept;
    void invalidateVisiblePagesCache() noexcept;
    void removePageItem(int pageno) noexcept;
    void createAndAddPlaceholderPageItem(int pageno) noexcept;
    void prunePendingRenders(const std::set<int> &visiblePages) noexcept;
    void renderSearchHitsForPage(int pageno) noexcept;
    void renderSearchHitsInScrollbar() noexcept;
    void clearSearchHits() noexcept;
    QGraphicsPathItem *ensureSearchItemForPage(int pageno) noexcept;
    QGraphicsPathItem *m_current_search_hit_item{nullptr};
    QSizeF pageSceneSize(int pageno) const noexcept;
    std::vector<Annotation *> annotationsInArea(int pageno,
                                                QRectF area) noexcept;
    Annotation *annotationAtPoint(int pageno, QPointF point) noexcept;
    void openImageInExternalViewer(const QImage &image) noexcept;
    std::vector<std::pair<int, Annotation *>> getSelectedAnnotations() noexcept;
    void changeColorOfSelectedAnnotations(const QColor &color) noexcept;
    void stopPendingRenders() noexcept;
    int pageAtAxisCoord(double coord) const noexcept;

#ifdef HAS_SYNCTEX
    void initSynctex() noexcept;
    void synctexLocateInDocument(const char *fileName, int line) noexcept;
#endif

    Id m_id{0};
    Model *m_model{nullptr};
    GraphicsView *m_gview{nullptr};
    GraphicsScene *m_gscene{nullptr};
    const Config &m_config;
    FitMode m_fit_mode{FitMode::Width};
    int m_pageno{-1};
    double m_spacing{10.0f};
    double m_target_zoom{MIN_ZOOM_FACTOR}, m_current_zoom{MIN_ZOOM_FACTOR};
    bool m_auto_resize{false}, m_auto_reload{false};
    ScrollBar *m_hscroll{nullptr};
    ScrollBar *m_vscroll{nullptr};
    QHash<int, GraphicsImageItem *> m_page_items_hash;
    QHash<int, std::vector<BrowseLinkItem *>> m_page_links_hash;
    QHash<int, std::vector<Annotation *>> m_page_annotations_hash;
    QSet<int> m_pending_renders;
    QQueue<int> m_render_queue;
    JumpMarker *m_jump_marker{nullptr};
    QTimer *m_scroll_page_update_timer{nullptr};
    QTimer *m_resize_timer{nullptr};
    PageLocation m_pending_jump{-1, 0, 0};
    int m_search_index{-1};
    QMap<int, std::vector<Model::SearchHit>> m_search_hits;
    std::vector<HitRef> m_search_hit_flat_refs;
    QHash<int, QGraphicsPathItem *> m_search_items;
    QPointF m_selection_start, m_selection_end;
    int m_selection_start_page{-1}, m_selection_end_page{-1};
    int m_last_selection_page{-1};
    QGraphicsPathItem *m_selection_path_item{nullptr};
    QTimer *m_hq_render_timer{nullptr};
    std::vector<PageLocation> m_loc_history;
    int m_loc_history_index{-1};
    bool m_is_modified{false};
    // fz_pixmap *m_hit_pixmap{nullptr};
    LayoutMode m_layout_mode{LayoutMode::TOP_TO_BOTTOM};
    WaitingSpinnerWidget *m_spinner{nullptr};
    bool m_visible_pages_dirty{true};
    bool m_deferred_fit{false};
    bool m_scroll_to_hit_pending{false};
    QFutureWatcher<void> m_open_future_watcher;
    QFileSystemWatcher *m_file_watcher{nullptr};
    DocumentContainer *m_container{nullptr};
    std::vector<LinkHint *> m_kb_link_hints{};
    std::vector<double> m_page_offsets{};
    std::set<int> m_visible_pages_cache;

    // max cross-axis page size, cached by cachePageStride()
    double m_max_page_cross_extent{0.0};

    unsigned int MAX_CONCURRENT_RENDERS{
        std::thread::hardware_concurrency() > 1
            ? std::thread::hardware_concurrency() - 1
            : 1};
    std::atomic<int> m_renders_in_flight{0};
    std::shared_ptr<std::atomic<bool>> m_cancelled{
        std::make_shared<std::atomic<bool>>(false)};

    QPointF m_old_jump_marker_pos{};

#ifdef HAS_SYNCTEX
    synctex_scanner_p m_synctex_scanner{nullptr};
#endif

    // Portal
    DocumentView *m_source_view{nullptr};
    DocumentView *m_portal_view{nullptr};

    enum Direction
    {
        LEFT = 0,
        RIGHT,
        UP,
        DOWN
    };

    // Visual Line Mode
    QGraphicsPathItem *m_visual_line_item{nullptr};
    int m_visual_line_index{-1};
    bool m_visual_line_mode{false};
    std::vector<Model::VisualLineInfo> m_visual_lines{};
    void visual_line_move(Direction direction) noexcept;
    void snap_visual_line(bool centerView = true) noexcept;

    int m_generation = 0; // save generation count for pending render jobs to detect stale jobs
};
