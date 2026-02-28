#pragma once

// Wrapper for MuPDF Model

#include "Annotations/Annotation.hpp"
#include "BrowseLinkItem.hpp"
#include "LRUCache.hpp"

#include <QColor>
#include <QFuture>
#include <QPixmap>
#include <QRectF>
#include <QRegularExpression>
#include <QString>
#include <QUndoStack>
#include <unordered_map>

extern "C"
{
#include <mupdf/fitz.h>
#include <mupdf/fitz/geometry.h>
#include <mupdf/fitz/image.h>
#include <mupdf/pdf.h>

#ifdef HAS_DJVU
#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>
#endif
}

#define CSTR(x) x.toStdString().c_str()

// Forward declaration
class TextHighlightAnnotationCommand;
class TextAnnotationCommand;
class DocumentView;

class Model : public QObject
{
    Q_OBJECT
public:
    Model(QObject *parent = nullptr) noexcept;
    ~Model() noexcept;

    enum class FileType
    {
        NONE = 0,
        PDF,
        CBZ,
        MOBI,
        SVG,
        XPS,
        EPUB,
        FB2,
        TIFF,
        PNG,
        JPG,
#ifdef HAS_DJVU
        DJVU
#endif
    };

    struct LinkInfo
    {
        QString uri;
        fz_link_dest dest;
        BrowseLinkItem::LinkType type{BrowseLinkItem::LinkType::External};
        int target_page{-1};
        BrowseLinkItem::PageLocation target_loc{0, 0, 0};
        BrowseLinkItem::PageLocation source_loc{0, 0, 0};
        int source_page{-1};
    };

    struct SearchHit
    {
        int page;
        fz_quad quad; // Coordinate of the hit in logical page space
        int index;    // Index of the hit in the page
    };

    struct HighlightText
    {
        int page;
        QString text;
        fz_quad quad;
    };
    struct EncryptInfo
    {
        QString user_password;
        QString owner_password;
        int perm_flags{0};
        int enc_level{128}; // 40, 128, 256
    };

    struct RenderJob
    {
        int pageno;
        double zoom;
        int rotation;
        double dpi;
        double dpr;
        bool invert_color;
        fz_colorspace *colorspace;
        QString filepath; // path to PDF
    };

    struct RenderLink
    {
        QRectF rect;
        QString uri;
        BrowseLinkItem::LinkType type{BrowseLinkItem::LinkType::External};
        bool boundary{false};
        int target_page{-1};
        BrowseLinkItem::PageLocation target_loc{0, 0, 0};
        BrowseLinkItem::PageLocation source_loc{0, 0, 0};
    };

    struct RenderAnnotation
    {
        QRectF rect;
        enum pdf_annot_type type;
        QColor color;
        QString text;
        int index{-1};
    };

    struct PageRenderResult
    {
        QImage image;
        std::vector<RenderLink> links;
        std::vector<RenderAnnotation> annotations;
    };

    inline bool supports_links() const noexcept
    {
        return (m_filetype == FileType::PDF || m_filetype == FileType::EPUB
                || m_filetype == FileType::XPS || m_filetype == FileType::FB2);
    }

    inline bool supports_text_selection() const noexcept
    {
        return m_filetype == FileType::PDF || m_filetype == FileType::EPUB
               || m_filetype == FileType::XPS || m_filetype == FileType::FB2
               || m_filetype == FileType::MOBI;
    }

    inline bool supports_text_search() const noexcept
    {
        return supports_text_selection();
    }

    inline bool supports_outline() const noexcept
    {
        return m_filetype == FileType::PDF || m_filetype == FileType::EPUB
               || m_filetype == FileType::XPS || m_filetype == FileType::FB2 ||
#ifdef HAS_DJVU
               m_filetype == FileType::DJVU ||
#endif
               m_filetype == FileType::MOBI;
    }

    inline bool supports_metadata() const noexcept
    {
        return supports_outline();
    }

