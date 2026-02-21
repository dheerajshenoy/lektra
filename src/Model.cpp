#include "Model.hpp"

#include "BrowseLinkItem.hpp"
#include "Commands/TextHighlightAnnotationCommand.hpp"
#include "utils.hpp"

#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <array>
#include <pthread.h>
#include <qbytearrayview.h>
#include <qregularexpression.h>
#include <qstyle.h>
#include <qtextformat.h>
#include <unordered_set>

static std::array<std::mutex, FZ_LOCK_MAX> mupdf_mutexes;

// This is called by Qt when the last copy of the QImage is destroyed
static void
imageCleanupHandler(void *info) noexcept
{
    Model::RenderPayload *payload = static_cast<Model::RenderPayload *>(info);
    if (payload)
    {
        // Drop the pixmap first, then the context
        fz_drop_pixmap(payload->ctx, payload->pix);
        fz_drop_context(payload->ctx);
        delete payload;
    }
}

static void
mupdf_lock_mutex(void *user, int lock)
{
    auto *m = static_cast<std::mutex *>(user);
    m[lock].lock();
}

static void
mupdf_unlock_mutex(void *user, int lock)
{
    auto *m = static_cast<std::mutex *>(user);
    m[lock].unlock();
}

Model::Model(QObject *parent) noexcept : QObject(parent)
{
    initMuPDF();
    m_undo_stack = new QUndoStack();
    setUrlLinkRegex(QString::fromUtf8(R"((https?://|www\.)[^\s<>()\"']+)"));

    // Eviction for LRU Cache
    m_page_lru_cache.setCallback([this](PageCacheEntry &entry)
    { LRUEvictFunction(entry); });
}

Model::~Model() noexcept
{
    cleanup();
    fz_drop_context(m_ctx);
}

void
Model::initMuPDF() noexcept
{
    // initialize each mutex
    m_fz_locks.user   = mupdf_mutexes.data();
    m_fz_locks.lock   = mupdf_lock_mutex;
    m_fz_locks.unlock = mupdf_unlock_mutex;
    m_ctx             = fz_new_context(nullptr, &m_fz_locks, FZ_STORE_DEFAULT);
    fz_register_document_handlers(m_ctx);
    m_colorspace = fz_device_rgb(m_ctx);
}

void
Model::LRUEvictFunction(PageCacheEntry &entry) noexcept
{
    // std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    if (entry.display_list)
    {
        fz_context *ctx = cloneContext(); // Use a clone to avoid racing m_ctx
        if (!ctx)
        {
            qWarning()
                << "LRUEvictFunction: failed to clone context for eviction";
            return;
        }

        fz_drop_display_list(ctx, entry.display_list);
        entry.display_list = nullptr;
        fz_drop_context(ctx);
    }

    // Also clear text cache for this page to save memory
    auto textIt = m_text_cache.has(entry.pageno);
    if (textIt)
    {
        m_text_cache.remove(textIt);
    }
}

void
Model::cleanup() noexcept
{
    m_render_future.cancel();

    fz_drop_outline(m_ctx, m_outline);
    m_outline = nullptr;
    fz_drop_document(m_ctx, m_doc);
    m_doc     = nullptr;
    m_pdf_doc = nullptr;

    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
        m_page_lru_cache.clear();
    }

    m_text_cache.clear();

    {
        std::lock_guard<std::mutex> lock(m_page_dim_mutex);
        m_page_dim_cache.reset(0);
        m_default_page_dim = {};
    }
}

QFuture<void>
Model::openAsync(const QString &filePath) noexcept
{
    m_filepath              = QFileInfo(filePath).canonicalFilePath();
    const QString canonPath = m_filepath;

    fz_context *bg_ctx = cloneContext();
    if (!bg_ctx)
    {
        QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                  Qt::QueuedConnection);
        return QtConcurrent::run([] {});
    }

    return QtConcurrent::run([this, canonPath, bg_ctx]
    {
        struct Guard
        {
            fz_context *ctx;
            fz_document *doc{nullptr};
            bool committed{false};
            ~Guard()
            {
                if (!committed)
                {
                    if (doc)
                        fz_drop_document(ctx, doc);
                    fz_drop_context(ctx);
                }
            }
        } g{bg_ctx};

        // --- detect type ---
        FileType filetype = FileType::NONE;
        fz_try(bg_ctx)
        {
            if (auto *h
                = fz_recognize_document_content(bg_ctx, CSTR(canonPath)))
            {
                if (h->extensions)
                {
                    const QString ext = QString::fromUtf8(h->extensions[0]);
                    if (ext == "pdf")
                        filetype = FileType::PDF;
                    else if (ext == "epub")
                        filetype = FileType::EPUB;
                    else if (ext == "cbt")
                        filetype = FileType::CBZ;
                    else if (ext == "svg")
                        filetype = FileType::SVG;
                }
            }
        }
        fz_catch(bg_ctx) {}

        // --- open ---
        fz_document *doc = nullptr;
        fz_try(bg_ctx)
        {
            doc = fz_open_document(bg_ctx, CSTR(canonPath));
        }
        fz_catch(bg_ctx) {}

        if (!doc)
        {
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }
        g.doc = doc;

        // --- encrypted? park and stop ---
        if (filetype == FileType::PDF && fz_needs_password(bg_ctx, doc))
        {
            g.committed = true;
            QMetaObject::invokeMethod(this, [this, bg_ctx, doc, filetype]
            {
                m_pending = {bg_ctx, doc, filetype};
                emit passwordRequired();
            }, Qt::QueuedConnection);
            return;
        }

        // --- normal path ---
        g.committed = true;
        _continueOpen(bg_ctx, doc, filetype);
    });
}

QFuture<void>
Model::submitPassword(const QString &password) noexcept
{
    auto ctx = m_pending.ctx;
    auto doc = m_pending.doc;
    auto ft  = m_pending.filetype;
    m_pending.clear();

    if (!ctx || !doc)
        return QtConcurrent::run([] {});

    return QtConcurrent::run([this, password, ctx, doc, ft]
    {
        if (!fz_authenticate_password(ctx, doc, CSTR(password)))
        {
            // Wrong password — put it back so the user can retry
            QMetaObject::invokeMethod(this, [this, ctx, doc, ft]
            {
                m_pending = {ctx, doc, ft};
                emit wrongPassword();
            }, Qt::QueuedConnection);
            return;
        }

        _continueOpen(ctx, doc, ft);
    });
}

void
Model::_continueOpen(fz_context *ctx, fz_document *doc,
                     FileType filetype) noexcept
{
    auto _ = QtConcurrent::run([this, ctx, doc, filetype]
    {
        struct Guard
        {
            fz_context *ctx;
            fz_document *doc;
            bool committed{false};
            ~Guard()
            {
                if (!committed)
                {
                    if (doc)
                        fz_drop_document(ctx, doc);
                    fz_drop_context(ctx);
                }
            }
        } g{ctx, doc};

        int page_count = 0;
        float w = 0, h = 0;

        fz_try(ctx)
        {
            page_count = fz_count_pages(ctx, doc);
            if (page_count > 0)
            {
                fz_page *p = fz_load_page(ctx, doc, 0);
                fz_rect r  = fz_bound_page(ctx, p);
                fz_drop_page(ctx, p);
                w = r.x1 - r.x0;
                h = r.y1 - r.y0;
            }
        }
        fz_catch(ctx)
        {
            QMetaObject::invokeMethod(this, &Model::openFileFailed,
                                      Qt::QueuedConnection);
            return;
        }

        g.committed = true;

        QMetaObject::invokeMethod(this,
                                  [this, ctx, doc, page_count, w, h, filetype]
        {
            waitForRenders();
            cleanup();

            fz_drop_context(m_ctx);

            m_ctx        = ctx;
            m_doc        = doc;
            m_pdf_doc    = pdf_specifics(m_ctx, m_doc);
            m_page_count = page_count;
            m_filetype   = filetype;
            m_success    = true;

            {
                std::lock_guard<std::mutex> lk(m_page_dim_mutex);
                m_default_page_dim = {w, h};
                m_page_dim_cache.dimensions.assign(page_count,
                                                   m_default_page_dim);
                m_page_dim_cache.known.assign(page_count, 0);
                if (page_count > 0)
                    m_page_dim_cache.known[0] = true;
            }

            emit openFileFinished();
        }, Qt::QueuedConnection);
    });
}

void
Model::close() noexcept
{
    m_filepath.clear();
    cleanup();
}

void
Model::clearPageCache() noexcept
{
    std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    // for (auto &[_, entry] : m_page_cache)
    //     fz_drop_display_list(m_ctx, entry.display_list);

    m_page_lru_cache.clear();
}

void
Model::ensurePageCached(int pageno) noexcept
{
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
        if (m_page_lru_cache.has(pageno))
            return;
    }

    // Not cached, build it
    // Build outside the lock — expensive, but safe
    buildPageCache(pageno);
}

