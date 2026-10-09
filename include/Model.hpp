#pragma once

// Wrapper for MuPDF Model

#include "Annotation.hpp"
#include "BrowseLinkItem.hpp"
#include "LRUCache.hpp"

#include <QColor>
#include <QFuture>
#include <QPixmap>
#include <QRectF>
#include <QRegularExpression>
#include <QString>
#include <QUndoStack>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

extern "C"
{
#include <mupdf/fitz.h>
#include <mupdf/fitz/geometry.h>
#include <mupdf/fitz/image.h>
#include <mupdf/pdf.h>
}

// Cancels a render, including one that is already drawing: besides the flag
// the worker polls between phases, the MuPDF cookie is handed to the display
// list replay, which polls it while drawing and stops early.
struct RenderCancel
{
    std::atomic<bool> flag{false};
    fz_cookie cookie{};

    void cancel() noexcept
    {
        flag.store(true, std::memory_order_release);
        std::atomic_ref<int>(cookie.abort).store(1, std::memory_order_relaxed);
    }
    [[nodiscard]] bool cancelled() const noexcept
    {
        return flag.load(std::memory_order_acquire);
    }
};

// Forward declaration
class ImageAnimation;
class TextHighlightAnnotationCommand;
class TextAnnotationCommand;
class DocumentView;

struct Config;

class Model : public QObject
{
    Q_OBJECT
public:
    using Properties = std::vector<std::pair<QString, QString>>;
    using FileSize   = uint64_t;

    Model(const Config &config, QObject *parent = nullptr) noexcept;
    ~Model() noexcept;

    enum class FileType
    {
        NONE = 0,
        PDF,
        DJVU,
        XPS,
        CBZ,
        EPUB,
        FB2,
        MOBI,
        HTML,
        MD,
        TXT,
        DOCX,
        XLSX,
        PPTX,
        // Images
        JPG,
        PNG,
        SVG,
        APNG,
        BMP,
        GIF,
        WEBP,
        TIFF,
        TGA,
        ICO,
        PPM,
        PGM,
        PBM,
    };

    struct LinkInfo
    {
        QString uri;
        fz_link_dest dest;
        BrowseLinkItem::LinkType type = BrowseLinkItem::LinkType::External;
        int target_page               = -1;
        BrowseLinkItem::PageLocation target_loc = {0, 0, 0};
        BrowseLinkItem::PageLocation source_loc = {0, 0, 0};
        int source_page                         = -1;
    };

    // A link on a page: where it is (page space, points) and where it goes.
    struct PageLink
    {
        QRectF rect;
        LinkInfo info;
    };

    // Links of a page, in the order the document lists them. Loads the page
    // if needed, so it can take a moment on a page that was never shown.
    // Empty for formats without links or an out-of-range page.
    [[nodiscard]] std::vector<PageLink> pageLinks(int pageno) noexcept;