    inline bool supports_annotations() const noexcept
    {
        return m_filetype == FileType::PDF;
    }

    inline bool supports_save()
    {
        return supports_annotations();
    }
    inline bool supports_encryption()
    {
        return supports_annotations();
    }
    inline bool supports_decryption()
    {
        return supports_annotations();
    }

    inline fz_context *cloneContext() const noexcept
    {
        return fz_clone_context(m_ctx);
    }

    inline void setRotation(float angle) noexcept
    {
        m_rotation = angle;
    }

    inline float rotation() const noexcept
    {
        return m_rotation;
    }

    inline float zoom() const noexcept
    {
        return m_zoom;
    }

    inline int searchMatchesCount() const noexcept
    {
        return m_search_match_count;
    }

    inline QString filePath() const noexcept
    {
        return m_filepath;
    }

    inline int numPages() const noexcept
    {
        return m_page_count;
    }

    inline QUndoStack *undoStack() noexcept
    {
        return m_undo_stack;
    }

    inline void setInvertColor(bool invert) noexcept
    {
        m_invert_color = invert;
    }

    inline bool invertColor() const noexcept
    {
        return m_invert_color;
    }

    inline void setDPR(float dpr) noexcept
    {
        m_dpr     = dpr;
        m_inv_dpr = 1.0f / dpr;
    }

    inline float DPR() const noexcept
    {
        return m_dpr;
    }

    inline void setDPI(float dpi) noexcept
    {
        m_dpi = dpi;
    }

    inline float DPI() noexcept
    {
        return m_dpi;
    }

    inline float invDPR() const noexcept
    {
        return m_inv_dpr;
    }

    inline bool success() const noexcept
    {
        return m_success;
    }

    inline QColor highlightAnnotColor() const noexcept
    {
        return QColor(static_cast<int>(m_highlight_color[0] * 255),
                      static_cast<int>(m_highlight_color[1] * 255),
                      static_cast<int>(m_highlight_color[2] * 255),
                      static_cast<int>(m_highlight_color[3] * 255));
    }

    inline bool hasUnsavedChanges() const noexcept
    {
        // If the undo stack says we are at the 'save point', it's clean.
        if (m_undo_stack && !m_undo_stack->isClean())
            return true;

        // Fallback: Ask MuPDF if the bitstream is dirty
        return pdf_has_unsaved_changes(m_ctx, m_pdf_doc);
    }

    // This is the "Logical" scale for the UI
    inline float logicalScale() const noexcept
    {
        return m_zoom * (m_dpi / 72.0f);
    }

    // This is the "Physical" scale for the actual pixels
    inline float physicalScale() const noexcept
    {
        return logicalScale() * m_dpr;
    }

    inline void setLinkBoundary(bool state) noexcept
    {
        m_link_show_boundary = state;
    }

    inline void setDetectUrlLinks(bool state) noexcept
    {
        m_detect_url_links = state;
    }