void
Model::buildPageCache(int pageno) noexcept
{
    if (m_page_lru_cache.has(pageno))
        return;

    PageCacheEntry entry;

    fz_context *ctx = cloneContext();
    if (!ctx)
    {
        qWarning() << "Failed to clone context for page cache";
        return;
    }

    fz_page *page{nullptr};
    fz_display_list *dlist{nullptr};
    fz_device *list_dev{nullptr};
    fz_link *head{nullptr};
    fz_rect bounds{};
    bool success{false};

    // TODO: Pre-allocate vectors to avoid reallocations
    // entry.links.reserve(32);      // Typical PDF has ~10-20 links
    // entry.annotations.reserve(16); // Typical annotations per page

    fz_try(ctx)
    {
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        page = fz_load_page(ctx, m_doc, pageno);
        if (!page)
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");
        bounds = fz_bound_page(ctx, page);

        dlist    = fz_new_display_list(ctx, bounds);
        list_dev = fz_new_list_device(ctx, dlist);

        fz_run_page(ctx, page, list_dev, fz_identity, nullptr);
        fz_close_device(ctx, list_dev);

        {
            const float w = bounds.x1 - bounds.x0;
            const float h = bounds.y1 - bounds.y0;

            std::lock_guard<std::mutex> lock(m_page_dim_mutex);
            m_page_dim_cache.set(pageno, w, h);
        }

        // Extract links and cache them
        head = fz_load_links(ctx, page);
        for (fz_link *link = head; link; link = link->next)
        {
            if (!link->uri || !link->uri[0])
                continue;

            CachedLink cl;
            cl.rect = link->rect;
            cl.uri  = QString::fromUtf8(link->uri);

            // Store source location for all link types (where the link is
            // located)
            cl.source_loc.x = link->rect.x0;
            cl.source_loc.y = link->rect.y0;

            if (fz_is_external_link(ctx, link->uri))
            {
                cl.type = BrowseLinkItem::LinkType::External;
            }
            else if (cl.uri.startsWith("#page"))
            {
                float xp, yp;
                fz_location loc
                    = fz_resolve_link(ctx, m_doc, link->uri, &xp, &yp);
                cl.type        = BrowseLinkItem::LinkType::Page;
                cl.target_page = loc.page;
            }
            else
            {
                fz_link_dest dest = fz_resolve_link_dest(ctx, m_doc, link->uri);
                cl.type           = BrowseLinkItem::LinkType::Location;
                cl.target_page    = dest.loc.page;
                cl.target_loc.x   = dest.x;
                cl.target_loc.y   = dest.y;
                cl.zoom           = dest.zoom;
            }

            entry.links.push_back(std::move(cl));
        }

        pdf_page *pdfPage = pdf_page_from_fz_page(ctx, page);
        if (pdfPage)
        {
            float color[3]{0.0f, 0.0f, 0.0f};
            int n = 3;

            for (pdf_annot *annot = pdf_first_annot(ctx, pdfPage); annot;
                 annot            = pdf_next_annot(ctx, annot))
            {
                CachedAnnotation ca;
                ca.rect = pdf_bound_annot(ctx, annot);
                if (fz_is_infinite_rect(ca.rect) || fz_is_empty_rect(ca.rect))
                    continue;

                ca.type = pdf_annot_type(ctx, annot);

                // Only get text for annotations that typically have it
                if (ca.type == PDF_ANNOT_TEXT || ca.type == PDF_ANNOT_POPUP)
                {
                    const char *contents = pdf_annot_contents(ctx, annot);
                    if (contents)
                        ca.text = QString::fromUtf8(contents);
                }

                ca.index   = pdf_to_num(ctx, pdf_annot_obj(ctx, annot));
                ca.opacity = pdf_annot_opacity(ctx, annot);

                switch (ca.type)
                {
                    case PDF_ANNOT_POPUP:
                    case PDF_ANNOT_TEXT:
                    case PDF_ANNOT_HIGHLIGHT:
                    {
                        pdf_annot_color(ctx, annot, &n, color);
                        ca.color = QColor::fromRgbF(color[0], color[1],
                                                    color[2], ca.opacity);
                    }
                    break;

                    case PDF_ANNOT_SQUARE:
                    {
                        pdf_annot_interior_color(ctx, annot, &n, color);
                        ca.color = QColor::fromRgbF(color[0], color[1],
                                                    color[2], ca.opacity);
                    }
                    break;

                    default:
                        continue;
                }

                entry.annotations.push_back(std::move(ca));
            }
        }

        entry.display_list = dlist;
        entry.bounds       = bounds;
        entry.pageno       = pageno;
        success            = true;
    }
    fz_always(ctx)
    {
        fz_drop_link(ctx, head);
        fz_drop_device(ctx, list_dev);
        fz_drop_page(ctx, page);
        if (!success && dlist)
            fz_drop_display_list(ctx, dlist);
    }
    fz_catch(ctx)
    {
        qWarning() << "Failed to build page cache for page" << pageno << ":"
                   << fz_caught_message(ctx);
        fz_drop_context(ctx);
        return;
    }

    if (!success)
    {
        fz_drop_context(ctx);
        return;
    }

    // Cache the display list and links
    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
        if (!m_page_lru_cache.has(pageno))
        {
            m_page_lru_cache.put(pageno, std::move(entry));
        }
        // else
        //     fz_drop_display_list(ctx, dlist);
    }
}

void
Model::setPopupColor(const QColor &color) noexcept
{
    m_popup_color[0] = color.redF();
    m_popup_color[1] = color.greenF();
    m_popup_color[2] = color.blueF();
    m_popup_color[3] = color.alphaF();
}

void
Model::setHighlightColor(const QColor &color) noexcept
{
    m_highlight_color[0] = color.redF();
    m_highlight_color[1] = color.greenF();
    m_highlight_color[2] = color.blueF();
    m_highlight_color[3] = color.alphaF();
}

void
Model::setSelectionColor(const QColor &color) noexcept
{
    m_selection_color[0] = color.redF();
    m_selection_color[1] = color.greenF();
    m_selection_color[2] = color.blueF();
    m_selection_color[3] = color.alphaF();
}

void
Model::setAnnotRectColor(const QColor &color) noexcept
{
    m_annot_rect_color[0] = color.redF();
    m_annot_rect_color[1] = color.greenF();
    m_annot_rect_color[2] = color.blueF();
    m_annot_rect_color[3] = color.alphaF();
}

bool
Model::decrypt() noexcept
{
    // Use MuPDF to decrypt the PDF
    fz_try(m_ctx)
    {
        pdf_write_options opts = m_pdf_write_options;
        opts.do_encrypt        = PDF_ENCRYPT_NONE;

        if (m_pdf_doc)
            pdf_save_document(m_ctx, m_pdf_doc, CSTR(m_filepath), &opts);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Cannot decrypt file: " << fz_caught_message(m_ctx);
        return false;
    }
    return true;
}

bool
Model::encrypt(const EncryptInfo &info) noexcept
{
    if (!m_doc || !m_pdf_doc)
        return false;

    fz_try(m_ctx)
    {

        pdf_write_options opts = m_pdf_write_options;
        opts.do_encrypt        = PDF_ENCRYPT_AES_256;

        QByteArray userPwdBytes = info.user_password.toUtf8();
        strncpy(opts.upwd_utf8, userPwdBytes.constData(),
                sizeof(opts.upwd_utf8) - 1);

        // Set owner password (required for full access/editing)
        // QByteArray ownerPwdBytes = password.toUtf8();
        strncpy(opts.opwd_utf8, userPwdBytes.constData(),
                sizeof(opts.opwd_utf8) - 1);

        opts.permissions = PDF_PERM_PRINT | PDF_PERM_COPY | PDF_PERM_ANNOTATE
                           | PDF_PERM_FORM | PDF_PERM_MODIFY | PDF_PERM_ASSEMBLE
                           | PDF_PERM_PRINT_HQ;

        m_pdf_write_options = opts;
        SaveChanges();
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Encryption failed:" << fz_caught_message(m_ctx);
        return false;
    }

    return true;
}

bool
Model::reloadDocument() noexcept
{
    const QString filepath = m_filepath;
    if (filepath.isEmpty())
        return false;

    waitForRenders();

    // Lock to prevent concurrent access
    std::lock_guard<std::mutex> lock(m_doc_mutex);

    if (!m_ctx)
        initMuPDF();

    cleanup();
    m_page_count = 0;
    m_success    = false;

    bool ok = false;
    fz_try(m_ctx)
    {
        m_doc = fz_open_document(m_ctx, CSTR(filepath));
        if (!m_doc)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to open document");

        m_pdf_doc    = pdf_specifics(m_ctx, m_doc);
        m_page_count = fz_count_pages(m_ctx, m_doc);
        ok           = true;
    }
    fz_catch(m_ctx)
    {
        ok = false;
    }

    m_success = ok;
    return ok;
}

bool
Model::SaveChanges() noexcept
{
    if (!m_doc || !m_pdf_doc)
        return false;

    const std::string path = m_filepath.toStdString();

    fz_try(m_ctx)
    {
        // MUST clear text pages; they hold page references!
        pdf_save_document(m_ctx, m_pdf_doc, path.c_str(), &m_pdf_write_options);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Cannot save file: " << fz_caught_message(m_ctx);
        return false;
    }

    return true;
}

bool
Model::SaveAs(const QString &newFilePath) noexcept
{
    if (!m_doc || !m_pdf_doc)
        return false;

    fz_try(m_ctx)
    {
        pdf_save_document(m_ctx, m_pdf_doc, CSTR(newFilePath),
                          nullptr); // TODO: options for saving
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Save As failed: " << fz_caught_message(m_ctx);
        return false;
    }
    return true;
}

fz_outline *
Model::getOutline() noexcept
{
    if (!m_doc)
        return nullptr;
    if (!m_outline)
        m_outline = fz_load_outline(m_ctx, m_doc);
    return m_outline;
}

