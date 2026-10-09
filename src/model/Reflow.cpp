#include "BrowseLinkItem.hpp"
#include "Commands/TextHighlightAnnotationCommand.hpp"
#include "Config.hpp"
#include "Model.hpp"
#include "utils.hpp"

#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QMovie>
#include <QPainter>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <qbytearrayview.h>
#include <qregularexpression.h>
#include <qstyle.h>
#include <qtextformat.h>
#include <unordered_map>
#include <unordered_set>
#ifdef HAVE_FONTCONFIG

    #include <fontconfig/fontconfig.h>
#endif
#ifdef HAVE_FONTCONFIG
#else // !HAVE_FONTCONFIG

    #include <QDirIterator>
#endif
#ifdef HAVE_FONTCONFIG
#else // !HAVE_FONTCONFIG
    #include <QFile>
#endif
#ifdef HAVE_FONTCONFIG
#else // !HAVE_FONTCONFIG
    #include <QStandardPaths>
#endif
#ifdef HAVE_LIBARCHIVE

    #include <archive.h>
#endif
#ifdef HAVE_LIBARCHIVE
    #include <archive_entry.h>
#endif

static std::array<std::mutex, FZ_LOCK_MAX> mupdf_mutexes;

// ============================================================================

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
#ifdef HAVE_FONTCONFIG