    // Cache management
    inline size_t pageCacheSize() const noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        return m_page_lru_cache.size();
    }

    inline void setCacheCapacity(const size_t n) noexcept
    {
        m_page_lru_cache.setCapacity(n);
    }

    void rotateClock() noexcept;
    void rotateAnticlock() noexcept;
    void setZoom(float zoom) noexcept;
    void setUrlLinkRegex(const QString &pattern) noexcept;

    // Clear page cache to free memory (e.g., when tab becomes inactive)
    void clearPageCache() noexcept;

    // Ensure a page is cached (lazy loading)
    void ensurePageCached(int pageno) noexcept;

    inline void setBackgroundColor(const uint32_t bg) noexcept
    {
        m_bg_color = bg;
    }

    inline void setForegroundColor(const uint32_t fg) noexcept
    {
        m_fg_color = fg;
    }

    [[nodiscard]] inline uint32_t backgroundColor() noexcept
    {
        return m_bg_color;
    }

    [[nodiscard]] inline uint32_t foregroundColor() noexcept
    {
        return m_fg_color;
    }

    [[nodiscard]] inline float DPI() const noexcept
    {
        return m_dpi;
    }

    // Clear cached fz_stext_page objects
    // inline void clear_fz_stext_page_cache() noexcept
    // {
    // for (auto &pair : m_stext_page_cache)
    //     fz_drop_stext_page(m_ctx, pair.second);
    // m_stext_page_cache.clear();
    //     m_stext_page_cache.clear();
    // }

    [[nodiscard]] inline const float *annotRectColor() const noexcept
    {
        return m_annot_rect_color;
    }

    [[nodiscard]] inline FileType fileType() const noexcept
    {
        return m_filetype;
    }

    RenderJob createRenderJob(int pageno) const noexcept;
    void requestPageRender(
        const RenderJob &job,
        const std::function<void(PageRenderResult)> &callback) noexcept;
    PageRenderResult renderPageWithExtrasAsync(const RenderJob &job) noexcept;

    // fz_pixmap *hitTestImage(int pageno, QPointF pt, float zoom,
    //                         float rotation) noexcept;

    std::vector<std::pair<QString, QString>> properties() noexcept;
    fz_outline *getOutline() noexcept;
    void cancelOpen() noexcept;
    QFuture<void> openAsync(const QString &filePath) noexcept;

    QFuture<void> openAsync_mupdf(const QString &canonicalPath) noexcept;
#ifdef HAS_DJVU
    QFuture<void> openAsync_djvu(const QString &canonicalPath) noexcept;
#endif
    void _continueOpen(fz_context *ctx, fz_document *doc) noexcept;

    QFuture<void> submitPassword(const QString &password) noexcept;
    void close() noexcept;
    void cleanup_mupdf() noexcept;
#ifdef HAS_DJVU
    void cleanup_djvu() noexcept;
#endif
    bool decrypt() noexcept;
    bool encrypt(const EncryptInfo &info) noexcept;
    void setPopupColor(const QColor &color) noexcept;
    void setHighlightColor(const QColor &color) noexcept;
    void setSelectionColor(const QColor &color) noexcept;
    void setAnnotRectColor(const QColor &color) noexcept;
    bool SaveChanges() noexcept;
    bool SaveAs(const QString &newFilePath) noexcept;
    QPointF toPixelSpace(int pageno, fz_point pt) const noexcept;
    fz_point toPDFSpace(int pageno, QPointF pt) const noexcept;

    std::vector<QPolygonF> computeTextSelectionQuad(int pageno, QPointF start,
                                                    QPointF end) noexcept;
    std::vector<QPolygonF> selectWordAt(int pageno, fz_point pt) noexcept;
    std::vector<QPolygonF> selectLineAt(int pageno, fz_point pt) noexcept;
    std::vector<QPolygonF> selectParagraphAt(int pageno, fz_point pt) noexcept;

    QString get_selected_text(int pageno, QPointF a, QPointF b,
                              bool formatted) noexcept;
    void highlight_text_selection(int pageno, QPointF start,
                                  QPointF end) noexcept;
    void invalidatePageCaches() noexcept;
    void invalidatePageCache(int pageno) noexcept;
    void search(const QString &term, bool caseSensitive = false,
                bool useRegex = false) noexcept;
    void searchInPage(const int pageno, const QString &term,
                      bool caseSensitive = false) noexcept;
    std::vector<Model::SearchHit> searchHelper(int pageno, const QString &term,
                                               bool caseSensitive) noexcept;
    std::vector<Model::SearchHit>
    searchHelperRegex(int pageno, const QRegularExpression &re) noexcept;

    std::vector<HighlightText> collectHighlightTexts(bool groupByLine
                                                     = true) noexcept;
    void annotChangeColor(int pageno, int index, const QColor &color) noexcept;

signals:
    void undoStackCleanChanged(bool clean);
    void urlLinksReady(int pageno, std::vector<RenderLink> links);
    void passwordRequired();
    void wrongPassword();
    void openFileFailed();
    void openFileFinished();
    void reloadRequested(int pageno);
    void
    searchResultsReady(const QMap<int, std::vector<Model::SearchHit>> &results);
    void searchPartialResultsReady(
        const QMap<int, std::vector<Model::SearchHit>> &results);