    // Page size in points (before rotation and zoom). `known` is false when
    // the page was never loaded and the size is the document default;
    // `exact` loads the page first so the size is always real.
    [[nodiscard]] inline QSizeF pageSizePts(int pageno, bool exact = false,
                                            bool *known = nullptr) noexcept
    {
        if (exact)
            ensurePageCached(pageno);
        if (known)
            *known = page_dimension_known(pageno);
        const PageDimension d = page_dimension_pts(pageno);
        return {d.width_pts, d.height_pts};
    }

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
        QString comment;
        fz_quad quad;
    };

    struct AnnotCommentInfo
    {
        int page;
        QString comment;
        fz_rect rect;
    };

    // One annotation of a page, in page coordinates (points, y down).
    struct AnnotationInfo
    {
        int objNum               = -1; // identifies it on its page
        enum pdf_annot_type type = PDF_ANNOT_UNKNOWN;
        fz_rect rect{};
        std::vector<fz_quad> quads; // highlights: one per line
        QColor color;               // with the opacity as its alpha
        QString contents;           // the comment
        QString text;               // highlights: the text under it
    };

    // What is known about an image file (see imageMetadata()).
    struct ImageMetadata
    {
        int width = 0, height = 0; // pixels
        QString format;            // "png", "jpeg", ...
        bool animated = false;
        int frames    = 1;           // frames of an animation, pages of a TIFF
        double dpi_x = 0, dpi_y = 0; // 0: not stored in the file
        Properties exif;             // tag name -> value
    };

    struct EncryptInfo
    {
        QString user_password;
        QString owner_password;
        int perm_flags = 0;
        int enc_level  = 128; // 40, 128, 256
    };

    struct RenderJob
    {
        int pageno;
        double zoom;
        int rotation;
        double dpi;
        double dpr;
        bool invert_color;
        bool flip_h               = false;
        bool flip_v               = false;
        fz_colorspace *colorspace = nullptr;
        QString filepath; // path to PDF
        // Part of the page (fractions of its width/height) that should be
        // sharp. Only used when the whole page would be too many pixels to
        // render in one go.
        QRectF clip_frac;
        bool has_clip = false;
    };

    struct RenderLink
    {
        QRectF rect;
        QString uri;
        BrowseLinkItem::LinkType type = BrowseLinkItem::LinkType::External;
        bool boundary                 = false;
        int target_page               = -1;
        BrowseLinkItem::PageLocation target_loc = {0, 0, 0};
        BrowseLinkItem::PageLocation source_loc = {0, 0, 0};
    };

    struct RenderAnnotation
    {
        QRectF rect;
        std::vector<QRectF> rects; // per-line rects for highlight annotations
        enum pdf_annot_type type;
        QColor color;
        QString text;
        int index = -1;
    };

    struct PageRenderResult
    {
        QImage image;
        // When the page was too large to render whole, `image` only covers
        // `region` (device pixels) of a `full_size` page.
        QSize full_size;
        QRect region;
        bool partial   = false;
        // Set when the render was skipped (document closing/reloading or
        // the caller cancelled it); `image` is null in that case.
        bool cancelled = false;
        // Timing of the render, for LEKTRA_RENDER_TRACE (ms).
        double queue_ms  = 0; // waiting for a worker thread
        double cache_ms  = 0; // loading the page into the page cache
        double render_ms = 0; // drawing it
        std::vector<RenderLink> links;
        std::vector<RenderAnnotation> annotations;
    };

    // Capability queries

    // This is useful for `dont_invert_images` config option.
    [[nodiscard]] inline bool supports_image_blocks() const noexcept
    {
        switch (m_filetype)
        {
            case FileType::PDF:
            case FileType::EPUB:
            case FileType::XPS:
            case FileType::FB2:
            case FileType::MOBI:
            case FileType::HTML:
            case FileType::MD:
            case FileType::DOCX:
            case FileType::XLSX:
            case FileType::PPTX:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] inline bool supports_links() const noexcept
    {
        switch (m_filetype)
        {
            case FileType::PDF:
            case FileType::EPUB:
            case FileType::XPS:
            case FileType::FB2:
            case FileType::HTML:
            case FileType::MD:
            case FileType::DOCX:
            case FileType::XLSX:
            case FileType::PPTX:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] inline bool supports_text_selection() const noexcept
    {
        switch (m_filetype)
        {
            case FileType::PDF:
            case FileType::EPUB:
            case FileType::XPS:
            case FileType::FB2:
            case FileType::MOBI:
            case FileType::HTML:
            case FileType::MD:
            case FileType::TXT:
            case FileType::DOCX:
            case FileType::XLSX:
            case FileType::PPTX:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] inline bool supports_text_search() const noexcept
    {
        return supports_text_selection();
    }

    // True if at least one page has extractable text. Scans pages until one
    // is found; the answer is cached until the document changes.
    [[nodiscard]] bool hasTextLayer() noexcept;

    [[nodiscard]] inline bool supports_outline() const noexcept
    {
        return m_filetype == FileType::PDF || m_filetype == FileType::EPUB
               || m_filetype == FileType::XPS || m_filetype == FileType::FB2
               || m_filetype == FileType::DJVU || m_filetype == FileType::MOBI
               || m_filetype == FileType::HTML || m_filetype == FileType::MD
               || m_filetype == FileType::DOCX || m_filetype == FileType::XLSX
               || m_filetype == FileType::PPTX;
    }

    // True for HTML-backed formats whose pagination MuPDF computes by
    // actually reflowing text into a page box of a given size
    // (fz_layout_document). XPS is chaptered the same way EPUB is but is
    // a fixed-layout format — its pages are not reflowable — so it is
    // deliberately excluded here. HTML/Markdown/plain text are all opened
    // via the same shared HTML-document backend as EPUB (MuPDF converts
    // Markdown/text to HTML internally before laying it out), so they
    // reflow the same way.
    [[nodiscard]] inline bool supports_reflow() const noexcept
    {
        return m_filetype == FileType::EPUB || m_filetype == FileType::FB2
               || m_filetype == FileType::MOBI || m_filetype == FileType::HTML
               || m_filetype == FileType::MD || m_filetype == FileType::TXT
               || m_filetype == FileType::DOCX || m_filetype == FileType::XLSX
               || m_filetype == FileType::PPTX;
    }

    [[nodiscard]] inline bool supports_annotations() const noexcept
    {
        return m_filetype == FileType::PDF;
    }

    // Whether pages can be written with MuPDF's document writers (not for
    // DjVu or image documents).
    [[nodiscard]] inline bool supportsWriterExport() const noexcept
    {
        return m_doc != nullptr && !m_is_image && m_filetype != FileType::DJVU;
    }
    // Writes pages (0-based) as pdf, text (txt), html, xhtml, cbz, docx, odt
    // or svg. All but svg make one file (`paths` has one name); svg makes one
    // file per page (`paths` has a name for each). On failure returns false
    // with the reason in `error`.
    bool exportWithWriter(const std::vector<int> &pages,
                          const QStringList &paths, const QString &format,
                          QString *error = nullptr) noexcept;

    [[nodiscard]] inline bool supports_save() const noexcept
    {
        return supports_annotations();
    }

    [[nodiscard]] inline bool supports_encryption() const noexcept
    {
        return supports_annotations();
    }

    [[nodiscard]] inline bool supports_decryption() const noexcept
    {
        return supports_annotations();
    }

    [[nodiscard]] inline fz_context *cloneContext() const noexcept
    {
        return fz_clone_context(m_ctx);
    }

    inline void setRotation(float angle) noexcept
    {
        m_rotation = angle;
    }

    [[nodiscard]] inline bool isFlippedH() const noexcept
    {
        return m_flip_h;
    }
    [[nodiscard]] inline bool isFlippedV() const noexcept
    {
        return m_flip_v;
    }

    inline void setFlipH(bool v) noexcept
    {
        m_flip_h = v;
    }
    inline void setFlipV(bool v) noexcept
    {
        m_flip_v = v;
    }
    inline void toggleFlipH() noexcept
    {
        m_flip_h = !m_flip_h;
    }
    inline void toggleFlipV() noexcept
    {
        m_flip_v = !m_flip_v;
    }

    [[nodiscard]] inline bool isAnimated() const noexcept
    {
        return m_is_animated;
    }

    [[nodiscard]] inline bool isImage() const noexcept
    {
        return m_is_image;
    }

    [[nodiscard]] inline float rotation() const noexcept
    {
        return m_rotation;
    }

    [[nodiscard]] inline float zoom() const noexcept
    {
        return m_zoom;
    }

    [[nodiscard]] inline int searchMatchesCount() const noexcept
    {
        return m_search_match_count;
    }

    [[nodiscard]] inline const QString &filePath() const noexcept
    {
        return m_filepath;
    }

    [[nodiscard]] inline int numPages() const noexcept
    {
        return m_page_count;
    }

    [[nodiscard]] inline QUndoStack *undoStack() const noexcept
    {
        return m_undo_stack;
    }

    inline void setInvertColor(bool invert) noexcept
    {
        m_invert_color = invert;
    }

    [[nodiscard]] inline bool invertColor() const noexcept
    {
        return m_invert_color;
    }

    [[nodiscard]] inline float DPI() const noexcept
    {
        return m_dpi;
    }

    inline void setDPR(float dpr) noexcept
    {
        m_dpr     = dpr;
        m_inv_dpr = 1.0f / dpr;
    }

    [[nodiscard]] inline float DPR() const noexcept
    {
        return m_dpr;
    }

    inline void setDPI(float dpi) noexcept
    {
        m_dpi = dpi;
    }

    [[nodiscard]] inline float invDPR() const noexcept
    {
        return m_inv_dpr;
    }

    [[nodiscard]] inline bool success() const noexcept
    {
        return m_success;
    }

    [[nodiscard]] inline QColor highlightAnnotColor() const noexcept
    {
        return QColor(qRound(m_highlight_color[0] * 255),
                      qRound(m_highlight_color[1] * 255),
                      qRound(m_highlight_color[2] * 255),
                      qRound(m_highlight_color[3] * 255));
    }

    [[nodiscard]] inline bool hasUnsavedChanges() const noexcept
    {
        if (m_filetype == FileType::DJVU)
            return false;
        return pdf_has_unsaved_changes(m_ctx, m_pdf_doc);
    }

    // This is the "Logical" scale for the UI
    [[nodiscard]] inline float logicalScale() const noexcept
    {
        return m_zoom * (m_dpi / 72.0f);
    }

    // This is the "Physical" scale for the actual pixels
    [[nodiscard]] inline float physicalScale() const noexcept
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
    [[nodiscard]] inline size_t pageCacheSize() const noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        return m_page_lru_cache.size();
    }

    inline void setCacheCapacity(const size_t n) noexcept
    {
        m_page_lru_cache.setCapacity(n);
    }

    inline void setBackgroundColor(const uint32_t bg) noexcept
    {
        m_bg_color = bg;
    }

    inline void setForegroundColor(const uint32_t fg) noexcept
    {
        m_fg_color = fg;
    }

    [[nodiscard]] inline uint32_t backgroundColor() const noexcept
    {
        return m_bg_color;
    }

    [[nodiscard]] inline uint32_t foregroundColor() const noexcept
    {
        return m_fg_color;
    }

    [[nodiscard]] inline const float *annotRectColor() const noexcept
    {
        return m_annot_rect_color;
    }

    [[nodiscard]] inline FileType fileType() const noexcept
    {
        return m_filetype;
    }

    void rotateClock() noexcept;
    void rotateAnticlock() noexcept;
    void setZoom(float zoom) noexcept;
    void setUrlLinkRegex(const QString &pattern) noexcept;
    void clearPending() noexcept;
    void clearPageCache() noexcept;
    void ensurePageCached(int pageno) noexcept;
    RenderJob createRenderJob(int pageno) const noexcept;
    // `cancel` (optional) is polled by the worker; once set, the render is
    // abandoned and `callback` receives a result with `cancelled ==
    // true`.
    void
    requestPageRender(const RenderJob &job,
                      const std::function<void(PageRenderResult)> &callback,
                      std::shared_ptr<RenderCancel> cancel
                      = nullptr) noexcept;
    QImage requestImageRender(bool highQuality = false) noexcept;
    // Rasterizes a page (plus its links/annotations) off the GUI
    // thread. `cancel` (optional) is polled between the render
    // phases; once set the remaining phases are skipped and the
    // result comes back with `cancelled == true` and a null
    // image. The display list replay polls the token's MuPDF cookie, so a
    // render already drawing stops early too.
    PageRenderResult renderPageWithExtrasAsync(
        const RenderJob &job,
        const std::shared_ptr<RenderCancel> &cancel = nullptr) noexcept;
    [[nodiscard]] QImage renderRegionAtDPI(int pageno, QRectF logicalRect,
                                           float targetDPI) noexcept;
    // Renders a rectangle of a page, given in page-space points, at the given
    // DPI (with the document's current rotation, flips and colours). Loads the
    // page if needed and uses its own MuPDF context, so it may run off the
    // GUI thread. Null for image/DjVu documents.
    [[nodiscard]] QImage renderPtsRegion(int pageno, QRectF ptsRect,
                                         float targetDPI) noexcept;

    Properties properties() noexcept;
    // False if the document is not an image.
    bool imageMetadata(ImageMetadata &out) noexcept;
    fz_outline *getOutline() noexcept;
    fz_outline *getGeneratedOutline() noexcept
    {
        return m_generated_outline;
    }
    fz_outline *generateOutline(float min_ratio, int max_levels) noexcept;

    // The outline as a flat list (one entry per node, in document order, with
    // the document-wide page of each). Built once and kept, so opening the
    // outline picker does not have to walk and resolve the outline again.
    // Uses the embedded outline, or the generated one if there is none.
    struct OutlineEntry
    {
        QString title;
        int depth;
        int page; // 0-based
        QPointF location;
        bool isHeading; // has children
    };
    const std::vector<OutlineEntry> &outlineEntries() noexcept;
    // Loads the embedded outline and builds the entries on a worker thread, so
    // opening the outline picker later is instant and the GUI is never held up.
    // Does nothing if they are already built or being built.
    void prefetchOutlineAsync() noexcept;
    // The entries of any outline of this document (not kept).
    std::vector<OutlineEntry> buildOutlineEntries(fz_outline *outline) noexcept;
    bool exportOutlineToFile(const QString &path, fz_outline *outline) noexcept;
    fz_outline *loadOutlineFromFile(const QString &path) noexcept;

    // Resolve an outline node's chapter-aware fz_location to a single
    // global (0-based) page index. Every fz_outline consumer must go
    // through this rather than reading loc.page directly — for chaptered
    // formats (EPUB) loc.page is only the LOCAL page number within
    // loc.chapter, not the document-wide index. Falls back to loc.page
    // unresolved when m_doc is null (DjVu, which has no fz_document /
    // chapter concept), matching how generated/loaded synthetic outline
    // entries already encode a plain global index in that case.
    [[nodiscard]] inline int
    pageNumberFromLocation(fz_location loc) const noexcept
    {
        if (!m_ctx || !m_doc)
            return loc.page;
        return fz_page_number_from_location(m_ctx, m_doc, loc);
    }

    // Resolve an fz_outline node's actual target: document-wide (0-based)
    // page index, with the in-page x/y position written to *x/*y when
    // given. For most formats (PDF, XPS, FB2, MOBI) MuPDF already
    // resolves node->page/x/y while loading the outline. EPUB's loader
    // does NOT: it leaves node->page as the sentinel {-1, -1} and x/y
    // unset, expecting the caller to resolve the destination from
    // node->uri via fz_resolve_link() (this is how MuPDF's own reference
    // viewers, e.g. platform/gl, do it). This method handles both cases
    // uniformly so every consumer gets a correct global page index
    // regardless of format.
    [[nodiscard]] inline int
    resolveOutlineNode(fz_outline *node, float *x = nullptr,
                       float *y = nullptr) const noexcept
    {
        if (!m_ctx || !node)
            return -1;
        fz_location loc = node->page;
        float lx = node->x, ly = node->y;
        if (loc.chapter < 0 && m_doc && node->uri)
            loc = fz_resolve_link(m_ctx, m_doc, node->uri, &lx, &ly);
        if (x)
            *x = lx;
        if (y)
            *y = ly;
        return pageNumberFromLocation(loc);
    }

    // Re-paginate a reflowable document (EPUB/FB2/MOBI) to fit a page box
    // of the given size in points, e.g. following a viewport resize. No-op
    // for formats where supports_reflow() is false, or when (w, h, em)
    // match the last-applied layout (fz_layout_document + the page-count
    // recompute it forces is real work, proportional to chapter count —
    // don't repeat it for a no-change resize tick). Runs the MuPDF work
    // off the calling thread; emits documentRelayouted() once the new
    // page count / dimensions are live. See TODO.md for the known
    // limitation this introduces for PageLocation-based bookmarks/history.
    QFuture<void> relayoutForViewport(float widthPts, float heightPts,
                                      float emPts) noexcept;

    // Font family and line spacing (multiple of the font size) used for
    // reflowable documents; empty / 0 keep the document's own. Takes effect
    // at the next layout, so follow it with relayoutForViewport().
    void setReflowStyle(const QString &fontFamily, float lineSpacing) noexcept;

    // Starts indexing the system fonts in the background (platforms without
    // fontconfig), so a configured font_family is ready by the time a
    // document needs it. Safe to call repeatedly.
    static void prewarmFontIndex() noexcept;

    // Current reflow page-box size in points — the last size passed to
    // relayoutForViewport(), or MuPDF's own default (FZ_DEFAULT_LAYOUT_W/H,
    // 420x595) when the document hasn't been laid out yet. Font-size
    // commands should reuse these rather than deriving a size from the
    // viewport, so changing text size does not also change the page size.
    [[nodiscard]] inline float layoutWidthPts() const noexcept
    {
        return m_layout_w > 0.0f ? m_layout_w : float(FZ_DEFAULT_LAYOUT_W);
    }
    [[nodiscard]] inline float layoutHeightPts() const noexcept
    {
        return m_layout_h > 0.0f ? m_layout_h : float(FZ_DEFAULT_LAYOUT_H);
    }

    void cancelOpen() noexcept;
    QFuture<void> openAsync(const QString &filePath) noexcept;

    QFuture<void> openAsync_mupdf(const QString &canonicalPath) noexcept;
    QFuture<void> openAsync_djvu(const QString &canonicalPath) noexcept;
    QFuture<void> openAsync_image(const QString &canonicalPath) noexcept;
    void cleanup_image() noexcept;
    void _continueOpen(fz_context *ctx, fz_document *doc) noexcept;

    QFuture<void> submitPassword(const QString &password) noexcept;
    void close() noexcept;
    void cleanup_mupdf() noexcept;
    void cleanup_djvu() noexcept;
    bool decrypt() noexcept;
    bool encrypt(const EncryptInfo &info) noexcept;
    void setPopupColor(const QColor &color) noexcept;
    void setHighlightColor(const QColor &color) noexcept;
    // Colour of the underlines made from now on (default: the config's).
    void setUnderlineColor(const QColor &color) noexcept
    {
        m_underline_color = color;
    }
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
    // What text selection markup annotations are made of.
    enum class TextMarkup
    {
        Highlight,
        Underline
    };
    void highlight_text_selection(int pageno, QPointF start, QPointF end,
                                  const QString &comment = {},
                                  TextMarkup kind = TextMarkup::Highlight) noexcept;
    void invalidatePageCaches() noexcept;
    void invalidatePageCache(int pageno) noexcept;
    void search(const QString &term, bool caseSensitive = false,
                int pageFrom = -1, bool useRegex = false,
                int pageTo = -1) noexcept;
    void searchCancel() noexcept;
    void searchInPage(const int pageno, const QString &term,
                      bool caseSensitive = false) noexcept;
    std::vector<Model::SearchHit> searchHelper(int pageno, const QString &term,
                                               bool caseSensitive) noexcept;
    std::vector<Model::SearchHit>
    searchHelperRegex(int pageno, const QRegularExpression &re) noexcept;

    std::vector<HighlightText> collectHighlightTexts(bool groupByLine
                                                     = true) noexcept;
    bool exportTextHighlights(const QString &path) noexcept;

    std::vector<AnnotCommentInfo> collect_annot_comments() noexcept;
    void annotChangeColor(int pageno, int index, const QColor &color) noexcept;
    void removeAnnotComment(const int pageno, const int objNum) noexcept;
    void addAnnotComment(const int pageno, const int objNum,
                         const QString &comment) noexcept;
    QString getAnnotComment(const int pageno, const int objNum) noexcept;
    QColor getAnnotColor(const int pageno, const int index) noexcept;
    QString getHighlightText(const int pageno, const int objNum) noexcept;
    // The annotations of a page (PDF only, empty for other formats).
    std::vector<AnnotationInfo> annotationInfos(int pageno) noexcept;
    int get_obj_num_at_rect(int pageno, fz_rect targetRect) noexcept;
    QString fileTypeToString() const noexcept;
    [[nodiscard]] static QString fileTypeName(FileType type,
                                              const QString &filepath) noexcept;
    // Pure path->FileType lookup (extension/mime), usable without an open
    // Model instance — e.g. by link-following to decide whether a file://
    // URI points at something Lektra can open locally.
    [[nodiscard]] static FileType
    getFileTypeForPath(const QString &path) noexcept
    {
        return getFileType(path);
    }

signals:
    void undoStackCleanChanged(bool clean);
    void urlLinksReady(int pageno, std::vector<RenderLink> links);
    void passwordRequired();
    void wrongPassword();
    void reloadPasswordRequired();
    void openFileFailed();
    void openFileFinished();
    void documentRelayouted();
    void reloadRequested(int pageno);
    void
    searchResultsReady(const QMap<int, std::vector<Model::SearchHit>> &results);
    void searchPartialResultsReady(
        const QMap<int, std::vector<Model::SearchHit>> &results);

private:
    struct CachedLink
    {
        fz_rect rect; // page space
        QString uri;
        BrowseLinkItem::LinkType type;

        // optional extras
        int target_page = -1;
        fz_point target_loc; // It's target location
        fz_point source_loc; // It's own location
        float zoom = 0.0f;
    };

    struct CachedAnnotation
    {
        fz_rect rect; // for non-highlight annotations
        std::vector<fz_rect>
            quad_rects; // per-line rects for highlight annotations
        enum pdf_annot_type type;
        QColor color;
        QString text;
        int index;
        float opacity;
    };

    struct PageDimension
    {
        float width_pts = 0.0f, height_pts = 0.0f;
    };

    // Cache for page dimensions (W, H)
    struct PageDimensionCache
    {
        std::vector<PageDimension> dimensions;
        std::vector<bool> known;

        void reset(int page_count) noexcept
        {
            dimensions.assign(page_count, PageDimension{});
            known.assign(page_count, false);
        }

        [[nodiscard]] inline bool isKnown(int pageno) const noexcept
        {
            return pageno >= 0 && pageno < static_cast<int>(known.size())
                   && known[pageno] != false;
        }

        void set(int p, float w, float h)
        {
            if (p < 0 || p >= static_cast<int>(dimensions.size()))
                return;

            dimensions[p] = PageDimension{w, h};
            known[p]      = true;
        }

        [[nodiscard]] PageDimension
        getOrDefault(int p, const PageDimension &def) const noexcept
        {
            if (p < 0 || p >= static_cast<int>(dimensions.size()))
                return def;

            return known[p] ? dimensions[p] : def;
        }

        [[nodiscard]] PageDimension
        get(int p, const PageDimension &fallback) const noexcept
        {
            if (p < 0 || p >= static_cast<int>(dimensions.size()))
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

    // Tight bounding box of drawn content on a page (points, unrotated page
    // coordinates). "Smart" fit modes use this to fit the content region to
    // the viewport instead of the raw page rectangle, so blank margins get
    // pushed off-screen instead of consuming zoom. Non-PDF documents fall
    // back to the full page rect. Results are cached per page; invalidated
    // on document swap.
    struct ContentBBox
    {
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        [[nodiscard]] inline bool isEmpty() const noexcept
        {
            return x1 <= x0 || y1 <= y0;
        }
        [[nodiscard]] inline float width() const noexcept
        {
            return x1 - x0;
        }
        [[nodiscard]] inline float height() const noexcept
        {
            return y1 - y0;
        }
    };
    [[nodiscard]] ContentBBox contentBBox(int pageno) noexcept;

    // Topmost embedded raster image under a given point, if any — used to
    // let the user drag an image out of the document (e.g. onto a file
    // manager or another app) instead of just copying a rendered region.
    // logicalPt is in the same item-local logical-pixel space as
    // renderRegionAtDPI()'s logicalRect. The image is decoded at its native
    // resolution, independent of the current view zoom.
    struct ImageHit
    {
        bool valid = false;
        QRectF page_rect_pts;
        QImage image;
    };
    [[nodiscard]] ImageHit imageAt(int pageno, QPointF logicalPt) noexcept;

    struct PageCacheEntry
    {
        int pageno;
        fz_display_list *display_list = nullptr;
        fz_rect bounds;

        QImage cached_image; // for DJVU
        PageDimension dimension;
        // Shared and immutable once built, so a render takes them under the
        // cache lock by copying a pointer, not the vectors. Null when the page
        // has none.
        std::shared_ptr<const std::vector<CachedLink>> links;
        std::shared_ptr<const std::vector<CachedAnnotation>> annotations;
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
        fz_image *img = nullptr;
    };

    // Used for password handling
    struct PendingOpen
    {
        fz_context *ctx  = nullptr;
        fz_document *doc = nullptr;

        void clear() noexcept
        {
            ctx = nullptr;
            doc = nullptr;
        }
    };
    void initMuPDF() noexcept;

    [[nodiscard]] std::string getTextInPage(const int pageno,
                                            bool formatted) noexcept;
    [[nodiscard]] std::string getTextInArea(const int pageno, QPointF start,
                                            QPointF end) noexcept;
    [[nodiscard]] std::tuple<float, float>
    getPageDimensions(int pageno) const noexcept;

    QString m_filepath;
    QString m_cached_password;
    int m_page_count     = 0;
    int m_has_text_layer = -1; // -1 unknown, 0 no, 1 yes
    float m_dpr = 1.0f, m_dpi = 96.0f, m_zoom = 1.0f, m_rotation = 0.0f,
          m_inv_dpr     = 1.0f;
    bool m_invert_color = false;
    bool m_flip_h = false, m_flip_v = false;

    // private helper in Model
    [[nodiscard]] std::vector<QPolygonF> selectAtHelper(int pageno, fz_point pt,
                                                        int snapMode) noexcept;

    [[nodiscard]] std::pair<fz_matrix, fz_matrix>
    buildPageTransforms(int pageno) const noexcept;
    void buildPageCache(int pageno) noexcept;
    void buildPageCache_djvu(int pageno) noexcept;
    [[nodiscard]] int addRectAnnotation(const int pageno, const fz_rect &rect,
                                        const QString &content = {}) noexcept;
    [[nodiscard]] int
    addHighlightAnnotation(const int pageno, const std::vector<fz_quad> &quads,
                           const QColor &color    = QColor(),
                           const QString &content = {}) noexcept;
    // An underline whose look (thickness, position, style) comes from
    // annotations.underline: MuPDF only draws a fixed one, so the appearance
    // stream is written here.
    [[nodiscard]] int
    addUnderlineAnnotation(const int pageno, const std::vector<fz_quad> &quads,
                           const QColor &color    = QColor(),
                           const QString &content = {}) noexcept;
    void writeUnderlineAppearance(pdf_annot *annot) noexcept;
    [[nodiscard]] int addTextAnnotation(const int pageno, const fz_rect &rect,
                                        const QString &text) noexcept;
    void removeAnnotations(const int pageno,
                           const std::vector<int> &objNums) noexcept;
    void buildTextCacheForPages(const std::set<int> &pagenos) noexcept;
    [[nodiscard]] fz_stext_page *get_or_build_stext_page(fz_context *ctx,
                                                         int pageno) noexcept;
    void populatePDFProperties(Properties &props) noexcept;
    void populateCBZProperties(Properties &props) noexcept;
    void populateSignatureProperties(Properties &props) noexcept;
    [[nodiscard]] fz_point getFirstCharPos(const int pageno) noexcept;
    [[nodiscard]] std::vector<Model::RenderLink>
    detectUrlLinksForPage(const RenderJob &job) noexcept;
    [[nodiscard]] static FileType getFileType(const QString &filepath) noexcept;
    [[nodiscard]] bool reloadDocument() noexcept;
    void waitForPendingRenders() noexcept;
    [[nodiscard]] FileSize computeFileSize() noexcept;
    [[nodiscard]] QString fileSizeToString() const noexcept;
    QUndoStack *m_undo_stack = nullptr;
    QColor m_underline_color; // invalid: use annotations.underline.color
    // std::optional<std::wstring>
    // get_paper_name_at_position(const int pageno, const fz_point) noexcept;

    float m_popup_color[4]      = {1.0f, 1.0f, 0.8f, 0.8f},
          m_highlight_color[4]  = {1.0f, 1.0f, 0.0f, 0.5f},
          m_selection_color[4]  = {0.0f, 0.0f, 1.0f, 0.3f},
          m_annot_rect_color[4] = {1.0f, 0.0f, 0.0f, 0.5f};
    uint32_t m_bg_color         = 0;
    uint32_t m_fg_color         = 0;
    bool m_success              = false;

    mutable std::recursive_mutex m_page_cache_mutex;
    LRUCache<int, PageCacheEntry> m_page_lru_cache;
    LRUCache<int, CachedTextPage> m_text_cache;
    LRUCache<int, fz_stext_page *> m_stext_page_cache;

    PageDimensionCache m_page_dim_cache;
    mutable std::mutex m_page_dim_mutex;
    PageDimension m_default_page_dim;

    // Cache for contentBBox() so we only rasterize the bbox device once per
    // page. Cleared alongside the page-dim cache on document swap.
    std::unordered_map<int, ContentBBox> m_content_bbox_cache;
    mutable std::mutex m_content_bbox_mutex;

    pdf_write_options m_pdf_write_options = pdf_default_write_options;
    bool m_link_show_boundary             = false;
    bool m_detect_url_links               = false;
    FileType m_filetype                   = FileType::NONE;
    FileSize m_filesize                   = 0;

    mutable std::mutex m_doc_mutex;
    QFuture<void> m_search_future;
    QRegularExpression m_url_link_re;
    PendingOpen m_pending;

    // MuPDF core objects
    fz_locks_context m_fz_locks;
    fz_context *m_ctx               = nullptr;
    fz_document *m_doc              = nullptr;
    pdf_document *m_pdf_doc         = nullptr;
    fz_colorspace *m_colorspace     = nullptr;
    fz_outline *m_outline           = nullptr;
    fz_outline *m_generated_outline = nullptr;
    std::vector<OutlineEntry> m_outline_entries;
    fz_outline *m_outline_entries_src = nullptr; // what they were built from
    bool m_outline_entries_valid      = false;
    int m_outline_generation          = 0; // bumped when the entries go stale
    QFuture<void> m_outline_future;
    void invalidateOutlineEntries() noexcept
    {
        m_outline_entries.clear();
        m_outline_entries_src   = nullptr;
        m_outline_entries_valid = false;
        ++m_outline_generation;
    }
    void harvestOutline(fz_outline *node, int depth,
                        std::vector<OutlineEntry> &out) noexcept;

    // Last (widthPts, heightPts, emPts) applied via relayoutForViewport(),
    // so an unchanged viewport size doesn't trigger a redundant
    // fz_layout_document + page-count recompute. 0 means "never laid out"
    // (still at fz_open_document's default layout).
    float m_layout_w = 0.0f, m_layout_h = 0.0f, m_layout_em = 0.0f;
    // The stylesheet for reflowable documents (font, line spacing) from
    // setReflowStyle(), applied to a document when it is opened.
    QByteArray m_reflow_css;

    void *m_ddjvu_ctx = nullptr;
    void *m_ddjvu_doc = nullptr;

    // For use with visual line mode
    struct VisualLineInfo
    {
        QRectF bbox; // in page coordinates
        int pageno;
    };

    [[nodiscard]] std::vector<VisualLineInfo>
    get_text_lines(int pageno) noexcept;
    [[nodiscard]] int
    visual_line_index_at_pos(QPointF scenePos,
                             const std::vector<VisualLineInfo> &lines) noexcept;

    // Flat per-page character list (rune + quad, in page-point space), with
    // a synthetic '\n' entry (empty quad) marking each line break — the same
    // cache buildTextCacheForPages() maintains for search/mouse selection.
    // Used for caret mode's character-level keyboard navigation.
    [[nodiscard]] std::vector<CachedTextChar>
    textCharsForPage(int pageno) noexcept;

    friend class TextHighlightAnnotationCommand;
    friend class RectAnnotationCommand;
    friend class TextAnnotationCommand;
    friend class DeleteAnnotationsCommand;
    friend class DocumentView;

    // Dedicated pools so page renders never compete with other work in the
    // global pool, and so URL-link detection can't delay a visible page.
    QThreadPool m_render_pool;
    QThreadPool m_aux_pool;
    std::atomic<int> m_active_renders = 0;
    std::mutex m_renders_mutex;
    std::condition_variable m_renders_cv;

    std::atomic<bool> m_render_cancelled  = false;
    std::atomic<int> m_search_match_count = 0;
    std::atomic<bool> m_search_cancelled  = false;

    QImage m_image_cache;
    bool m_is_image         = false;
    bool m_is_animated      = false;
    ImageAnimation *m_movie = nullptr;

    [[nodiscard]] inline ImageAnimation *movie() const noexcept
    {
        return m_movie;
    }

    const Config &m_config;
};