std::vector<QPolygonF>
Model::computeTextSelectionQuad(int pageno, const QPointF &devStart,
                                const QPointF &devEnd) noexcept
{
    std::vector<QPolygonF> out;

    constexpr int MAX_HITS = 1024;
    thread_local std::array<fz_quad, MAX_HITS> hits;

    const float scale = logicalScale();

    fz_stext_page *stext_page{nullptr};
    fz_page *page{nullptr};
    fz_rect page_bounds{};
    fz_matrix page_to_dev{};
    int count{0};

    fz_try(m_ctx)
    {
        // Single page load — get bounds AND build stext in one shot
        page        = fz_load_page(m_ctx, m_doc, pageno);
        page_bounds = fz_bound_page(m_ctx, page);

        // Build page -> device transform
        page_to_dev = fz_scale(scale, scale);
        page_to_dev = fz_pre_rotate(page_to_dev, m_rotation);

        const fz_rect dev_bounds = fz_transform_rect(page_bounds, page_to_dev);
        page_to_dev              = fz_concat(page_to_dev,
                                             fz_translate(-dev_bounds.x0, -dev_bounds.y0));

        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

        fz_point a = {float(devStart.x()), float(devStart.y())};
        fz_point b = {float(devEnd.x()), float(devEnd.y())};

        a = fz_transform_point(a, dev_to_page);
        b = fz_transform_point(b, dev_to_page);

        m_selection_start = a;
        m_selection_end   = b;

        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

        fz_snap_selection(m_ctx, stext_page, &a, &b, FZ_SELECT_CHARS);

        // Re-store snapped endpoints so callers get the corrected range
        m_selection_start = a;
        m_selection_end   = b;

        count = fz_highlight_selection(m_ctx, stext_page, a, b, hits.data(),
                                       MAX_HITS);
    }
    fz_always(m_ctx)
    {
        fz_drop_stext_page(m_ctx, stext_page);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Selection failed:" << fz_caught_message(m_ctx);
        return out;
    }

    out.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const fz_quad &q = hits[i];

        auto toDev = [&](const fz_point &p0) -> QPointF
        {
            const fz_point p = fz_transform_point(p0, page_to_dev);
            return {p.x, p.y};
        };

        QPolygonF poly;
        poly.reserve(4);
        poly << toDev(q.ll) << toDev(q.lr) << toDev(q.ur) << toDev(q.ul);
        out.push_back(std::move(poly));
    }

    return out;
}

std::string
Model::getSelectedText(int pageno, const fz_point &a, const fz_point &b,
                       bool formatted) noexcept
{
    std::string result;
    fz_page *page{nullptr};
    char *selection_text{nullptr};
    fz_stext_page *stext_page{nullptr};

    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, pageno);

        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);

        selection_text = fz_copy_selection(m_ctx, stext_page, a, b, 0);
    }
    fz_always(m_ctx)
    {
        if (selection_text)
        {
            result = std::string(selection_text);
            fz_free(m_ctx, selection_text);
        }
        fz_drop_page(m_ctx, page);
        fz_drop_stext_page(m_ctx, stext_page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Failed to copy selection text";
    }

    if (!formatted)
        clean_pdf_text(result);

    return result;
}

std::vector<std::pair<QString, QString>>
Model::properties() noexcept
{
    std::vector<std::pair<QString, QString>> props;
    props.reserve(16); // Typical number of PDF properties

    if (!m_ctx || !m_doc)
        return props;

    props.push_back(qMakePair("File Path", m_filepath));
    props.push_back(
        qMakePair("Encrypted", fz_needs_password(m_ctx, m_doc) ? "Yes" : "No"));
    props.push_back(qMakePair("Page Count", QString::number(m_page_count)));

    if (m_pdf_doc)
        populatePDFProperties(props);

    return props;
}

void
Model::populatePDFProperties(
    std::vector<std::pair<QString, QString>> &props) noexcept
{
    // ========== Info Dictionary ==========
    pdf_obj *info
        = pdf_dict_get(m_ctx, pdf_trailer(m_ctx, m_pdf_doc), PDF_NAME(Info));
    if (info && pdf_is_dict(m_ctx, info))
    {
        int len = pdf_dict_len(m_ctx, info);
        for (int i = 0; i < len; ++i)
        {
            pdf_obj *keyObj = pdf_dict_get_key(m_ctx, info, i);
            pdf_obj *valObj = pdf_dict_get_val(m_ctx, info, i);

            if (!pdf_is_name(m_ctx, keyObj))
                continue;

            QString key = QString::fromLatin1(pdf_to_name(m_ctx, keyObj));
            QString val;

            if (pdf_is_string(m_ctx, valObj))
            {
                const char *s = pdf_to_str_buf(m_ctx, valObj);
                int slen      = pdf_to_str_len(m_ctx, valObj);

                if (slen >= 2 && (quint8)s[0] == 0xFE && (quint8)s[1] == 0xFF)
                {
                    QStringDecoder decoder(QStringDecoder::Utf16BE);
                    val = decoder(QByteArray(s + 2, slen - 2));
                }
                else
                {
                    val = QString::fromUtf8(s, slen);
                }
            }
            else if (pdf_is_int(m_ctx, valObj))
                val = QString::number(pdf_to_int(m_ctx, valObj));
            else if (pdf_is_bool(m_ctx, valObj))
                val = pdf_to_bool(m_ctx, valObj) ? "true" : "false";
            else if (pdf_is_name(m_ctx, valObj))
                val = QString::fromLatin1(pdf_to_name(m_ctx, valObj));
            else
                val = QStringLiteral("[Non-string value]");

            props.push_back({key, val});
        }
    }

    // ========== Add Derived Properties ==========
    props.push_back(
        qMakePair("PDF Version", QString("%1.%2")
                                     .arg(m_pdf_doc->version / 10)
                                     .arg(m_pdf_doc->version % 10)));
}

// Returns page dimensions in points (1/72 inch) if known, otherwise (-1, -1)
std::tuple<float, float>
Model::getPageDimensions(int pageno) const noexcept
{
    std::tuple<float, float> dims{-1.0f, -1.0f};
    std::lock_guard<std::mutex> lock(m_page_dim_mutex);
    if (pageno >= 0 && pageno < m_page_count && m_page_dim_cache.known[pageno])
    {
        dims = {m_page_dim_cache.dimensions[pageno].width_pts,
                m_page_dim_cache.dimensions[pageno].height_pts};
        return dims;
    }
    return dims;
}

fz_point
Model::toPDFSpace(int pageno, QPointF pixelPos) const noexcept
{
    fz_point p{0, 0};

    const auto [width_pts, height_pts] = getPageDimensions(pageno);

    // Create bounds rect from cached dimensions
    fz_rect bounds = {0, 0, width_pts, height_pts};

    // Re-create the same transform used in rendering
    const float scale   = m_zoom * m_dpr * m_dpi;
    fz_matrix transform = fz_transform_page(bounds, scale, m_rotation);

    // Get the bbox (to find the origin shift)
    fz_rect transformed = fz_transform_rect(bounds, transform);
    fz_irect bbox       = fz_round_rect(transformed);

    // Adjust for Qt's Device Pixel Ratio and add bbox origin
    float physicalX = pixelPos.x() * m_dpr;
    float physicalY = pixelPos.y() * m_dpr;

    p.x = physicalX + bbox.x0;
    p.y = physicalY + bbox.y0;

    // Invert transformation to get PDF space coordinates
    fz_matrix inv_transform = fz_invert_matrix(transform);
    p                       = fz_transform_point(p, inv_transform);

    return p;
}

QPointF
Model::toPixelSpace(int pageno, fz_point p) const noexcept
{
    // Get cached page dimensions instead of loading the page
    const auto [width_pts, height_pts] = getPageDimensions(pageno);

    // Create bounds rect from cached dimensions
    fz_rect bounds = {0, 0, width_pts, height_pts};

    // Re-create the same transform used in rendering
    const float scale   = m_zoom * m_dpr * m_dpi;
    fz_matrix transform = fz_transform_page(bounds, scale, m_rotation);

    // Get the bbox (this is the key!)
    fz_rect transformed = fz_transform_rect(bounds, transform);
    fz_irect bbox       = fz_round_rect(transformed);

    // Transform point to device space and subtract bbox origin
    fz_point device_point = fz_transform_point(p, transform);
    float localX          = device_point.x - bbox.x0;
    float localY          = device_point.y - bbox.y0;

    // Adjust for Qt's Device Pixel Ratio
    return QPointF(localX / m_dpr, localY / m_dpr);
}

Model::RenderJob
Model::createRenderJob(int pageno) const noexcept
{
    RenderJob job;
    job.filepath = m_filepath;
    job.pageno   = pageno;
    job.dpr      = m_dpr;
    job.dpi      = m_dpi;
    job.zoom = m_zoom * m_dpr * m_dpi; // DPI resolution for fz_transform_page
                                       // (divides by 72 internally)
    job.rotation     = m_rotation;
    job.invert_color = m_invert_color;
    job.colorspace   = m_colorspace;
    return job;
}

void
Model::requestPageRender(
    const RenderJob &job,
    const std::function<void(PageRenderResult)> &callback) noexcept
{
    m_render_future
        = QtConcurrent::run([this, job, callback]() -> PageRenderResult
    {
        ensurePageCached(job.pageno);
        return renderPageWithExtrasAsync(job);
    });

    auto watcher = new QFutureWatcher<PageRenderResult>(this);
    connect(watcher, &QFutureWatcher<PageRenderResult>::finished, this,
            [this, watcher, callback, job]()
    {
        PageRenderResult result = watcher->result();
        watcher->deleteLater();

        // Deliver pixels + PDF links immediately — this fires GotoLocation
        if (callback)
            callback(result);

        // URL detection runs as a fire-and-forget second pass
        // It doesn't block the jump marker or page display at all
        if (m_detect_url_links)
        {
            const int pageno = job.pageno;
            QFuture<void> _  = QtConcurrent::run([this, job, pageno]()
            {
                auto urlLinks = detectUrlLinksForPage(job);
                if (!urlLinks.empty())
                    emit urlLinksReady(pageno, std::move(urlLinks));
            });
        }
    });

    watcher->setFuture(m_render_future);
}