private:
    inline void waitForRenders() noexcept
    {
        if (m_render_future.isRunning())
            m_render_future.waitForFinished();
    }

    struct CachedLink
    {
        fz_rect rect; // page space
        QString uri;
        BrowseLinkItem::LinkType type;

        // optional extras
        int target_page = -1;
        fz_point target_loc{}; // It's target location
        fz_point source_loc{}; // It's own location
        float zoom = 0.0f;
    };

    struct CachedAnnotation
    {
        fz_rect rect; // for non-highlight annotations
        enum pdf_annot_type type;
        QColor color;
        QString text;
        int index;
        float opacity;
    };

    struct PageDimension
    {
        float width_pts{0.0f}, height_pts{0.0f};
    };

    // Cache for page dimensions (W, H)
    struct PageDimensionCache
    {
        std::vector<PageDimension> dimensions;
        std::vector<bool> known;

        void reset(int page_count)
        {
            dimensions.assign(page_count, PageDimension{});
            known.assign(page_count, 0);
        }

        bool isKnown(int pageno) const
        {
            return pageno >= 0 && pageno < static_cast<int>(known.size())
                   && known[pageno] != false;
        }

        void set(int p, float w, float h)
        {
            if (p < 0 || p >= (int)dimensions.size())
                return;
            dimensions[p] = PageDimension{w, h};
            known[p]      = true;
        }

        PageDimension getOrDefault(int p, const PageDimension &def) const
        {
            if (p < 0 || p >= (int)dimensions.size())
                return def;
            return known[p] ? dimensions[p] : def;
        }

        PageDimension get(int p, const PageDimension &fallback) const
        {
            if (p < 0 || p >= (int)dimensions.size())
                return fallback;
            return dimensions[p];
        }
    };

    [[nodiscard]] inline PageDimension
    page_dimension_pts(int pageno) const noexcept
    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        return m_page_dim_cache.getOrDefault(pageno, m_default_page_dim);
    }

    [[nodiscard]] inline bool page_dimension_known(int pageno) const noexcept
    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        return m_page_dim_cache.isKnown(pageno);
    }

    struct PageCacheEntry
    {
        int pageno;
        fz_display_list *display_list{nullptr};
        fz_rect bounds{};

        PageDimension dimension{};
        std::vector<CachedLink> links;
        std::vector<CachedAnnotation> annotations;
#ifdef HAS_DJVU
        QImage cached_image; // for DJVU
#endif
    };

    struct CachedTextChar
    {
        uint32_t rune;
        fz_quad quad;
    };

    struct CachedTextPage
    {
        std::vector<CachedTextChar> chars;
    };

    // Used for hit-testing images on a page
    struct ImageHitTestDevice
    {
        fz_device super;
        fz_point query;
        fz_image *img{nullptr};
    };

    // Used for password handling
    struct PendingOpen
    {
        fz_context *ctx{nullptr};
        fz_document *doc{nullptr};

        void clear() noexcept
        {
            ctx = nullptr;
            doc = nullptr;
        }
    };
    void initMuPDF() noexcept;

    [[nodiscard]] std::string getTextInArea(const int pageno, QPointF start,
                                            QPointF end) noexcept;
    [[nodiscard]] std::tuple<float, float>
    getPageDimensions(int pageno) const noexcept;

    QString m_filepath;
    int m_page_count{0};
    float m_dpr{1.0f}, m_dpi{96.0f}, m_zoom{1.0f}, m_rotation{0.0f},
        m_inv_dpr{1.0f};
    bool m_invert_color{false};

    // private helper in Model
    std::vector<QPolygonF> selectAtHelper(int pageno, fz_point pt,
                                          int snapMode) noexcept;

    std::pair<fz_matrix, fz_matrix>
    buildPageTransforms(int pageno) const noexcept;
    void buildPageCache(int pageno) noexcept;