// Lets documents (EPUB/FB2 text styled with a font-family) use fonts that are
// installed on the system. Only an exact family match is accepted: otherwise
// fontconfig would answer every unknown name with its default font and
// override MuPDF's own built-in font handling.
static fz_font *
load_system_font(fz_context *ctx, const char *name, int bold, int italic,
                 int /*needs_exact_metrics*/)
{
    if (!FcInit())
        return nullptr;

    fz_font *font   = nullptr;
    FcPattern *pat  = FcPatternCreate();
    FcPattern *best = nullptr;
    if (!pat)
        return nullptr;

    FcPatternAddString(pat, FC_FAMILY, reinterpret_cast<const FcChar8 *>(name));
    FcPatternAddInteger(pat, FC_WEIGHT,
                        bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,
                        italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    best = FcFontMatch(nullptr, pat, &result);
    if (best)
    {
        FcChar8 *family = nullptr;
        FcChar8 *file   = nullptr;
        int index       = 0;
        if (FcPatternGetString(best, FC_FAMILY, 0, &family) == FcResultMatch
            && FcPatternGetString(best, FC_FILE, 0, &file) == FcResultMatch
            && qstrnicmp(reinterpret_cast<const char *>(family), name,
                         strlen(name))
                   == 0
            && strlen(reinterpret_cast<const char *>(family)) == strlen(name))
        {
            FcPatternGetInteger(best, FC_INDEX, 0, &index);
            fz_try(ctx) font = fz_new_font_from_file(
                ctx, nullptr, reinterpret_cast<const char *>(file), index, 0);
            fz_catch(ctx) font = nullptr;
        }
        FcPatternDestroy(best);
    }
    FcPatternDestroy(pat);
    return font;
}
#endif
#ifdef HAVE_FONTCONFIG
#else // !HAVE_FONTCONFIG
namespace
{

struct IndexedFont
{
    QString path;
    int index   = 0; // face within a .ttc collection
    bool bold   = false;
    bool italic = false;
};

QHash<QString, std::vector<IndexedFont>> g_font_index; // lower-case family
QFuture<void> g_font_index_future;
std::once_flag g_font_index_once;

quint32
be32(const QByteArray &b, int o)
{
    if (o < 0 || o + 4 > b.size())
        return 0;
    return (quint32(uchar(b[o])) << 24) | (quint32(uchar(b[o + 1])) << 16)
           | (quint32(uchar(b[o + 2])) << 8) | quint32(uchar(b[o + 3]));
}

quint16
be16(const QByteArray &b, int o)
{
    if (o < 0 || o + 2 > b.size())
        return 0;
    return (quint16(uchar(b[o])) << 8) | quint16(uchar(b[o + 1]));
}

// Reads one face (the sfnt starting at `base`) and adds it to the index.
void
indexFace(QFile &f, const QString &path, quint32 base, int faceIndex,
          QHash<QString, std::vector<IndexedFont>> &out)
{
    f.seek(base);
    const QByteArray hdr = f.read(12);
    if (hdr.size() < 12)
        return;
    const int numTables  = be16(hdr, 4);
    const QByteArray dir = f.read(qint64(numTables) * 16);

    quint32 nameOff = 0, nameLen = 0, headOff = 0;
    for (int i = 0; i < numTables && (i + 1) * 16 <= dir.size(); ++i)
    {
        const QByteArray tag = dir.mid(i * 16, 4);
        if (tag == "name")
        {
            nameOff = be32(dir, i * 16 + 8);
            nameLen = be32(dir, i * 16 + 12);
        }
        else if (tag == "head")
            headOff = be32(dir, i * 16 + 8);
    }
    if (!nameOff || nameLen < 6 || nameLen > (1u << 20))
        return;

    IndexedFont font;
    font.path  = path;
    font.index = faceIndex;
    if (headOff && f.seek(headOff))
    {
        const QByteArray head = f.read(54); // macStyle at offset 44
        const quint16 mac     = be16(head, 44);
        font.bold             = mac & 1;
        font.italic           = mac & 2;
    }

    f.seek(nameOff);
    const QByteArray t = f.read(nameLen);
    const int count = be16(t, 2), strings = be16(t, 4);
    QStringList names;
    for (int i = 0; i < count; ++i)
    {
        const int rec = 6 + i * 12;
        if (rec + 12 > t.size())
            break;
        const int platform = be16(t, rec), id = be16(t, rec + 6);
        const int len = be16(t, rec + 8), off = strings + be16(t, rec + 10);
        // Legacy family (1) and typographic family (16).
        if ((id != 1 && id != 16) || off + len > t.size())
            continue;
        QString name;
        if (platform == 3 || platform == 0) // UTF-16BE
        {
            for (int k = 0; k + 1 < len; k += 2)
                name += QChar(be16(t, off + k));
        }
        else if (platform == 1) // Mac Roman; ASCII-compatible for names
            name = QString::fromLatin1(t.constData() + off, len);
        if (!name.isEmpty() && !names.contains(name, Qt::CaseInsensitive))
            names << name;
    }
    for (const QString &n : names)
        out[n.toLower()].push_back(font);
}

void
buildFontIndex()
{
    QHash<QString, std::vector<IndexedFont>> index;
    const QStringList filters{"*.ttf", "*.otf", "*.ttc", "*.otc"};
    for (const QString &dir :
         QStandardPaths::standardLocations(QStandardPaths::FontsLocation))
    {
        QDirIterator it(dir, filters, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString path = it.next();
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QByteArray tag = f.read(4);
            if (tag == "ttcf")
            {
                f.seek(8);
                const QByteArray n    = f.read(4);
                const quint32 faces   = be32(n, 0);
                const QByteArray offs = f.read(qint64(faces) * 4);
                for (quint32 i = 0; i < faces && i < 64; ++i)
                    indexFace(f, path, be32(offs, i * 4), int(i), index);
            }
            else
                indexFace(f, path, 0, 0, index);
        }
    }
    g_font_index = std::move(index);
}

void
startFontIndex()
{
    std::call_once(g_font_index_once, []
    { g_font_index_future = QtConcurrent::run(buildFontIndex); });
}

fz_font *
load_system_font(fz_context *ctx, const char *name, int bold, int italic,
                 int /*needs_exact_metrics*/)
{
    startFontIndex();
    g_font_index_future.waitForFinished(); // no-op once the scan is done

    const auto it = g_font_index.constFind(QString::fromUtf8(name).toLower());
    if (it == g_font_index.cend() || it->empty())
        return nullptr;

    // Prefer the requested bold/italic, else the closest face.
    const IndexedFont *best = nullptr;
    int bestScore           = -1;
    for (const IndexedFont &f : *it)
    {
        const int score = (f.bold == bool(bold) ? 2 : 0)
                          + (f.italic == bool(italic) ? 1 : 0);
        if (score > bestScore)
        {
            best      = &f;
            bestScore = score;
        }
    }

    fz_font *font         = nullptr;
    const QByteArray path = best->path.toUtf8();
    fz_try(ctx) font
        = fz_new_font_from_file(ctx, nullptr, path.constData(), best->index, 0);
    fz_catch(ctx) font = nullptr;
    return font;
}
} // namespace

#endif

void
Model::prewarmFontIndex() noexcept
{
#ifndef HAVE_FONTCONFIG
    startFontIndex();
#endif
}

void
Model::setReflowStyle(const QString &fontFamily, float lineSpacing) noexcept
{
    if (!m_ctx)
        return;

#ifndef HAVE_FONTCONFIG
    if (!fontFamily.isEmpty())
        startFontIndex(); // background scan; the loader waits for it
#endif

    QString css;
    if (!fontFamily.isEmpty())
    {
        QString family = fontFamily;
        family.remove('"').remove('\\').remove('{').remove('}').remove(';');
        css += QString(
                   "body, p, div, span, li, td, th, blockquote, h1, h2, h3, "
                   "h4, h5, h6 { font-family: \"%1\" !important; }\n")
                   .arg(family);
    }
    if (lineSpacing > 0.0f)
        css += QString("body, p, div, span, li, td, th, blockquote "
                       "{ line-height: %1 !important; }\n")
                   .arg(lineSpacing);

    // Applied to each document as it opens (see openAsync_mupdf), with
    // fz_style_document. The context-wide fz_set_user_css it replaces is
    // deprecated.
    m_reflow_css = css.toUtf8();

    // Force the next relayoutForViewport() to run even if the page box and
    // font size are unchanged.
    m_layout_em = 0.0f;
}

void
Model::initMuPDF() noexcept
{
    // initialize each mutex
    m_fz_locks.user   = mupdf_mutexes.data();
    m_fz_locks.lock   = mupdf_lock_mutex;
    m_fz_locks.unlock = mupdf_unlock_mutex;
    const size_t storeBytes
        = static_cast<size_t>(m_config.behavior.mupdf_store_size) << 20;
    m_ctx = fz_new_context(nullptr, &m_fz_locks, storeBytes);
    fz_register_document_handlers(m_ctx);
    fz_install_load_system_font_funcs(m_ctx, load_system_font, nullptr,
                                      nullptr);
    m_colorspace = fz_device_rgb(m_ctx);
}

QFuture<void>
Model::relayoutForViewport(float widthPts, float heightPts,
                           float emPts) noexcept
{
    if (!supports_reflow() || !m_doc)
        return QtConcurrent::run([] {});

    if (widthPts <= 0 || heightPts <= 0 || emPts <= 0)
        return QtConcurrent::run([] {});

    // Nothing changed since the last relayout (or since open, which left
    // the document at MuPDF's built-in default) — fz_layout_document
    // forces a page-count recompute that lays out every chapter's HTML to
    // count its pages, so skip repeating that for a no-op resize tick.
    if (widthPts == m_layout_w && heightPts == m_layout_h
        && emPts == m_layout_em)
        return QtConcurrent::run([] {});

    fz_context *ctx = cloneContext();
    if (!ctx)
        return QtConcurrent::run([] {});

    return QtConcurrent::run([this, ctx, widthPts, heightPts, emPts]
    {
        int page_count = 0;
        float w = 0, h = 0;
        bool ok = true;

        std::lock_guard<std::mutex> lock(m_doc_mutex);

        fz_try(ctx)
        {
            fz_layout_document(ctx, m_doc, widthPts, heightPts, emPts);
            page_count = fz_count_pages(ctx, m_doc);
            if (page_count > 0)
            {
                fz_page *p = fz_load_page(ctx, m_doc, 0);
                fz_rect r  = fz_bound_page(ctx, p);
                fz_drop_page(ctx, p);
                w = r.x1 - r.x0;
                h = r.y1 - r.y0;
            }
        }
        fz_catch(ctx)
        {
            ok = false;
        }
        fz_drop_context(ctx);

        if (!ok)
            return;

        QMetaObject::invokeMethod(
            this, [this, widthPts, heightPts, emPts, page_count, w, h]
        {
            waitForPendingRenders();
            m_render_cancelled.store(false, std::memory_order_release);

            m_layout_w   = widthPts;
            m_layout_h   = heightPts;
            m_layout_em  = emPts;
            m_page_count = page_count;

            {
                std::lock_guard<std::recursive_mutex> lk(m_page_cache_mutex);
                m_page_lru_cache.clear();
                m_text_cache.clear();
                m_has_text_layer = -1;
                m_stext_page_cache.clear();
            }
            {
                std::lock_guard<std::mutex> lk(m_page_dim_mutex);
                m_default_page_dim = {w, h};
                m_page_dim_cache.dimensions.assign(page_count,
                                                   m_default_page_dim);
                m_page_dim_cache.known.assign(page_count, 0);
                if (page_count > 0)
                    m_page_dim_cache.known[0] = true;
            }
            {
                std::lock_guard<std::mutex> lk(m_content_bbox_mutex);
                m_content_bbox_cache.clear();
            }

            // The embedded outline (m_outline) resolves each node's page
            // live via resolveOutlineNode() -> fz_resolve_link() on every
            // use, so it self-corrects for free. The generated outline
            // bakes in a resolved fz_location at generation time
            // (generateOutline() -> fz_location_from_page_number()), which
            // is now stale — drop it so it lazily regenerates next use.
            fz_drop_outline(m_ctx, m_generated_outline);
            m_generated_outline = nullptr;
            invalidateOutlineEntries();

            emit documentRelayouted();
        }, Qt::QueuedConnection);
    });
}