Model::PageRenderResult
Model::renderPageWithExtrasAsync(const RenderJob &job) noexcept
{
    PageRenderResult result;
    fz_context *ctx{nullptr};

    ctx = cloneContext();
    if (!ctx)
        return result;

    // Copy cache entry data under lock to avoid race condition with
    // invalidatePageCache. We keep a reference to the display list so it
    // won't be freed while we're using it.
    fz_display_list *dlist{nullptr};
    fz_rect bounds{};
    std::vector<CachedLink> links;
    std::vector<CachedAnnotation> annotations;

    {
        std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);

        if (!m_page_lru_cache.has(job.pageno))
        {
            qWarning() << "Model::PageRenderResult() Page not cached:"
                       << job.pageno;
            fz_drop_context(ctx);
            return result;
        }

        const PageCacheEntry *entry = m_page_lru_cache.get(job.pageno);
        if (!entry->display_list)
        {
            qWarning() << "Model::PageRenderResult() Missing display list for:"
                       << job.pageno;
            fz_drop_context(ctx);
            return result;
        }

        // Increment reference count so the display list stays valid
        dlist  = fz_keep_display_list(ctx, entry->display_list);
        bounds = entry->bounds;

        links.reserve(entry->links.size());
        links = entry->links;
        annotations.reserve(entry->annotations.size());
        annotations = entry->annotations;
    }

    fz_link *head{nullptr};
    fz_pixmap *pix{nullptr};
    fz_device *dev{nullptr};
    fz_page *text_page{nullptr};
    fz_stext_page *stext_page{nullptr};

    fz_try(ctx)
    {
        fz_matrix transform = fz_transform_page(bounds, job.zoom, job.rotation);
        fz_rect transformed = fz_transform_rect(bounds, transform);
        fz_irect bbox       = fz_round_rect(transformed);

        // // --- Render page to QImage ---
        pix = fz_new_pixmap_with_bbox(ctx, job.colorspace, bbox, nullptr, 1);

        dev = fz_new_draw_device(ctx, fz_identity, pix);

        fz_clear_pixmap_with_value(ctx, pix, 255);
        fz_run_display_list(ctx, dlist, dev, transform,
                            fz_rect_from_irect(bbox), nullptr);

        const int fg = (m_fg_color >> 8) & 0xFFFFFF;
        const int bg = (m_bg_color >> 8) & 0xFFFFFF;

        if (fg != 0 || bg != 0)
            fz_tint_pixmap(ctx, pix, fg, bg);

        if (job.invert_color)
            fz_invert_pixmap_luminance(ctx, pix);

        // fz_gamma_pixmap(ctx, pix, 1.0f);

        const int width  = fz_pixmap_width(ctx, pix);
        const int height = fz_pixmap_height(ctx, pix);
        const int n      = fz_pixmap_components(ctx, pix);
        const int stride = fz_pixmap_stride(ctx, pix);

        unsigned char *samples = fz_pixmap_samples(ctx, pix);
        if (!samples)
            return result;

        QImage::Format fmt;
        switch (n)
        {
            case 1:
                fmt = QImage::Format_Grayscale8;
                break;
            case 3:
                fmt = QImage::Format_RGB888;
                break;
            case 4:
                fmt = QImage::Format_RGBA8888;
                break;

            default:
            {
                qWarning() << "Unsupported pixmap component count:" << n;
                return result;
            }
        }

        RenderPayload *payload = new RenderPayload{ctx, pix};

        QImage image = QImage(samples, width, height, stride, fmt,
                              imageCleanupHandler, payload);
        image.setDotsPerMeterX(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDotsPerMeterY(static_cast<int>((job.dpi * 1000) / 25.4));
        image.setDevicePixelRatio(job.dpr);
        result.image = image;

        // --- Extract links ---
        for (const auto &link : links)
        {
            if (link.uri.isEmpty())
                continue;
            fz_rect r         = fz_transform_rect(link.rect, transform);
            const float scale = m_inv_dpr;
            QRectF qtRect(r.x0 * scale, r.y0 * scale, (r.x1 - r.x0) * scale,
                          (r.y1 - r.y0) * scale);

            RenderLink renderLink;
            renderLink.rect       = qtRect;
            renderLink.uri        = link.uri;
            renderLink.type       = link.type;
            renderLink.boundary   = m_link_show_boundary;
            renderLink.source_loc = BrowseLinkItem::PageLocation{
                link.source_loc.x, link.source_loc.y, 0.0f};

            if (link.type == BrowseLinkItem::LinkType::Page)
            {
                renderLink.target_page = link.target_page;
            }

            if (link.type == BrowseLinkItem::LinkType::Location)
            {
                renderLink.target_page = link.target_page;
                renderLink.target_loc  = BrowseLinkItem::PageLocation{
                    link.target_loc.x, link.target_loc.y, link.zoom};
            }

            result.links.push_back(std::move(renderLink));
        }

        // if (m_detect_url_links)
        // {
        //     text_page = fz_load_page(ctx, m_doc, job.pageno);
        //     if (text_page)
        //         stext_page
        //             = fz_new_stext_page_from_page(ctx, text_page,
        //             nullptr);
        //
        //     if (stext_page)
        //     {
        //         const QRegularExpression &urlRe = m_url_link_re;
        //
        //         auto hasIntersectingLink = [&](const fz_rect &r) -> bool
        //         {
        //             for (const auto &link : links)
        //             {
        //                 const fz_rect lr = link.rect;
        //                 if (r.x1 < lr.x0 || r.x0 > lr.x1 || r.y1 < lr.y0
        //                     || r.y0 > lr.y1)
        //                     continue;
        //                 return true;
        //             }
        //             return false;
        //         };
        //
        //         for (fz_stext_block *b = stext_page->first_block; b;
        //              b                 = b->next)
        //         {
        //             if (b->type != FZ_STEXT_BLOCK_TEXT)
        //                 continue;
        //
        //             for (fz_stext_line *line = b->u.t.first_line; line;
        //                  line                = line->next)
        //             {
        //                 QString lineText;
        //                 lineText.reserve(256);
        //                 for (fz_stext_char *ch = line->first_char; ch;
        //                      ch                = ch->next)
        //                 {
        //                     lineText.append(QChar::fromUcs4(ch->c));
        //                 }
        //
        //                 if (lineText.isEmpty())
        //                     continue;
        //
        //                 QRegularExpressionMatchIterator it
        //                     = urlRe.globalMatch(lineText);
        //                 while (it.hasNext())
        //                 {
        //                     QRegularExpressionMatch match = it.next();
        //                     int start = match.capturedStart();
        //                     int len   = match.capturedLength();
        //                     if (start < 0 || len <= 0)
        //                         continue;
        //
        //                     QString raw = match.captured();
        //                     while (
        //                         !raw.isEmpty()
        //                         &&
        //                         QString(".,;:!?)\"'").contains(raw.back()))
        //                     {
        //                         raw.chop(1);
        //                         --len;
        //                     }
        //
        //                     if (raw.isEmpty() || len <= 0)
        //                         continue;
        //
        //                     fz_quad q = getQuadForSubstring(line, start,
        //                     len); fz_rect r = fz_rect_from_quad(q); if
        //                     (fz_is_empty_rect(r))
        //                         continue;
        //
        //                     if (hasIntersectingLink(r))
        //                         continue;
        //
        //                     QString uri = raw;
        //                     if (uri.startsWith("www."))
        //                         uri.prepend("https://");
        //
        //                     fz_rect tr        = fz_transform_rect(r,
        //                     transform); const float scale = m_inv_dpr;
        //                     QRectF qtRect(tr.x0 * scale, tr.y0 * scale,
        //                                   (tr.x1 - tr.x0) * scale,
        //                                   (tr.y1 - tr.y0) * scale);
        //
        //                     RenderLink renderLink;
        //                     renderLink.rect = qtRect;
        //                     renderLink.uri  = uri;
        //                     renderLink.type
        //                         = BrowseLinkItem::LinkType::External;
        //                     renderLink.boundary = m_link_show_boundary;
        //                     result.links.push_back(std::move(renderLink));
        //                 }
        //             }
        //         }
        //     }
        // }

        for (const auto &annot : annotations)
        {
            RenderAnnotation renderAnnot;

            fz_rect r         = fz_transform_rect(annot.rect, transform);
            const float scale = m_inv_dpr;
            QRectF qtRect(r.x0 * scale, r.y0 * scale, (r.x1 - r.x0) * scale,
                          (r.y1 - r.y0) * scale);
            renderAnnot.rect  = qtRect;
            renderAnnot.type  = annot.type;
            renderAnnot.index = annot.index;
            renderAnnot.color = annot.color;
            renderAnnot.text  = annot.text;
            result.annotations.push_back(std::move(renderAnnot));
        }
    }
    fz_always(ctx)
    {
        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);
        fz_drop_link(ctx, head);
        fz_drop_display_list(ctx, dlist);
        if (stext_page)
            fz_drop_stext_page(ctx, stext_page);
        if (text_page)
            fz_drop_page(ctx, text_page);
    }
    fz_catch(ctx)
    {
        qWarning() << "MuPDF error in thread:" << fz_caught_message(ctx);
        fz_drop_pixmap(ctx, pix);
    }
    // result.image = result.image.convertToFormat(
    //         QImage::Format_ARGB32_Premultiplied); // Optimize for
    //         rendering in
    //                                               // QGraphicsView

    return result;
}