#ifdef HAS_DJVU
    void buildPageCache_djvu(int pageno) noexcept;
#endif
    int addRectAnnotation(const int pageno, const fz_rect &rect) noexcept;
    int addHighlightAnnotation(const int pageno,
                               const std::vector<fz_quad> &quads) noexcept;
    int addTextAnnotation(const int pageno, const fz_rect &rect,
                          const QString &text) noexcept;
    void setTextAnnotationContents(const int pageno, const int objNum,
                                   const QString &text) noexcept;
    void removeAnnotations(const int pageno,
                           const std::vector<int> &objNums) noexcept;
    void buildTextCacheForPages(const std::set<int> &pagenos) noexcept;
    void LRUEvictFunction(PageCacheEntry &entry) noexcept;
    fz_stext_page *get_or_build_stext_page(fz_context *ctx,
                                           int pageno) noexcept;

    void populatePDFProperties(
        std::vector<std::pair<QString, QString>> &props) noexcept;
    fz_point getFirstCharPos(const int pageno) noexcept;
    std::vector<Model::RenderLink>
    detectUrlLinksForPage(const RenderJob &job) noexcept;
    FileType detectFileType(const QString &path) noexcept;

    QUndoStack *m_undo_stack{nullptr};
    // std::optional<std::wstring>
    // get_paper_name_at_position(const int pageno, const fz_point) noexcept;

    float m_popup_color[4]{1.0f, 1.0f, 0.8f, 0.8f},
        m_highlight_color[4]{1.0f, 1.0f, 0.0f, 0.5f},
        m_selection_color[4]{0.0f, 0.0f, 1.0f, 0.3f},
        m_annot_rect_color[4]{1.0f, 0.0f, 0.0f, 0.5f};
    uint32_t m_bg_color{0};
    uint32_t m_fg_color{0};
    bool m_success{false};

    mutable std::recursive_mutex m_page_cache_mutex;
    LRUCache<int, PageCacheEntry> m_page_lru_cache;
    LRUCache<int, CachedTextPage> m_text_cache;
    LRUCache<int, fz_stext_page *> m_stext_page_cache;

    bool reloadDocument() noexcept;
    PageDimensionCache m_page_dim_cache{};
    mutable std::mutex m_page_dim_mutex;
    PageDimension m_default_page_dim{};

    mutable std::mutex m_doc_mutex;
    QFuture<PageRenderResult> m_render_future;
    QFuture<void> m_search_future;
    pdf_write_options m_pdf_write_options{pdf_default_write_options};
    std::atomic<int> m_search_match_count{0};
    bool m_link_show_boundary{false};
    bool m_detect_url_links{false};
    QRegularExpression m_url_link_re;
    FileType m_filetype{FileType::NONE};
    PendingOpen m_pending;

    // MuPDF core objects
    fz_locks_context m_fz_locks;
    fz_context *m_ctx{nullptr};
    fz_document *m_doc{nullptr};
    pdf_document *m_pdf_doc{nullptr};
    fz_colorspace *m_colorspace{nullptr};
    fz_outline *m_outline{nullptr};

#ifdef HAS_DJVU

    // DJVU core objects
    ddjvu_context_t *m_ddjvu_ctx{nullptr};
    ddjvu_document_t *m_ddjvu_doc{nullptr};
#endif

    // For use with visual line mode
    struct VisualLineInfo
    {
        QRectF bbox; // in page coordinates
        int pageno;
    };

    std::vector<VisualLineInfo> get_text_lines(int pageno) noexcept;
    int
    visual_line_index_at_pos(QPointF scenePos,
                             const std::vector<VisualLineInfo> &lines) noexcept;

    friend class TextHighlightAnnotationCommand;
    friend class RectAnnotationCommand;
    friend class TextAnnotationCommand;
    friend class DeleteAnnotationsCommand;
    friend class DocumentView;
};