void
Model::highlightTextSelection(int pageno, const QPointF &start,
                              const QPointF &end) noexcept
{
    fz_page *page{nullptr};

    constexpr int MAX_HITS = 1000;
    fz_quad hits[MAX_HITS];
    int count = 0;
    fz_stext_page *stext_page{nullptr};

    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, pageno);

        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);

        fz_point a, b;
        a     = {static_cast<float>(start.x()), static_cast<float>(start.y())};
        b     = {static_cast<float>(end.x()), static_cast<float>(end.y())};
        count = fz_highlight_selection(m_ctx, stext_page, a, b, hits, MAX_HITS);
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
        fz_drop_stext_page(m_ctx, stext_page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Failed to copy selection text";
    }

    // Collect quads for the command
    std::vector<fz_quad> quads;
    quads.reserve(count);
    for (int i = 0; i < count; ++i)
        quads.push_back(hits[i]);

    // // Create and push the command onto the undo stack for undo/redo
    // support
    m_undo_stack->push(
        new TextHighlightAnnotationCommand(this, pageno, std::move(quads)));
}

int
Model::addHighlightAnnotation(const int pageno,
                              const std::vector<fz_quad> &quads) noexcept
{
    int objNum{-1};

    fz_try(m_ctx)
    {
        // Load the specific page for this annotation
        pdf_page *page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        // Create a separate highlight annotation for each quad
        // This looks better visually for multi-line selections
        pdf_annot *annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_HIGHLIGHT);
        if (!annot)
            return objNum;

        if (quads.empty())
            return objNum;

        pdf_set_annot_quad_points(m_ctx, annot, quads.size(), &quads[0]);
        pdf_set_annot_color(m_ctx, annot, 3, m_highlight_color);
        pdf_set_annot_opacity(m_ctx, annot, m_highlight_color[3]);
        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        // Store the object number for later undo
        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (obj)
            objNum = pdf_to_num(m_ctx, obj);

        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);

        {
            std::lock_guard<std::recursive_mutex> cache_lock(
                m_page_cache_mutex);
            if (m_page_lru_cache.has(pageno))
            {
                // fz_drop_display_list(m_ctx,
                // m_page_cache[pageno].display_list);
                // m_page_cache.erase(pageno);
                m_page_lru_cache.remove(pageno);
            }
        }
        // Build cache outside the lock to avoid holding it during expensive
        // operations
        buildPageCache(pageno);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Redo failed:" << fz_caught_message(m_ctx);
    }

#ifndef NDEBUG
    qDebug() << "Adding highlight annotation on page" << pageno
             << " Quad count:" << quads.size() << " ObjNum:" << objNum;
#endif
    return objNum;
}

int
Model::addRectAnnotation(const int pageno, const fz_rect &rect) noexcept
{
    int objNum{-1};

    fz_try(m_ctx)
    {
        // Load the specific page for this annotation
        pdf_page *page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        pdf_annot *annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_SQUARE);

        if (!annot)
            return objNum;

        pdf_set_annot_rect(m_ctx, annot, rect);
        pdf_set_annot_interior_color(m_ctx, annot, 3, m_annot_rect_color);
        pdf_set_annot_color(m_ctx, annot, 3, m_annot_rect_color);
        pdf_set_annot_opacity(m_ctx, annot, m_annot_rect_color[3]);
        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        // Store the object number for later undo
        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (obj)
            objNum = pdf_to_num(m_ctx, obj);

        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);

        {
            std::lock_guard<std::recursive_mutex> cache_lock(
                m_page_cache_mutex);
            if (m_page_lru_cache.has(pageno))
            {
                m_page_lru_cache.remove(pageno);
                // fz_drop_display_list(m_ctx,
                // m_page_cache[pageno].display_list);
                // m_page_cache.erase(pageno);
            }
        }
        // Build cache outside the lock to avoid holding it during expensive
        // operations
        buildPageCache(pageno);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Redo failed:" << fz_caught_message(m_ctx);
    }

#ifndef NDEBUG
    qDebug() << "Adding rect annotation on page" << pageno
             << " ObjNum:" << objNum;
#endif
    return objNum;
}

int
Model::addTextAnnotation(const int pageno, const fz_rect &rect,
                         const QString &text) noexcept
{
    int objNum{-1};

    fz_try(m_ctx)
    {
        // Load the specific page for this annotation
        pdf_page *page = pdf_load_page(m_ctx, m_pdf_doc, pageno);

        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        // Create a text (sticky note) annotation
        pdf_annot *annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_TEXT);

        if (!annot)
            return objNum;

        pdf_set_annot_rect(m_ctx, annot, rect);
        pdf_set_annot_color(m_ctx, annot, 3, m_popup_color);
        pdf_set_annot_opacity(m_ctx, annot, m_popup_color[3]);

        // Set the annotation contents (the text that appears in the popup)
        if (!text.isEmpty())
        {
            pdf_set_annot_contents(m_ctx, annot, text.toUtf8().constData());
        }

        // Set the annotation to be open by default (optional)
        // pdf_set_annot_is_open(m_ctx, annot, 0);

        pdf_update_annot(m_ctx, annot);
        pdf_update_page(m_ctx, page);

        // Store the object number for later undo
        pdf_obj *obj = pdf_annot_obj(m_ctx, annot);
        if (obj)
            objNum = pdf_to_num(m_ctx, obj);

        pdf_drop_annot(m_ctx, annot);
        pdf_drop_page(m_ctx, page);

        {
            std::lock_guard<std::recursive_mutex> cache_lock(
                m_page_cache_mutex);
            if (m_page_lru_cache.has(pageno))
            {
                // fz_drop_display_list(m_ctx,
                // m_page_cache[pageno].display_list);
                // m_page_cache.erase(pageno);
                m_page_lru_cache.remove(pageno);
            }
        }
        // Build cache outside the lock to avoid holding it during expensive
        // operations
        buildPageCache(pageno);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "addTextAnnotation failed:" << fz_caught_message(m_ctx);
    }

#ifndef NDEBUG
    qDebug() << "Adding text annotation on page" << pageno
             << " ObjNum:" << objNum;
#endif
    return objNum;
}

void
Model::setTextAnnotationContents(const int pageno, const int objNum,
                                 const QString &text) noexcept
{
    bool changed = false;

    fz_try(m_ctx)
    {
        pdf_page *page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != objNum)
                continue;

            const QByteArray utf8 = text.toUtf8();
            pdf_set_annot_contents(m_ctx, annot, utf8.constData());
            pdf_update_annot(m_ctx, annot);
            pdf_update_page(m_ctx, page);
            changed = true;
            break;
        }

        pdf_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "setTextAnnotationContents failed:"
                   << fz_caught_message(m_ctx);
        return;
    }

    if (changed)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
}

void
Model::setUrlLinkRegex(const QString &pattern) noexcept
{
    const QString defaultPattern
        = QString::fromUtf8(R"((https?://|www\.)[^\s<>()\"']+)");
    const QString effectivePattern
        = pattern.isEmpty() ? defaultPattern : pattern;
    QRegularExpression re(effectivePattern);
    re.optimize();

    if (!re.isValid())
    {
        qWarning() << "Invalid url_regex:" << re.errorString();
        re = QRegularExpression(defaultPattern);
    }
    m_url_link_re = re;
}

void
Model::removeAnnotations(int pageno, const std::vector<int> &objNums) noexcept
{
    if (objNums.empty())
        return;

    // Build fast lookup set
    std::unordered_set<int> to_delete;
    to_delete.reserve(objNums.size());
    for (int n : objNums)
        to_delete.insert(n);

    fz_try(m_ctx)
    {
        pdf_page *page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        bool changed = false;

        // Safe iteration pattern: grab next before deleting current
        for (pdf_annot *a = pdf_first_annot(m_ctx, page); a;)
        {
            pdf_annot *next = pdf_next_annot(m_ctx, a);

            pdf_obj *obj  = pdf_annot_obj(m_ctx, a);
            const int num = obj ? pdf_to_num(m_ctx, obj) : 0;

            if (num != 0 && to_delete.find(num) != to_delete.end())
            {
                pdf_delete_annot(m_ctx, page, a);
                changed = true;
            }

            a = next;
        }

        if (changed)
        {
#ifndef NDEBUG
            qDebug() << "Removed annotations on page" << pageno
                     << " Count:" << objNums.size();
#endif
            // Update once
            pdf_update_page(m_ctx, page);

            invalidatePageCache(pageno);
            emit reloadRequested(pageno);
            // Optional (depends on your saving flow):
            // pdf_document *doc = m_pdf_doc;
            // pdf_write_options opts = ...;
            // pdf_save_document(m_ctx, doc, path, &opts);
        }

        pdf_drop_page(m_ctx,
                      page); // prefer this if available in your MuPDF
    }
    fz_catch(m_ctx)
    {
        qWarning() << "removeAnnotations failed:" << fz_caught_message(m_ctx);
    }
}

void
Model::invalidatePageCache(int pageno) noexcept
{
    std::lock_guard<std::recursive_mutex> cache_lock(m_page_cache_mutex);
    if (m_page_lru_cache.has(pageno))
    {
        // fz_drop_display_list(m_ctx, m_page_cache[pageno].display_list);
        // m_page_cache.erase(pageno);
        m_page_lru_cache.remove(pageno);

        // buildPageCache(pageno);
    }
}

// private helper in Model
std::vector<QPolygonF>
Model::selectAtHelper(int pageno, fz_point pt, int snapMode) noexcept
{
    std::vector<QPolygonF> out;

    constexpr int MAX_HITS = 1024;
    thread_local std::array<fz_quad, MAX_HITS> hits;

    const float scale = logicalScale(); // does not include DPR or DPI,
                                        // since selection is in PDF points

    fz_rect page_bounds{};
    fz_page *page_for_bounds = nullptr;

    fz_try(m_ctx)
    {
        page_for_bounds = fz_load_page(m_ctx, m_doc, pageno);
        page_bounds     = fz_bound_page(m_ctx, page_for_bounds);
    }
    fz_always(m_ctx)
    {
        if (page_for_bounds)
            fz_drop_page(m_ctx, page_for_bounds);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Selection failed (bounds):" << fz_caught_message(m_ctx);
        return out;
    }

    fz_matrix page_to_dev    = fz_scale(scale, scale);
    page_to_dev              = fz_pre_rotate(page_to_dev, m_rotation);
    const fz_rect dev_bounds = fz_transform_rect(page_bounds, page_to_dev);
    page_to_dev
        = fz_concat(page_to_dev, fz_translate(-dev_bounds.x0, -dev_bounds.y0));
    const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

    fz_point a = pt;
    fz_point b = pt;
    a          = fz_transform_point(a, dev_to_page);
    b          = fz_transform_point(b, dev_to_page);

    fz_stext_page *stext_page{nullptr};
    fz_page *page{nullptr};
    int count = 0;

    fz_try(m_ctx)
    {
        page       = fz_load_page(m_ctx, m_doc, pageno);
        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);

        fz_snap_selection(m_ctx, stext_page, &a, &b, snapMode);
        count = fz_highlight_selection(m_ctx, stext_page, a, b, hits.data(),
                                       MAX_HITS);
        m_selection_start = a;
        m_selection_end   = b;
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
        fz_drop_stext_page(m_ctx, stext_page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Selection failed";
        return out;
    }

    out.reserve(count);
    auto toDev = [&](const fz_point &p0) -> QPointF
    {
        const fz_point p = fz_transform_point(p0, page_to_dev);
        return QPointF(p.x, p.y);
    };

    for (int i = 0; i < count; ++i)
    {
        const fz_quad &q = hits[i];
        QPolygonF poly;
        poly.reserve(4);
        poly << toDev(q.ll) << toDev(q.lr) << toDev(q.ur) << toDev(q.ul);
        out.push_back(std::move(poly));
    }

    return out;
}

std::vector<QPolygonF>
Model::selectWordAt(int pageno, fz_point pt) noexcept
{
    return selectAtHelper(pageno, pt, FZ_SELECT_WORDS);
}

std::vector<QPolygonF>
Model::selectLineAt(int pageno, fz_point pt) noexcept
{
    return selectAtHelper(pageno, pt, FZ_SELECT_LINES);
}

std::vector<QPolygonF>
Model::selectParagraphAt(int pageno, fz_point pt) noexcept
{
    std::vector<QPolygonF> out;

    constexpr int MAX_HITS = 1024;
    thread_local std::array<fz_quad, MAX_HITS> hits;

    const float scale = logicalScale(); // does not include DPR or DPI,
                                        // since selection is in PDF points

    fz_rect page_bounds{};
    fz_page *page_for_bounds = nullptr;

    fz_try(m_ctx)
    {
        page_for_bounds = fz_load_page(m_ctx, m_doc, pageno);
        page_bounds     = fz_bound_page(m_ctx, page_for_bounds);
    }
    fz_always(m_ctx)
    {
        if (page_for_bounds)
            fz_drop_page(m_ctx, page_for_bounds);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Selection failed (bounds):" << fz_caught_message(m_ctx);
        return out;
    }

    fz_matrix page_to_dev    = fz_scale(scale, scale);
    page_to_dev              = fz_pre_rotate(page_to_dev, m_rotation);
    const fz_rect dev_bounds = fz_transform_rect(page_bounds, page_to_dev);
    page_to_dev
        = fz_concat(page_to_dev, fz_translate(-dev_bounds.x0, -dev_bounds.y0));
    const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

    fz_point page_pt = fz_transform_point(pt, dev_to_page);

    fz_stext_page *stext_page{nullptr};
    fz_page *page{nullptr};

    fz_try(m_ctx)
    {
        page       = fz_load_page(m_ctx, m_doc, pageno);
        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);

        for (fz_stext_block *block = stext_page->first_block; block;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;

            if (page_pt.x >= block->bbox.x0 && page_pt.x <= block->bbox.x1
                && page_pt.y >= block->bbox.y0 && page_pt.y <= block->bbox.y1)
            {
                fz_point blockStart = {block->bbox.x0, block->bbox.y0};
                fz_point blockEnd   = {block->bbox.x1, block->bbox.y1};

                int count
                    = fz_highlight_selection(m_ctx, stext_page, blockStart,
                                             blockEnd, hits.data(), MAX_HITS);

                auto toDev = [&](const fz_point &p0) -> QPointF
                {
                    const fz_point p = fz_transform_point(p0, page_to_dev);
                    return QPointF(p.x, p.y);
                };

                out.reserve(count);
                for (int i = 0; i < count; ++i)
                {
                    const fz_quad &q = hits[i];
                    QPolygonF poly;
                    poly.reserve(4);
                    poly << toDev(q.ll) << toDev(q.lr) << toDev(q.ur)
                         << toDev(q.ul);
                    out.push_back(std::move(poly));
                }

                m_selection_start = blockStart;
                m_selection_end   = blockEnd;
                break;
            }
        }
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
        fz_drop_stext_page(m_ctx, stext_page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "Quadruple-click paragraph selection failed";
    }

    return out;
}

// Returns {page_to_dev, dev_to_page}, or {identity, identity} on failure
std::pair<fz_matrix, fz_matrix>
Model::buildPageTransforms(int pageno) const noexcept
{
    const fz_matrix identity = fz_identity;
    fz_page *page            = nullptr;
    fz_rect bounds{};

    fz_try(m_ctx)
    {
        page   = fz_load_page(m_ctx, m_doc, pageno);
        bounds = fz_bound_page(m_ctx, page);
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        return {identity, identity};
    }

    const float scale = logicalScale(); // does not include DPR or DPI,
                                        // since selection is in PDF points
    fz_matrix page_to_dev = fz_scale(scale, scale);
    page_to_dev           = fz_pre_rotate(page_to_dev, m_rotation);
    const fz_rect dbox    = fz_transform_rect(bounds, page_to_dev);
    page_to_dev = fz_concat(page_to_dev, fz_translate(-dbox.x0, -dbox.y0));
    return {page_to_dev, fz_invert_matrix(page_to_dev)};
}

void
Model::search(const QString &term, bool caseSensitive) noexcept
{
    if (m_search_future.isRunning())
    {
        m_search_future.cancel();
        m_search_future.waitForFinished();
    }

    m_search_future = QtConcurrent::run([this, term, caseSensitive]()
    {
        QMap<int, std::vector<Model::SearchHit>> results;
        m_search_match_count = 0;

        if (term.isEmpty())
        {
            emit searchResultsReady(results);
            return;
        }

        for (int p = 0; p < m_page_count; ++p)
        {
            auto hits = searchHelper(p, term, caseSensitive);
            if (!hits.empty())
            {
                m_search_match_count += hits.size();
                results.insert(p, std::move(hits));
            }
        }
        emit searchResultsReady(results);
    });
}

void
Model::searchInPage(const int pageno, const QString &term,
                    bool caseSensitive) noexcept
{
    QFuture<void> result
        = QtConcurrent::run([this, pageno, term, caseSensitive]()
    {
        QMap<int, std::vector<Model::SearchHit>> results;
        m_search_match_count = 0;

        if (term.isEmpty() || pageno < 0 || pageno >= m_page_count)
        {
            emit searchResultsReady(results);
            return;
        }

        auto hits = searchHelper(pageno, term, caseSensitive);
        if (!hits.empty())
        {
            m_search_match_count += hits.size();
            results.insert(pageno, std::move(hits));
        }
        emit searchResultsReady(results);
    });
}

std::vector<Model::SearchHit>
Model::searchHelper(int pageno, const QString &term,
                    bool caseSensitive) noexcept
{
    std::vector<SearchHit> results;

    if (term.isEmpty())
        return results;

    buildTextCacheForPages({pageno});

    if (!m_text_cache.has(pageno))
        return results;

    const auto &text = m_text_cache.get(pageno)->chars;
    const int n      = text.size();
    const int m      = term.size();

    if (n < m)
        return results;

    // Convert search term once
    std::vector<uint32_t> pattern;
    pattern.reserve(m);
    for (QChar c : term)
        pattern.push_back(c.unicode());

    for (int i = 0; i <= n - m; ++i)
    {
        bool match = true;

        for (int j = 0; j < m; ++j)
        {
            if (!charEqual(text[i + j].rune, pattern[j], caseSensitive))
            {
                match = false;
                break;
            }
        }

        if (!match)
            continue;

        // Compute **single quad for entire match**
        fz_rect bbox = fz_empty_rect;
        for (int j = 0; j < m; ++j)
        {
            if (!fz_is_empty_quad(text[i + j].quad))
            {
                bbox = fz_union_rect(bbox, fz_rect_from_quad(text[i + j].quad));
            }
        }

        if (!fz_is_empty_rect(bbox))
        {
            results.push_back({
                pageno,
                fz_quad{{bbox.x1, bbox.y1},
                        {bbox.x1, bbox.y0},
                        {bbox.x0, bbox.y1},
                        {bbox.x0, bbox.y0}},
                i // index of first character in match
            });
        }
    }

    return results;
}

std::vector<Model::HighlightText>
Model::collectHighlightTexts(bool groupByLine) noexcept
{
    std::vector<HighlightText> results;

    if (!m_ctx || !m_doc || !m_pdf_doc)
        return results;

    for (int pageno = 0; pageno < m_page_count; ++pageno)
    {
        pdf_page *pdfPage{nullptr};
        fz_stext_page *stext_page{nullptr};

        fz_try(m_ctx)
        {
            pdfPage = pdf_load_page(m_ctx, m_pdf_doc, pageno);
            if (!pdfPage)
                fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

            stext_page = fz_new_stext_page_from_page(m_ctx, (fz_page *)pdfPage,
                                                     nullptr);
            pdf_drop_page(m_ctx, pdfPage);

            if (!stext_page)
                continue;

            for (pdf_annot *annot = pdf_first_annot(m_ctx, pdfPage); annot;
                 annot            = pdf_next_annot(m_ctx, annot))
            {
                if (pdf_annot_type(m_ctx, annot) != PDF_ANNOT_HIGHLIGHT)
                    continue;

                const int quad_count = pdf_annot_quad_point_count(m_ctx, annot);
                if (quad_count <= 0)
                    continue;

                std::vector<fz_quad> quads;
                quads.reserve(quad_count);
                for (int i = 0; i < quad_count; ++i)
                    quads.push_back(pdf_annot_quad_point(m_ctx, annot, i));

                std::vector<fz_quad> line_quads;
                if (groupByLine)
                    line_quads = merge_quads_by_line(quads);
                else
                    line_quads = merged_quads_from_quads(quads);

                for (const fz_quad &q : line_quads)
                {
                    fz_rect rect = fz_rect_from_quad(q);
                    if (fz_is_infinite_rect(rect) || fz_is_empty_rect(rect))
                        continue;

                    const fz_point a{rect.x0, rect.y0};
                    const fz_point b{rect.x1, rect.y1};
                    char *selection_text
                        = fz_copy_selection(m_ctx, stext_page, a, b, 0);
                    if (!selection_text)
                        continue;

                    QString text = QString::fromUtf8(selection_text).trimmed();
                    fz_free(m_ctx, selection_text);

                    if (text.isEmpty())
                        continue;

                    results.push_back({pageno, text, q});
                }
            }
        }
        fz_always(m_ctx)
        {
            pdf_drop_page(m_ctx, pdfPage);
            fz_drop_stext_page(m_ctx, stext_page);
        }
        fz_catch(m_ctx)
        {
            qWarning() << "Failed to collect highlight text on page" << pageno;
        }
    }

    return results;
}

void
Model::buildTextCacheForPages(const std::set<int> &pagenos) noexcept
{
    if (pagenos.empty())
        return;

    fz_context *ctx = cloneContext();
    if (!ctx)
        return;

    for (int pageno : pagenos)
    {
        if (m_text_cache.has(pageno))
            continue;

        fz_page *page        = nullptr;
        fz_stext_page *stext = nullptr;

        fz_try(m_ctx)
        {
            page  = fz_load_page(m_ctx, m_doc, pageno);
            stext = fz_new_stext_page_from_page(m_ctx, page, nullptr);

            CachedTextPage cache;
            cache.chars.reserve(4096); // pre-reserve to reduce reallocations

            for (fz_stext_block *b = stext->first_block; b; b = b->next)
            {
                if (b->type != FZ_STEXT_BLOCK_TEXT)
                    continue;

                for (fz_stext_line *l = b->u.t.first_line; l; l = l->next)
                {
                    for (fz_stext_char *c = l->first_char; c; c = c->next)
                    {
                        cache.chars.push_back(
                            {static_cast<uint32_t>(c->c), c->quad});
                    }

                    // logical line break (prevents cross-line matches)
                    cache.chars.push_back({'\n', {}});
                }
            }

            m_text_cache.put(pageno, std::move(cache));
        }
        fz_always(m_ctx)
        {
            fz_drop_stext_page(m_ctx, stext);
            fz_drop_page(m_ctx, page);
        }
        fz_catch(m_ctx)
        {
            // ignore page failures
        }
    }

    fz_drop_context(ctx);
}

// fz_pixmap *
// Model::hitTestImage(int pageno, const QPointF &pagePos, float zoom,
//                     float rotation) noexcept
// {
// #ifndef NDEBUG
//     qDebug() << "Hit-testing image at page" << pageno << "pos" << pagePos
//              << "zoom" << zoom << "rotation" << rotation;
// #endif
//     std::lock_guard lock(m_doc_mutex);

//     auto it = m_page_cache.find(pageno);
//     if (it == m_page_cache.end() || !it->second.display_list)
//         return nullptr;

//     fz_pixmap *result{nullptr};

//     fz_try(m_ctx)
//     {
//         const PageCacheEntry &entry = m_page_cache[pageno];

//         fz_point query    = {static_cast<float>(pagePos.x()),
//                              static_cast<float>(pagePos.y())};
//         const float scale = m_zoom * (m_dpi / 72.0f);
//         fz_matrix ctm     = fz_scale(1 / scale, 1 / scale);
//         query             = fz_transform_point(query, ctm);

//         auto *dev = reinterpret_cast<ImageHitTestDevice *>(
//             fz_new_device_of_size(m_ctx, sizeof(ImageHitTestDevice)));

//         memset(dev, 0, sizeof(ImageHitTestDevice));

//         dev->query            = query;
//         dev->super.fill_image = hit_test_image;
//         dev->img              = nullptr;

//         fz_matrix transform = fz_transform_page(entry.bounds, zoom,
//         rotation);
//         // fz_matrix transform = fz_identity;
//         fz_rect transformed = fz_transform_rect(entry.bounds, transform);
//         fz_irect bbox       = fz_round_rect(transformed);

//         fz_run_display_list(m_ctx, entry.display_list, (fz_device *)dev,
//                             fz_identity, fz_rect_from_irect(bbox),
//                             nullptr);

//         if (dev->img)
//         {
//             result = fz_get_pixmap_from_image(m_ctx, dev->img, nullptr,
//                                               &transform, nullptr,
//                                               nullptr);

//             fz_drop_image(m_ctx, dev->img);
//         }

//         fz_drop_device(m_ctx, (fz_device *)dev);
//     }
//     fz_catch(m_ctx)
//     {
//         qWarning() << "MuPDF exception during on-demand image hit-test";
//     }

//     return result;
// }

void
Model::annotChangeColor(int pageno, int index, const QColor &color) noexcept
{
    if (!m_pdf_doc)
        return;

    bool changed = false;

    fz_try(m_ctx)
    {
        pdf_page *page = pdf_load_page(m_ctx, m_pdf_doc, pageno);
        if (!page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to load page");

        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot;
             annot            = pdf_next_annot(m_ctx, annot))
        {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) != index)
                continue;

            const float rgb[3] = {color.redF(), color.greenF(), color.blueF()};
            switch (pdf_annot_type(m_ctx, annot))
            {
                case PDF_ANNOT_SQUARE:
                case PDF_ANNOT_TEXT:
                    pdf_set_annot_interior_color(m_ctx, annot, 3, rgb);
                    break;
                case PDF_ANNOT_HIGHLIGHT:
                    pdf_set_annot_color(m_ctx, annot, 3, rgb);
                    break;
                default:
                    break;
            }
            pdf_set_annot_opacity(m_ctx, annot, color.alphaF());
            pdf_update_annot(m_ctx, annot);
            pdf_update_page(m_ctx, page);
            changed = true;
            break;
        }

        pdf_drop_page(m_ctx, page);

        if (!changed)
            qWarning() << "annotChangeColor: annotation not found, index:"
                       << index;
    }
    fz_catch(m_ctx)
    {
        qWarning() << "annotChangeColor failed:" << fz_caught_message(m_ctx);
        return;
    }

    if (changed)
    {
        invalidatePageCache(pageno);
        emit reloadRequested(pageno);
    }
}

std::string
Model::getTextInArea(const int pageno, const QPointF &start,
                     const QPointF &end) noexcept
{
    std::string result;

    const QRectF deviceRect = QRectF(start, end).normalized();
    if (deviceRect.isEmpty())
        return result;

    const float scale = logicalScale(); // does not include DPR or DPI,
                                        // since selection is in PDF points

    fz_stext_page *stext_page{nullptr};
    fz_page *page{nullptr};
    char *selection_text{nullptr};

    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, pageno);

        const fz_rect page_bounds = fz_bound_page(m_ctx, page);
        fz_matrix page_to_dev     = fz_scale(scale, scale);
        page_to_dev               = fz_pre_rotate(page_to_dev, m_rotation);
        const fz_rect dev_bounds  = fz_transform_rect(page_bounds, page_to_dev);
        page_to_dev               = fz_concat(page_to_dev,
                                              fz_translate(-dev_bounds.x0, -dev_bounds.y0));
        const fz_matrix dev_to_page = fz_invert_matrix(page_to_dev);

        fz_point p1 = fz_transform_point(
            {float(deviceRect.left()), float(deviceRect.top())}, dev_to_page);
        fz_point p2 = fz_transform_point(
            {float(deviceRect.right()), float(deviceRect.top())}, dev_to_page);
        fz_point p3 = fz_transform_point(
            {float(deviceRect.right()), float(deviceRect.bottom())},
            dev_to_page);
        fz_point p4 = fz_transform_point(
            {float(deviceRect.left()), float(deviceRect.bottom())},
            dev_to_page);

        const fz_rect rect = {std::min({p1.x, p2.x, p3.x, p4.x}),
                              std::min({p1.y, p2.y, p3.y, p4.y}),
                              std::max({p1.x, p2.x, p3.x, p4.x}),
                              std::max({p1.y, p2.y, p3.y, p4.y})};

        stext_page     = fz_new_stext_page_from_page(m_ctx, page, nullptr);
        selection_text = fz_copy_rectangle(m_ctx, stext_page, rect, 0);
    }
    fz_always(m_ctx)
    {
        if (selection_text)
        {
            result = selection_text;
            fz_free(m_ctx, selection_text);
        }
        fz_drop_stext_page(m_ctx, stext_page);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getTextInArea failed:" << fz_caught_message(m_ctx);
    }

    return result;
}

// std::optional<std::wstring>
// Model::get_paper_name_at_position(const int pageno, const fz_point pos)
// noexcept
// {
//     fz_stext_page *stext_page{nullptr};
//     fz_page *page{nullptr};

//     fz_try(m_ctx)
//     {
//         page       = fz_load_page(m_ctx, m_doc, pageno);
//         stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);
//     }
//     fz_always(m_ctx)
//     {
//         fz_drop_page(m_ctx, page);
//         fz_drop_stext_page(m_ctx, stext_page);
//     }
//     fz_catch(m_ctx)
//     {
//         return {};
//     }

//     if (!stext_page)
//         return {};

//     // 2) Flatten all characters
//     std::vector<fz_stext_char *> flat_chars;
//     flat_chars.reserve(4096);

//     for (fz_stext_block *b = stext_page->first_block; b; b = b->next)
//     {
//         if (b->type != FZ_STEXT_BLOCK_TEXT)
//             continue;

//         for (fz_stext_line *ln = b->u.t.first_line; ln; ln = ln->next)
//         {
//             for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next)
//                 flat_chars.push_back(ch);

//             // Add a sentinel "line break" marker by pushing nullptr
//             (optional),
//             // but we can also just treat end-of-line later via
//             // ch->next==nullptr. (We won't push nullptr here to keep it
//             // simple.)
//         }
//     }

//     if (flat_chars.empty())
//         return {};

//     // 3) Find index of the clicked character (point-in-rect with
//     epsilon) auto contains_point_eps = [&](fz_rect r) -> bool
//     {
//         // expand rect a bit so clicks don't have to be perfect
//         const float eps = 0.75f; // page units; tweak if needed
//         r.x0 -= eps;
//         r.y0 -= eps;
//         r.x1 += eps;
//         r.y1 += eps;
//         return (pos.x >= r.x0 && pos.x <= r.x1 && pos.y >= r.y0
//                 && pos.y <= r.y1);
//     };

//     int hit = -1;
//     for (int i = 0; i < (int)flat_chars.size(); ++i)
//     {
//         fz_stext_char *ch = flat_chars[i];
//         if (!ch)
//             continue;

//         const fz_rect r = fz_rect_from_quad(ch->quad);
//         if (contains_point_eps(r))
//         {
//             hit = i;
//             break;
//         }
//     }

//     if (hit < 0)
//         return {};

//     // 4) Expand to sentence-like chunk delimited by '.' (your original
//     intent) int left  = hit; int right = hit;

//     // Move left to char after previous '.'
//     while (left > 0)
//     {
//         fz_stext_char *ch = flat_chars[left - 1];
//         if (!ch)
//         {
//             --left;
//             continue;
//         }

//         if (ch->c == L'.')
//             break;

//         --left;
//     }

//     // Move right to next '.'
//     while (right < (int)flat_chars.size())
//     {
//         fz_stext_char *ch = flat_chars[right];
//         if (!ch)
//         {
//             ++right;
//             continue;
//         }

//         if (ch->c == L'.')
//             break;

//         ++right;
//     }

//     if (right <= left)
//         return {};

//     // 5) Build the string from [left, right) (excluding the '.')
//     std::wstring out;
//     out.reserve((size_t)(right - left) + 16);

//     for (int i = left; i < right; ++i)
//     {
//         fz_stext_char *ch = flat_chars[i];
//         if (!ch)
//             continue;

//         // Skip end-of-line hyphenation: hyphen at end of a line
//         if (ch->c == L'-' && ch->next == nullptr)
//             continue;

//         // Normal character
//         out.push_back((wchar_t)ch->c);

//         // If end of line, add a space (but avoid double spaces)
//         if (ch->next == nullptr)
//         {
//             if (!out.empty() && out.back() != L' ')
//                 out.push_back(L' ');
//         }
//     }

//     fz_drop_stext_page(m_ctx, stext_page);
//     trim_ws(out);

//     if (out.empty())
//         return {};

//     return out;
// }

// Logic for Model.cpp to get the first character's position
fz_point
Model::getFirstCharPos(const int pageno) noexcept
{
    fz_stext_page *stext_page{nullptr};
    fz_page *page{nullptr};

    fz_try(m_ctx)
    {
        page       = fz_load_page(m_ctx, m_doc, pageno);
        stext_page = fz_new_stext_page_from_page(m_ctx, page, nullptr);
        if (!stext_page)
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Failed to build text page");

        for (fz_stext_block *block = stext_page->first_block; block;
             block                 = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;
            for (fz_stext_line *line = block->u.t.first_line; line;
                 line                = line->next)
            {
                for (fz_stext_char *span = line->first_char; span;
                     span                = span->next)
                {
                    if (span->size > 0)
                    {
                        // Return the origin of the first character in the
                        // first span
                        return span->origin;
                    }
                }
            }
        }
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
        fz_drop_stext_page(m_ctx, stext_page);
    }
    fz_catch(m_ctx)
    {
        qWarning() << "getFirstCharPos failed:" << fz_caught_message(m_ctx);
    }

    return {0, 0};
}

// Detect URL-like text and return as links, excluding areas already covered
// by PDF links
std::vector<Model::RenderLink>
Model::detectUrlLinksForPage(const RenderJob &job) noexcept
{
    std::vector<RenderLink> result;

    fz_context *ctx = cloneContext();
    if (!ctx)
        return result;

    fz_page *text_page        = nullptr;
    fz_stext_page *stext_page = nullptr;

    // Grab cached links under lock to check for intersections
    std::vector<CachedLink> cachedLinks;
    {
        std::lock_guard<std::recursive_mutex> lock(m_page_cache_mutex);
        const PageCacheEntry *entry = m_page_lru_cache.get(job.pageno);
        if (entry)
            cachedLinks = entry->links;
    }

    fz_matrix transform
        = fz_transform_page(fz_empty_rect, job.zoom,
                            job.rotation); // bounds not needed for stext

    fz_try(ctx)
    {
        text_page  = fz_load_page(ctx, m_doc, job.pageno);
        stext_page = fz_new_stext_page_from_page(ctx, text_page, nullptr);

        const QRegularExpression &urlRe = m_url_link_re;

        // Get bounds for proper transform
        fz_rect bounds = fz_bound_page(ctx, text_page);
        transform      = fz_transform_page(bounds, job.zoom, job.rotation);

        for (fz_stext_block *b = stext_page->first_block; b; b = b->next)
        {
            if (b->type != FZ_STEXT_BLOCK_TEXT)
                continue;

            for (fz_stext_line *line = b->u.t.first_line; line;
                 line                = line->next)
            {
                QString lineText;
                lineText.reserve(256);
                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
                    lineText.append(QChar::fromUcs4(ch->c));

                if (lineText.isEmpty())
                    continue;

                QRegularExpressionMatchIterator it
                    = urlRe.globalMatch(lineText);
                while (it.hasNext())
                {
                    QRegularExpressionMatch match = it.next();
                    int start                     = match.capturedStart();
                    int len                       = match.capturedLength();
                    if (start < 0 || len <= 0)
                        continue;

                    QString raw = match.captured();
                    while (!raw.isEmpty()
                           && QString(".,;:!?)\"'").contains(raw.back()))
                    {
                        raw.chop(1);
                        --len;
                    }
                    if (raw.isEmpty() || len <= 0)
                        continue;

                    fz_quad q = getQuadForSubstring(line, start, len);
                    fz_rect r = fz_rect_from_quad(q);
                    if (fz_is_empty_rect(r))
                        continue;

                    // Skip if already covered by a PDF link
                    bool intersects = false;
                    for (const auto &cl : cachedLinks)
                    {
                        const fz_rect lr = cl.rect;
                        if (r.x1 >= lr.x0 && r.x0 <= lr.x1 && r.y1 >= lr.y0
                            && r.y0 <= lr.y1)
                        {
                            intersects = true;
                            break;
                        }
                    }
                    if (intersects)
                        continue;

                    QString uri = raw;
                    if (uri.startsWith("www."))
                        uri.prepend("https://");

                    fz_rect tr        = fz_transform_rect(r, transform);
                    const float scale = m_inv_dpr;
                    QRectF qtRect(tr.x0 * scale, tr.y0 * scale,
                                  (tr.x1 - tr.x0) * scale,
                                  (tr.y1 - tr.y0) * scale);

                    RenderLink renderLink;
                    renderLink.rect     = qtRect;
                    renderLink.uri      = uri;
                    renderLink.type     = BrowseLinkItem::LinkType::External;
                    renderLink.boundary = m_link_show_boundary;
                    result.push_back(std::move(renderLink));
                }
            }
        }
    }
    fz_always(ctx)
    {
        fz_drop_stext_page(ctx, stext_page);
        fz_drop_page(ctx, text_page);
    }
    fz_catch(ctx) {}

    fz_drop_context(ctx);
    return result;
}

void
Model::cancelOpen() noexcept
{
    if (m_pending.ctx)
    {
        fz_drop_document(m_pending.ctx, m_pending.doc);
        fz_drop_context(m_pending.ctx);
        m_pending.clear();
    }

    cleanup();

    emit openFileFailed();
}
