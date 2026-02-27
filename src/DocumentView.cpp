#include "DocumentView.hpp"

#include "Annotations/HighlightAnnotation.hpp"
#include "Annotations/RectAnnotation.hpp"
#include "Annotations/TextAnnotation.hpp"
#include "BrowseLinkItem.hpp"
#include "Commands/DeleteAnnotationsCommand.hpp"
#include "Commands/RectAnnotationCommand.hpp"
#include "Commands/TextAnnotationCommand.hpp"
#include "Config.hpp"
#include "GraphicsImageItem.hpp"
#include "GraphicsPixmapItem.hpp"
#include "GraphicsView.hpp"
#include "LinkHint.hpp"
#include "PropertiesWidget.hpp"
#include "WaitingSpinnerWidget.hpp"
#include "mupdf/pdf/annot.h"
#include "utils.hpp"

#include <QClipboard>
#include <QColorDialog>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QTextCursor>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <qdebug.h>
#include <qguiapplication.h>
#include <qicon.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qpolygon.h>
#include <qstyle.h>

static DocumentView::Id nextId{0};

static DocumentView::Id
g_newId() noexcept
{
    return nextId++;
}

DocumentView::DocumentView(const Config &config, QWidget *parent) noexcept
    : QWidget(parent), m_id(g_newId()), m_config(config)
{
#ifndef NDEBUG
    qDebug() << "DocumentView::DocumentView(): Initializing DocumentView";
#endif

    m_model = new Model(this);
    connect(m_model, &Model::openFileFailed, this,
            [this]() { emit openFileFailed(this); });

    connect(m_model, &Model::openFileFinished, this,
            &DocumentView::handleOpenFileFinished);

    connect(m_model, &Model::passwordRequired, this,
            &DocumentView::handle_password_required);

    connect(m_model, &Model::wrongPassword, this,
            &DocumentView::handle_wrong_password);

    initGui();
#ifdef HAS_SYNCTEX
    initSynctex();
#endif
}

DocumentView::~DocumentView() noexcept
{
    // Stop and WAIT for all renders to finish before touching anything
    m_cancelled->store(true);
    stopPendingRenders();
#ifdef HAS_SYNCTEX
    synctex_scanner_free(m_synctex_scanner);
#endif

    m_model->cleanup();

    m_gscene->removeItem(m_jump_marker);
    m_gscene->removeItem(m_selection_path_item);
    m_gscene->removeItem(m_current_search_hit_item);
    m_gscene->removeItem(m_visual_line_item);

    clearDocumentItems();

    delete m_visual_line_item;
    delete m_jump_marker;
    delete m_selection_path_item;
    delete m_current_search_hit_item;
}

void
DocumentView::initGui() noexcept
{
    m_gview  = new GraphicsView(m_config, this);
    m_gscene = new GraphicsScene(m_gview);
    m_gview->setScene(m_gscene);

    m_spinner = new WaitingSpinnerWidget(this);
    m_spinner->setInnerRadius(5.0);
    m_spinner->setColor(palette().color(QPalette::Text));

    m_spacing             = m_config.layout.spacing;
    m_selection_path_item = m_gscene->addPath(QPainterPath());

    m_selection_path_item->setBrush(
        QBrush(rgbaToQColor(m_config.colors.selection)));
    m_selection_path_item->setPen(Qt::NoPen);
    m_selection_path_item->setZValue(ZVALUE_TEXT_SELECTION);

    m_current_search_hit_item = m_gscene->addPath(QPainterPath());
    m_current_search_hit_item->setBrush(
        rgbaToQColor(m_config.colors.search_index));
    m_current_search_hit_item->setPen(Qt::NoPen);
    m_current_search_hit_item->setZValue(ZVALUE_SEARCH_HITS + 1);

    m_hq_render_timer = new QTimer(this);
    m_hq_render_timer->setInterval(150);
    m_hq_render_timer->setSingleShot(true);

    m_scroll_page_update_timer = new QTimer(this);
    m_scroll_page_update_timer->setInterval(66);
    m_scroll_page_update_timer->setSingleShot(true);

    m_resize_timer = new QTimer(this);
    m_resize_timer->setInterval(100);
    m_resize_timer->setSingleShot(true);
    connect(m_resize_timer, &QTimer::timeout, this,
            &DocumentView::handleDeferredResize);

    m_jump_marker = new JumpMarker(rgbaToQColor(m_config.colors.jump_marker));
    m_jump_marker->setZValue(ZVALUE_JUMP_MARKER);
    m_gscene->addItem(m_jump_marker);

    m_gview->setAlignment(Qt::AlignCenter);
    m_gview->setDefaultMode(m_config.behavior.initial_mode);
    m_gview->setMode(m_config.behavior.initial_mode);
    m_gview->setBackgroundBrush(rgbaToQColor(m_config.colors.background));
    m_model->setAnnotRectColor(
        rgbaToQColor(m_config.colors.annot_rect).toRgb());
    m_model->setSelectionColor(rgbaToQColor(m_config.colors.selection));
    m_model->setHighlightColor(rgbaToQColor(m_config.colors.highlight));
    // m_model->setAntialiasingBits(m_config.rendering.antialiasing_bits);
    m_model->undoStack()->setUndoLimit(m_config.behavior.undo_limit);

    m_model->setInvertColor(m_config.behavior.invert_mode);
    m_model->setLinkBoundary(m_config.links.boundary);
    m_model->setDetectUrlLinks(m_config.links.detect_urls);
    m_model->setUrlLinkRegex(m_config.links.url_regex);
    // if (m_config.rendering.icc_color_profile)
    //     m_model->enableICC();
    m_model->setCacheCapacity(m_config.behavior.cache_pages);
    m_model->setBackgroundColor(m_config.colors.page_background);
    m_model->setForegroundColor(m_config.colors.page_foreground);

    m_hscroll = new ScrollBar(Qt::Horizontal, this);
    m_vscroll = new ScrollBar(Qt::Vertical, this);
    m_gview->setVerticalScrollBar(m_vscroll);
    m_gview->setHorizontalScrollBar(m_hscroll);
    m_gview->bindScrollbarActivity(m_vscroll, m_hscroll);

    // Scrollbar policies are always off - we use overlay scrollbars
    // that don't affect layout. Visibility is controlled separately.
    m_gview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Parent scrollbars to viewport so they overlay content
    // This must be done after setVerticalScrollBar/setHorizontalScrollBar
    m_vscroll->setParent(m_gview->viewport());
    m_hscroll->setParent(m_gview->viewport());

    // Apply scrollbar size from config
    m_vscroll->setSize(m_config.scrollbars.size);
    m_hscroll->setSize(m_config.scrollbars.size);
    m_gview->setScrollbarSize(m_config.scrollbars.size);
    m_gview->setScrollbarIdleTimeout(m_config.scrollbars.hide_timeout * 1000);

    // Enable/disable each scrollbar based on config
    // auto_hide controls whether they fade after inactivity
    m_gview->setVerticalScrollbarEnabled(m_config.scrollbars.vertical);
    m_gview->setHorizontalScrollbarEnabled(m_config.scrollbars.horizontal);
    m_gview->setAutoHideScrollbars(m_config.scrollbars.auto_hide);

    m_auto_resize       = m_config.layout.auto_resize;
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(0, 0, 0, 0);
    this->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_gview);
}

// Get the size of the current page in scene coordinates
QSizeF
DocumentView::pageSceneSize(int pageno) const noexcept
{
    const float scale = m_model->logicalScale();

    const auto pageDim = m_model->page_dimension_pts(pageno);
    double w           = pageDim.width_pts * scale;
    double h           = pageDim.height_pts * scale;

    const int rot
        = static_cast<int>(std::fmod(std::abs(m_model->rotation()), 360.0));
    if (rot == 90 || rot == 270)
        std::swap(w, h);

    return QSizeF(w, h);
}

void
DocumentView::setLayoutMode(const LayoutMode &mode) noexcept
{
    if (m_layout_mode == mode)
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::setLayoutMode(): Changing layout mode to"
             << static_cast<int>(mode);
#endif

    m_layout_mode = mode;
    initConnections();
    invalidateVisiblePagesCache();

    if (m_model->numPages() == 0)
        return;

    clearDocumentItems();
    cachePageStride();
    updateSceneRect();

    // Ensure we are in valid page number
    m_pageno = std::clamp(m_pageno, 0, m_model->numPages() - 1);

    GotoPage(m_pageno);
    renderPages();
}

#ifdef HAS_SYNCTEX
void
DocumentView::initSynctex() noexcept
{
    if (m_synctex_scanner)
    {
        synctex_scanner_free(m_synctex_scanner);
        m_synctex_scanner = nullptr;
    }
    m_synctex_scanner = synctex_scanner_new_with_output_file(
        CSTR(m_model->filePath()), nullptr, 1);
    if (!m_synctex_scanner)
        return;
}
#endif

void
DocumentView::openAsync(const QString &filePath) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::openAsync(): Opening file:" << filePath;
#endif
    clearDocumentItems();

    m_spinner->start();
    m_spinner->show();

    QFuture<void> future = m_model->openAsync(QDir::cleanPath(filePath));

    m_open_future_watcher.setFuture(future);
}

void
DocumentView::handleOpenFileFinished() noexcept
{
    m_spinner->stop();
    m_spinner->hide();

    m_pageno = 0;

    // Block scroll signals to prevent jumping during layout swap
    m_vscroll->blockSignals(true);
    m_hscroll->blockSignals(true);

    // First, clear old items and caches
    clearDocumentItems();
    invalidateVisiblePagesCache();

    setLayoutMode(m_config.layout.mode);
    initConnections();

    m_vscroll->blockSignals(false);
    m_hscroll->blockSignals(false);

    // Always defer fitmode to next event loop tick so geometry is settled
    QTimer::singleShot(0, this,
                       [this]() { setFitMode(m_config.layout.initial_fit); });

    setAutoReload(m_config.behavior.auto_reload);
    emit openFileFinished(this);
}

void
DocumentView::resetConnections() noexcept
{
#ifndef NDEBUG
    qDebug()
        << "DocumentView::resetConnections(): Clearing existing connections";
#endif

    // 1. Disconnect specific objects that signal INTO this DocumentView
    if (m_model)
    {
        m_model->disconnect(this);
    }

    if (m_gview)
    {
        m_gview->disconnect(this);
    }

    // 3. Disconnect UI elements that are layout-dependent
    if (m_hscroll)
    {
        m_hscroll->disconnect(this);
        // Also disconnect it from the timer if it was connected there
        m_hscroll->disconnect(m_scroll_page_update_timer);
    }

    if (m_vscroll)
    {
        m_vscroll->disconnect(this);
        m_vscroll->disconnect(m_scroll_page_update_timer);
    }

    if (m_hq_render_timer)
    {
        m_hq_render_timer->disconnect(this);
    }

    if (m_scroll_page_update_timer)
    {
        m_scroll_page_update_timer->disconnect(this);
    }

}

// Initialize signal-slot connections
void
DocumentView::initConnections() noexcept
{
    resetConnections();

#ifndef NDEBUG
    qDebug() << "DocumentView::initConnections(): Initializing connections";
#endif

#ifdef HAS_SYNCTEX
    connect(m_gview, &GraphicsView::synctexJumpRequested, this,
            &DocumentView::handleSynctexJumpRequested);
#endif

    // connect(m_gview, &GraphicsView::rightClickRequested, this,
    //         [&](const QPointF &scenePos)
    // {
    // int pageIndex                = -1;
    // GraphicsImageItem *pageItem = nullptr;

    // if (!pageAtScenePos(scenePos, pageIndex, pageItem))
    //     return; // selection start outside visible pages?

    // const QPointF pagePos = pageItem->mapFromScene(scenePos);
    // m_hit_pixmap = m_model->hitTestImage(pageIndex, pagePos,
    // m_current_zoom,
    //                                      m_rotation);
    // });
    connect(m_model, &Model::searchResultsReady, this,
            &DocumentView::handleSearchResults);

    connect(m_model, &Model::searchPartialResultsReady, this,
            &DocumentView::handlePartialSearchResults);

    connect(m_model, &Model::urlLinksReady, this,
            [this](int pageno, std::vector<Model::RenderLink> links)
    { renderLinks(pageno, links, true); });

    connect(m_model, &Model::reloadRequested, this,
            &DocumentView::handleReloadRequested, Qt::UniqueConnection);

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        connect(m_hscroll, &QScrollBar::valueChanged, this,
                &DocumentView::handleHScrollValueChanged, Qt::UniqueConnection);

        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPages, Qt::UniqueConnection);

        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::renderPages, Qt::UniqueConnection);
    }
    else if (m_layout_mode == LayoutMode::TOP_TO_BOTTOM
             || m_layout_mode == LayoutMode::BOOK)
    {
        connect(m_vscroll, &QScrollBar::valueChanged, this,
                &DocumentView::handleVScrollValueChanged, Qt::UniqueConnection);

        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPages, Qt::UniqueConnection);

        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::renderPages, Qt::UniqueConnection);
    }

    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPage, Qt::UniqueConnection);
    }

    /* Graphics View Signals */
    connect(m_gview, &GraphicsView::textHighlightRequested, this,
            &DocumentView::handleTextHighlightRequested);

    connect(m_gview, QOverload<QRectF>::of(&GraphicsView::annotSelectRequested),
            this, [this](QRectF sceneRect)
    { handleAnnotSelectRequested(sceneRect); });

    connect(m_gview,
            QOverload<QPointF>::of(&GraphicsView::annotSelectRequested), this,
            [this](QPointF scenePos) { handleAnnotSelectRequested(scenePos); });

    connect(m_gview, &GraphicsView::annotSelectClearRequested, this,
            &DocumentView::handleAnnotSelectClearRequested);

    connect(m_gview, &GraphicsView::textSelectionRequested, this,
            &DocumentView::handleTextSelection);

    connect(m_gview, &GraphicsView::textSelectionDeletionRequested, this,
            &DocumentView::ClearTextSelection);

    connect(m_gview, &GraphicsView::clickRequested, this,
            &DocumentView::handleClickSelection);

    connect(m_gview, &GraphicsView::contextMenuRequested, this,
            &DocumentView::handleContextMenuRequested);

    connect(m_gview, &GraphicsView::regionSelectRequested, this,
            &DocumentView::handleRegionSelectRequested);

    connect(m_gview, &GraphicsView::annotRectRequested, this,
            &DocumentView::handleAnnotRectRequested);

    connect(m_gview, &GraphicsView::annotPopupRequested, this,
            &DocumentView::handleAnnotPopupRequested);

    connect(m_gview, &GraphicsView::linkCtrlClickRequested, this,
            &DocumentView::handleLinkCtrlClickRequested);
}

void
DocumentView::handleLinkCtrlClickRequested(QPointF scenePos) noexcept
{
    int pageIndex               = -1;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageIndex, pageItem))
        return;

    const std::vector<BrowseLinkItem *> links_in_page
        = m_page_links_hash[pageIndex];
    if (links_in_page.empty())
        return;

    // Get the link item at the clicked position, if any
    BrowseLinkItem *clicked_link{nullptr};
    for (BrowseLinkItem *link : links_in_page)
    {
        if (link->contains(scenePos))
        {
            clicked_link = link;
            break;
        }
    }

    if (!clicked_link)
        return;
    emit ctrlLinkClickRequested(this, clicked_link);
}

void
DocumentView::handleSearchResults(
    const QMap<int, std::vector<Model::SearchHit>> &results) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleSearchResults(): Received"
             << results.size() << "pages with search hits.";
#endif

    emit searchBarSpinnerShow(false);
    clearSearchHits();

    if (results.isEmpty())
    {
        QMessageBox::information(this, tr("Search"),
                                 tr("No matches found for "
                                    "the given term."));
        return;
    }

    m_search_hits = results;
    buildFlatSearchHitIndex();

    m_search_index = 0;

    if (m_config.scrollbars.search_hits)
        renderSearchHitsInScrollbar();

    emit searchCountChanged(m_model->searchMatchesCount());

    GotoHit(0);
}

void
DocumentView::handlePartialSearchResults(
    const QMap<int, std::vector<Model::SearchHit>> &results) noexcept
{
    m_search_hits = results; // full accumulated map, just replace

    buildFlatSearchHitIndex();

    if (m_config.scrollbars.search_hits)
        renderSearchHitsInScrollbar();

    emit searchCountChanged(m_model->searchMatchesCount());

    // Jump to first hit only on the very first partial result
    if (m_search_index == -1 && !m_search_hit_flat_refs.empty())
        GotoHit(0);
}

void
DocumentView::buildFlatSearchHitIndex() noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::buildFlatSearchHitIndex(): Building flat index";
#endif
    m_search_hit_flat_refs.clear();
    m_search_hit_flat_refs.reserve(m_model->searchMatchesCount());

    for (auto it = m_search_hits.constBegin(); it != m_search_hits.constEnd();
         ++it)
    {
        const int page   = it.key();
        const auto &hits = it.value();

        for (unsigned int i = 0; i < hits.size(); ++i)
            m_search_hit_flat_refs.push_back({page, static_cast<int>(i)});
    }
}

void
DocumentView::handleClickSelection(int clickType, QPointF scenePos) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleClickSelection(): Handling click type"
             << clickType << "at scene position" << scenePos;
#endif
    int pageIndex               = -1;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageIndex, pageItem))
        return;

    // Map to page-local coordinates
    const QPointF pagePos = pageItem->mapFromScene(scenePos);

    if (clickType == 1) // single click → place cursor or snap visual line
    {
        if (has_text_selection())
        {
            ClearTextSelection();
            return;
        }

        if (m_gview->mode() == GraphicsView::Mode::VisualLine)
        {
            const float scale = m_model->logicalScale();
            const QPointF modelPos(pagePos.x() / scale, pagePos.y() / scale);

            m_visual_line_index
                = m_model->visual_line_index_at_pos(pageIndex, modelPos);
            m_visual_lines = m_model->get_text_lines(
                pageIndex);       // ensure lines are for this page
            m_pageno = pageIndex; // sync page if user clicked a
                                  // different page
            snap_visual_line(false);
            return;
        }
    }

    fz_point pdfPos{float(pagePos.x()), float(pagePos.y())};

    std::vector<QPolygonF> quads;
    switch (clickType)
    {
        case 2: // double click → select word
        {
            quads = m_model->selectWordAt(pageIndex, pdfPos);
        }
        break;

        case 3: // triple click → select line
            quads = m_model->selectLineAt(pageIndex, pdfPos);
            break;

        case 4: // quadruple click → select entire page
            quads = m_model->selectParagraphAt(pageIndex, pdfPos);
            break;

        default:
            return;
    }

    if (quads.empty())
        return;

    QPainterPath totalPath;

    const QTransform toScene = pageItem->sceneTransform();
    for (const QPolygonF &poly : quads)
    {
        totalPath.addPolygon(toScene.map(poly));
    }
    m_selection_path_item->setPath(totalPath);

    // MuPDF quad winding: [top-left, top-right, bottom-right, bottom-left]

    const QPolygonF firstQuad = toScene.map(quads.front());
    const QPolygonF lastQuad  = toScene.map(quads.back());

    m_selection_start = firstQuad.at(0); // top-left of first quad
    m_selection_end   = lastQuad.at(2);

    // Update metadata for copying/highlighting
    m_selection_start_page = pageIndex;
    m_selection_end_page   = pageIndex;

    m_selection_path_item->show();
}

// Handle SyncTeX jump request
#ifdef HAS_SYNCTEX
void
DocumentView::handleSynctexJumpRequested(QPointF scenePos) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleSynctexJumpRequested(): Handling "
             << "SyncTeX jump to scene position" << scenePos;
#endif

    if (m_synctex_scanner)
    {
        int pageIndex               = -1;
        GraphicsImageItem *pageItem = nullptr;

        if (!pageAtScenePos(scenePos, pageIndex, pageItem))
            return;

        // Map to page-local coordinates
        const QPointF pagePos = pageItem->mapFromScene(scenePos);
        fz_point pdfPos{float(pagePos.x()), float(pagePos.y())};

        if (synctex_edit_query(m_synctex_scanner, pageIndex + 1, pdfPos.x,
                               pdfPos.y)
            > 0)
        {
            synctex_node_p node;
            while ((node = synctex_scanner_next_result(m_synctex_scanner)))
                synctexLocateInDocument(synctex_node_get_name(node),
                                        synctex_node_line(node));
        }
        else
        {
            QMessageBox::warning(this, "SyncTeX Error",
                                 "No matching source found!");
        }
    }
    else
    {
        QMessageBox::warning(this, "SyncTex", "Not a valid synctex document");
    }
}
#endif

#ifdef HAS_SYNCTEX
void
DocumentView::synctexLocateInDocument(const char *texFileName,
                                      int line) noexcept
{
    QString tmp = m_config.behavior.synctex_editor_command;
    if (!tmp.contains("%f") || !tmp.contains("%l"))
    {
        QMessageBox::critical(this, "SyncTeX error",
                              "Invalid SyncTeX editor command: missing "
                              "placeholders (%l and/or %f).");
        return;
    }

    auto args   = QProcess::splitCommand(tmp);
    auto editor = args.takeFirst();
    args.replaceInStrings("%l", QString::number(line));
    args.replaceInStrings("%f", texFileName);

    QProcess::startDetached(editor, args);
}
#endif

void
DocumentView::handleTextHighlightRequested(QPointF start, QPointF end) noexcept
{
    if (!has_text_selection())
        return;

    int startP = m_selection_start_page;
    int endP   = m_selection_end_page;

    for (int p = startP; p <= endP; ++p)
    {
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        assert(item && "Page is not yet in the hash map");

        if (p == startP && p == endP)
        {
            m_model->highlight_text_selection(p, item->mapFromScene(start),
                                              item->mapFromScene(end));
        }
        else if (p == startP)
        {
            // From start point to END of page
            m_model->highlight_text_selection(
                p, item->mapFromScene(start),
                QPointF(item->boundingRect().bottomRight()));
        }
        else if (p == endP)
        {
            // From START of page to end point
            m_model->highlight_text_selection(p, QPointF(0, 0),
                                              item->mapFromScene(end));
        }
        else
        {
            // Full page
            m_model->highlight_text_selection(
                p, QPointF(0, 0), QPointF(item->boundingRect().bottomRight()));
        }
    }

    ClearTextSelection();
    setModified(true);
}

// Handle text selection from GraphicsView
void
DocumentView::handleTextSelection(QPointF start, QPointF end) noexcept
{
    int startPage                    = -1;
    int endPage                      = -1;
    GraphicsImageItem *startPageItem = nullptr;
    GraphicsImageItem *endPageItem   = nullptr;

    if (!pageAtScenePos(start, startPage, startPageItem)
        || !pageAtScenePos(end, endPage, endPageItem))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::handleTextSelection(): Handling text selection"
             << "from" << startPage << "to" << endPage;
#endif

    if (startPage > endPage)
    {
        std::swap(startPage, endPage);
        std::swap(start, end);
    }

    QPainterPath totalPath;

    for (int p = startPage; p <= endPage; ++p)
    {
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        assert(item && "Page is not yet in the hash map");
        QRectF bounds = item->boundingRect();

        // Define logical anchors based on the current visual rotation
        QPointF docStart, docEnd;
        switch ((int)m_model->rotation())
        {
            case 90:
                docStart = QPointF(bounds.width(), 0);  // Top-right
                docEnd   = QPointF(0, bounds.height()); // Bottom-left
                break;
            case 180:
                docStart = QPointF(bounds.width(),
                                   bounds.height()); // Bottom-right
                docEnd   = QPointF(0, 0);            // Top-left
                break;
            case 270:
                docStart = QPointF(0, bounds.height()); // Bottom-left
                docEnd   = QPointF(bounds.width(), 0);  // Top-right
                break;
            default:                      // 0
                docStart = QPointF(0, 0); // Top-left
                docEnd   = QPointF(bounds.width(),
                                   bounds.height()); // Bottom-right
                break;
        }

        std::vector<QPolygonF> quads;
        QPointF localStart = item->mapFromScene(start);
        QPointF localEnd   = item->mapFromScene(end);

        if (p == startPage && p == endPage)
        {
            quads = m_model->computeTextSelectionQuad(p, localStart, localEnd);
        }
        else if (p == startPage)
        {
            // From click point to the end of the document flow
            quads = m_model->computeTextSelectionQuad(p, localStart, docEnd);
        }
        else if (p == endPage)
        {
            // From the start of the document flow to the current cursor
            quads = m_model->computeTextSelectionQuad(p, docStart, localEnd);
        }
        else
        {
            // Full page selection
            quads = m_model->computeTextSelectionQuad(p, docStart, docEnd);
        }

        // std::vector<QPolygonF> quads;
        // if (p == startPage && p == endPage)
        // {
        //     // Single page selection (existing logic)
        //     quads = m_model->computeTextSelectionQuad(
        //         p, item->mapFromScene(start), item->mapFromScene(end));
        // }
        // else if (p == startPage)
        // {
        //     // Selection from start point to end of page
        //     quads = m_model->computeTextSelectionQuad(
        //         p, item->mapFromScene(start), QPointF(1e6, 1e6));
        //     // QPointF(item->boundingRect().bottomRight()));
        // }
        // else if (p == endPage)
        // {
        //     // Selection from start of page to end point
        //     quads = m_model->computeTextSelectionQuad(p, QPointF(0, 0),
        //                                               item->mapFromScene(end));
        // }
        // else
        // {
        //     // Full page selection
        //     quads = m_model->computeTextSelectionQuad(p, QPointF(0, 0),
        //                                               QPointF(1e6, 1e6));
        //     // QPointF(item->boundingRect().bottomRight()));
        // }

        const QTransform toScene = item->sceneTransform();
        for (const QPolygonF &poly : quads)
        {
            totalPath.addPolygon(toScene.map(poly));
        }
    }

    m_selection_path_item->setPath(totalPath);

    // add these two lines to keep metadata in sync
    m_selection_start = start;
    m_selection_end   = end;

    // Update metadata for copying/highlighting
    m_selection_start_page = startPage;
    m_selection_end_page   = endPage;

    m_selection_path_item->show();

    if (m_config.selection.copy_on_select)
    {
        YankSelection();
    }
}

// Rotate page clockwise
void
DocumentView::RotateClock() noexcept
{
    m_model->rotateClock();
    rotateHelper();
}

// Rotate page anticlockwise
void
DocumentView::RotateAnticlock() noexcept
{
    m_model->rotateAnticlock();
    rotateHelper();
}

void
DocumentView::rotateHelper() noexcept
{
    cachePageStride();
    const std::set<int> &trackedPages = getVisiblePages();

    if (trackedPages.empty())
        return;

    for (int pageno : trackedPages)
    {
        // m_model->invalidatePageCache(pageno);
        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);
    }

    renderPages();
}

// Cycle to the next fit mode
void
DocumentView::NextFitMode() noexcept
{
    FitMode nextMode = static_cast<FitMode>((static_cast<int>(m_fit_mode) + 1)
                                            % static_cast<int>(FitMode::COUNT));
    m_fit_mode       = nextMode;
    setFitMode(nextMode);
    fitModeChanged(nextMode);
}

// Cycle to the next selection mode
void
DocumentView::NextSelectionMode() noexcept
{
    GraphicsView::Mode nextMode = m_gview->getNextMode();
    m_gview->setMode(nextMode);
    emit selectionModeChanged(nextMode);
}

// Set the fit mode and adjust zoom accordingly
void
DocumentView::setFitMode(FitMode mode) noexcept
{
#ifndef NDEBUG
    qDebug() << "setFitMode(): Setting fit mode to:" << static_cast<int>(mode);
#endif

    m_fit_mode = mode;

    const auto pageDim = m_model->page_dimension_pts(m_pageno);

    const double baseW = (pageDim.width_pts / 72.0) * m_model->DPI();
    const double baseH = (pageDim.height_pts / 72.0) * m_model->DPI();
    double rot         = static_cast<double>(m_model->rotation());
    rot                = std::fmod(rot, 360.0);
    if (rot < 0)
        rot += 360.0;

    const double t     = deg2rad(rot);
    const double c     = std::abs(std::cos(t));
    const double s     = std::abs(std::sin(t));
    double bboxW       = baseW * c + baseH * s;
    const double bboxH = baseW * s + baseH * c;

    if (mode == FitMode::Width && m_layout_mode == LayoutMode::BOOK)
    {
        int leftP  = (m_pageno == 0)
                         ? 0
                         : ((m_pageno % 2 != 0) ? m_pageno : m_pageno - 1);
        int rightP = (m_pageno == 0) ? -1 : leftP + 1;

        auto getW = [&](int p)
        {
            if (p < 0 || p >= m_model->numPages())
                return 0.0;
            const auto dim = m_model->page_dimension_pts(p);
            return ((dim.width_pts / 72.0) * m_model->DPI()) * c
                   + ((dim.height_pts / 72.0) * m_model->DPI()) * s;
        };

        bboxW = getW(leftP) + getW(rightP);
        if (m_pageno == 0)
            bboxW *= 2.0; // Force cover zoom to respect the logical spine
                          // center
    }

    switch (mode)
    {
        case FitMode::Width:
        {
            const int viewWidth = m_gview->viewport()->width();

            // Calculate using the unzoomed page size so fit is absolute.
            const double newZoom = static_cast<double>(viewWidth) / bboxW;

            setZoom(newZoom);
        }
        break;

        case FitMode::Height:
        {
            const int viewHeight = m_gview->viewport()->height();
            const double newZoom = static_cast<double>(viewHeight) / bboxH;

            setZoom(newZoom);
        }
        break;

        case FitMode::Window:
        {
            const int viewWidth  = m_gview->viewport()->width();
            const int viewHeight = m_gview->viewport()->height();

            const double zoomX = static_cast<double>(viewWidth) / bboxW;
            const double zoomY = static_cast<double>(viewHeight) / bboxH;

            const double newZoom = std::min(zoomX, zoomY);

            setZoom(newZoom);
        }
        break;

        default:
            break;
    }

    GotoPage(m_pageno);
}

// Set zoom factor directly
void
DocumentView::setZoom(double factor) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::setZoom(): Setting zoom to factor:" << factor;
#endif

    factor = std::clamp(factor, MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);

    m_target_zoom  = factor;
    m_current_zoom = factor;

    cachePageStride();
    updateSceneRect();

    // 2. IMPORTANT: Invalidate the visibility cache so we don't
    // render pages that were visible at the PREVIOUS zoom level.
    invalidateVisiblePagesCache();

    GotoPage(m_pageno);
    renderPages();

    zoomHelper();
}

void
DocumentView::GotoLocation(const PageLocation &targetLocation) noexcept
{
    if (m_model->numPages() == 0)
        return;

    // HANDLE PENDING RENDERS
    if (!m_page_items_hash.contains(targetLocation.pageno))
    {
#ifndef NDEBUG
        qDebug() << "DocumentView::GotoLocation(): Target page"
                 << targetLocation.pageno
                 << "not yet rendered. Deferring jump until render.";
#endif
        m_pending_jump = targetLocation;
        GotoPage(targetLocation.pageno);
        return;
    }

#ifndef NDEBUG
    qDebug() << "DocumentView::GotoLocation(): Requested "
                "target location:"
             << targetLocation.pageno << targetLocation.x << targetLocation.y
             << "in document with" << m_model->numPages() << "pages.";
#endif

    // Continuous / LTR layouts
    GraphicsImageItem *pageItem = m_page_items_hash[targetLocation.pageno];
    if (!pageItem)
        return;
    if (pageItem->data(0).toString() == "placeholder_page")
    {
        m_pending_jump = targetLocation;
        GotoPage(targetLocation.pageno);
        return;
    }

    const QPointF targetPixelPos = m_model->toPixelSpace(
        targetLocation.pageno, {targetLocation.x, targetLocation.y});

    const QPointF scenePos = pageItem->mapToScene(targetPixelPos);

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        if (m_pageno != targetLocation.pageno)
            GotoPage(targetLocation.pageno);
    }

    m_gview->centerOn(scenePos);

    m_jump_marker->showAt(scenePos.x(), scenePos.y());
    m_old_jump_marker_pos = scenePos;
    m_pending_jump        = {-1, 0, 0};
}

namespace
{
bool
locationsEqual(const DocumentView::PageLocation &a,
               const DocumentView::PageLocation &b) noexcept
{
    return a.pageno == b.pageno && a.x == b.x && a.y == b.y;
}
} // namespace

void
DocumentView::GotoLocationWithHistory(
    const PageLocation &targetLocation) noexcept
{
    const PageLocation current = CurrentLocation();
    if (current.pageno != -1)
        addToHistory(current);

    addToHistory(targetLocation);
    GotoLocation(targetLocation);
}

void
DocumentView::GotoPageWithHistory(int pageno) noexcept
{
    const PageLocation current = CurrentLocation();
    if (current.pageno != -1)
        addToHistory(current);

    GotoPage(pageno);
    const PageLocation target = CurrentLocation();
    if (target.pageno != -1)
        addToHistory(target);
}

// Go to specific page number
// Does not render page directly, just adjusts scrollbar

/*
 * NOTE: You have to handle history saving yourself and is not handled
 * inside this function
 */
void
DocumentView::GotoPage(int pageno) noexcept
{
    if (pageno < 0 || pageno >= m_model->numPages())
        return;

    m_pageno = pageno;

    if (!m_visible_pages_cache.contains(pageno))
        invalidateVisiblePagesCache();

    emit currentPageChanged(pageno + 1);

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        renderPage();
    }
    else if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        // Center the view on the horizontal middle of the page
        const double x
            = pageOffset(pageno) + pageSceneSize(pageno).width() / 2.0;
        m_gview->centerOn(QPointF(x, m_gview->sceneRect().center().y()));
    }
    else
    {
        // Center on the spread
        const double y
            = pageOffset(pageno) + pageSceneSize(pageno).height() / 2.0;
        m_gview->centerOn(QPointF(m_gview->sceneRect().center().x(), y));
    }

    if (m_visual_line_mode)
    {
        // m_visual_lines      = m_model->get_text_lines(m_pageno);
        m_visual_line_index = -1;
        snap_visual_line();
    }
}

// Go to next page
void
DocumentView::GotoNextPage() noexcept
{
    if (m_pageno >= m_model->numPages() - 1)
        return;

    if (m_layout_mode == DocumentView::LayoutMode::BOOK)
    {
        int next = (m_pageno == 0) ? 1 : m_pageno + 2;
        GotoPage(std::min(next, m_model->numPages() - 1));
    }
    else
    {
        GotoPage(m_pageno + 1);
    }
}

void
DocumentView::GotoPrevPage() noexcept
{
    if (m_pageno == 0)
        return;

    if (m_layout_mode == DocumentView::LayoutMode::BOOK)
    {
        int prev = (m_pageno <= 2) ? 0 : m_pageno - 2;
        GotoPage(prev);
    }
    else
    {
        GotoPage(m_pageno - 1);
    }
}

void
DocumentView::clearSearchHits() noexcept
{
#ifndef NDEBUG
    qDebug()
        << "DocumentView::clearSearchHits(): Clearing previous search hits";
#endif
    for (auto *item : m_search_items)
    {
        if (item && item->scene() == m_gscene)
            item->setPath(QPainterPath()); // clear instead of delete
    }
    m_search_index = -1;
    m_search_items.clear();
    m_search_hits.clear();
    m_search_hit_flat_refs.clear();
    m_vscroll->setSearchMarkers({});
}

// Perform search for the given term
void
DocumentView::Search(const QString &term, bool useRegex) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::Search(): Searching for term:" << term;
#endif

    clearSearchHits();
    if (term.isEmpty())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    // Check if term has atleast one uppercase letter
    bool caseSensitive = std::any_of(term.cbegin(), term.cend(),
                                     [](QChar c) { return c.isUpper(); });

    emit searchBarSpinnerShow(true);
    m_model->search(term, caseSensitive, useRegex);
}

void
DocumentView::SearchInPage(const int pageno, const QString &term) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::SearchInPage(): Searching page: " << pageno
             << " for term: " << term;
#endif

    clearSearchHits();
    if (term.isEmpty())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    emit searchBarSpinnerShow(true);
    // Check if term has atleast one uppercase letter
    bool caseSensitive = std::any_of(term.cbegin(), term.cend(),
                                     [](QChar c) { return c.isUpper(); });

    // m_search_hits = m_model->search(term);
    m_model->searchInPage(pageno, term, caseSensitive);
}

// Zoom in by a fixed factor
void
DocumentView::ZoomIn() noexcept
{
    if (m_target_zoom >= MAX_ZOOM_FACTOR)
        return;

    m_target_zoom = std::clamp(m_target_zoom * m_config.zoom.factor,
                               MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);
    zoomHelper();
}

// Zoom out by a fixed factor
void
DocumentView::ZoomOut() noexcept
{
    if (m_target_zoom <= MIN_ZOOM_FACTOR)
        return;

    m_target_zoom = std::clamp(m_current_zoom / m_config.zoom.factor,
                               MIN_ZOOM_FACTOR, MAX_ZOOM_FACTOR);
    zoomHelper();
}

// Reset zoom to 100%
void
DocumentView::ZoomReset() noexcept
{
    m_current_zoom = 1.0f;
    m_target_zoom  = 1.0f;
    zoomHelper();
}

// Navigate to the next search hit
void
DocumentView::NextHit() noexcept
{
    GotoHit(m_search_index + 1);
}

// Navigate to the previous search hit
void
DocumentView::PrevHit() noexcept
{
    GotoHit(m_search_index - 1);
}

// Navigate to a specific search hit by index
void
DocumentView::GotoHit(int index) noexcept
{
    if (m_search_hit_flat_refs.empty())
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::GotoHit(): Going to search hit index:" << index;
#endif

    if (index < 0)
        index = static_cast<int>(m_search_hit_flat_refs.size()) - 1;
    else if (index >= static_cast<int>(m_search_hit_flat_refs.size()))
        index = 0;

    const HitRef ref  = m_search_hit_flat_refs[index];
    m_search_index    = index;
    m_pageno          = ref.page;
    const auto &hit   = m_search_hits[ref.page][ref.indexInPage];
    const float scale = m_model->logicalScale();

    emit searchIndexChanged(index);
    emit currentPageChanged(ref.page + 1);

    // Compute hit centre in scene coordinates directly from cached offsets.
    const double hitX = (hit.quad.ul.x + hit.quad.ur.x) * scale / 2.0;
    const double hitY = (hit.quad.ul.y + hit.quad.ll.y) * scale / 2.0;

    QPointF scenePos;
    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        scenePos
            = QPointF(pageOffset(ref.page) + hitX,
                      pageXOffset(ref.page, pageSceneSize(ref.page).width(),
                                  m_gview->sceneRect().width())
                          + hitY);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        const QRectF sr = m_gview->sceneRect();
        // Ensure the hit is calculated relative to the centered page
        // position
        scenePos = QPointF(
            sr.left() + (sr.width() - pageSceneSize(ref.page).width()) / 2.0
                + hitX,
            sr.top() + (sr.height() - pageSceneSize(ref.page).height()) / 2.0
                + hitY);
    }
    // else if (m_layout_mode == LayoutMode::SINGLE)
    // {
    //     const QRectF sr = m_gview->sceneRect();
    //     scenePos        = QPointF(
    //         sr.x() + (sr.width() - pageSceneSize(ref.page).width()) / 2.0
    //             + hitX,
    //         sr.y() + (sr.height() - pageSceneSize(ref.page).height())
    //         / 2.0
    //             + hitY);
    // }
    else // TOP_TO_BOTTOM, BOOK
    {
        scenePos
            = QPointF(pageXOffset(ref.page, pageSceneSize(ref.page).width(),
                                  m_gview->sceneRect().width())
                          + hitX,
                      pageOffset(ref.page) + hitY);
    }

    m_scroll_to_hit_pending = true;

    m_scroll_page_update_timer->stop();
    m_hq_render_timer->stop();

    m_gview->centerOn(scenePos);

    // If the page is already rendered, the render callback won't reliably
    // fire for this hit — update the highlight immediately.
    if (m_page_items_hash.contains(ref.page)
        && m_page_items_hash[ref.page]->data(0).toString()
               != "placeholder_page")
    {
        m_scroll_to_hit_pending = false;
        updateCurrentHitHighlight();
    }

    if (m_layout_mode == LayoutMode::SINGLE)
        renderPage();
    else
        renderPages();
}

// Scroll left by a fixed amount
void
DocumentView::ScrollLeft() noexcept
{
    if (m_visual_line_mode)
    {
        visual_line_move(Direction::LEFT);
    }
    else
    {
        m_hscroll->setUpdatesEnabled(false);
        m_hscroll->setValue(m_hscroll->value() - 50);
        m_hscroll->setUpdatesEnabled(true);
    }
}

// Scroll right by a fixed amount
void
DocumentView::ScrollRight() noexcept
{
    if (m_visual_line_mode)
    {
        visual_line_move(Direction::RIGHT);
    }
    else
    {
        m_hscroll->setUpdatesEnabled(false);
        m_hscroll->setValue(m_hscroll->value() + 50);
        m_hscroll->setUpdatesEnabled(true);
    }
}

// Scroll up by a fixed amount
void
DocumentView::ScrollUp() noexcept
{
    if (m_visual_line_mode)
    {
        visual_line_move(Direction::UP);
    }
    else
    {
        m_vscroll->setUpdatesEnabled(false);
        m_vscroll->setValue(m_vscroll->value() - 50);
        m_vscroll->setUpdatesEnabled(true);
    }
}

// Scroll down by a fixed amount
void
DocumentView::ScrollDown() noexcept
{
    if (m_visual_line_mode)
    {
        visual_line_move(Direction::DOWN);
    }
    else
    {
        m_vscroll->setUpdatesEnabled(false);
        m_vscroll->setValue(m_vscroll->value() + 50);
        m_vscroll->setUpdatesEnabled(true);
    }
}

// Get the link KB for the current document
QMap<int, Model::LinkInfo>
DocumentView::LinkKB() noexcept
{
    QMap<int, Model::LinkInfo> hintMap;

    if (!m_gscene)
        return hintMap;

    ClearKBHintsOverlay();

    const QRectF visibleSceneRect
        = m_gview->mapToScene(m_gview->viewport()->rect()).boundingRect();

    std::vector<std::pair<BrowseLinkItem *, int>> visibleLinks;
    const std::set<int> &visiblePages = getVisiblePages();
    for (int pageno : visiblePages)
    {
        if (!m_page_links_hash.contains(pageno))
            continue;

        const auto &links = m_page_links_hash.value(pageno);
        for (auto *link : links)
        {
            if (!link || link->scene() != m_gscene)
                continue;

            const QRectF linkRect = link->sceneBoundingRect();
            if (!linkRect.intersects(visibleSceneRect))
                continue;

            visibleLinks.push_back({link, pageno});
        }
    }

    if (visibleLinks.empty())
        return hintMap;

    int hint = 1;
    if (visibleLinks.size() > 9)
    {
        int digits = QString::number(visibleLinks.size()).size();
        hint       = 1;
        for (int i = 1; i < digits; ++i)
            hint *= 10;
    }

    float fontSize = m_config.link_hints.size;
    if (fontSize < 1.0f)
        fontSize = std::max(8.0f, fontSize * 32.0f);

    QFont font;
    font.setPointSizeF(fontSize);
    QFontMetricsF metrics(font);

    const QColor bg = rgbaToQColor(m_config.colors.link_hint_bg);
    const QColor fg = rgbaToQColor(m_config.colors.link_hint_fg);

    for (const auto &entry : visibleLinks)
    {
        BrowseLinkItem *link = entry.first;
        const int pageno     = entry.second;

        const QString hintText = QString::number(hint);
        const QRectF textRect  = metrics.boundingRect(hintText);
        const qreal padding    = 4.0;
        const QSizeF hintSize(textRect.width() + padding * 2.0,
                              textRect.height() + padding * 2.0);

        QPointF hintPos = link->sceneBoundingRect().topLeft() + QPointF(2, 2);
        if (hintPos.x() + hintSize.width() > visibleSceneRect.right())
            hintPos.setX(visibleSceneRect.right() - hintSize.width());
        if (hintPos.y() + hintSize.height() > visibleSceneRect.bottom())
            hintPos.setY(visibleSceneRect.bottom() - hintSize.height());
        if (hintPos.x() < visibleSceneRect.left())
            hintPos.setX(visibleSceneRect.left());
        if (hintPos.y() < visibleSceneRect.top())
            hintPos.setY(visibleSceneRect.top());

        LinkHint *hintItem
            = new LinkHint(QRectF(hintPos, hintSize), bg, fg, hint, fontSize);
        hintItem->setZValue(ZVALUE_KB_LINK_OVERLAY);
        m_gscene->addItem(hintItem);

        Model::LinkInfo info;
        info.uri         = link->link();
        info.dest        = fz_make_link_dest_none();
        info.type        = link->linkType();
        info.target_page = link->gotoPageNo();
        info.target_loc  = link->location();
        info.source_loc  = link->sourceLocation();
        info.source_page = pageno;
        hintMap.insert(hint, info);

        ++hint;

        m_kb_link_hints.push_back(hintItem);
    }

    return hintMap;
}

void
DocumentView::FollowLink(const Model::LinkInfo &info) noexcept
{
    switch (info.type)
    {
        case BrowseLinkItem::LinkType::External:
            if (!info.uri.isEmpty())
                QDesktopServices::openUrl(QUrl(info.uri));
            break;

        case BrowseLinkItem::LinkType::FitH:
            if (info.target_page >= 0)
            {
                PageLocation target{info.target_page, info.target_loc.x,
                                    info.target_loc.y};
                if (std::isnan(target.x))
                    target.x = 0;
                if (std::isnan(target.y))
                    target.y = 0;
                addToHistory(
                    {info.source_page, info.source_loc.x, info.source_loc.y});
                addToHistory(target);
                GotoLocation(target);
                setFitMode(FitMode::Width);
            }
            break;

        case BrowseLinkItem::LinkType::FitV:
            if (info.target_page >= 0)
            {
                PageLocation target{info.target_page, info.target_loc.x,
                                    info.target_loc.y};
                if (std::isnan(target.x))
                    target.x = 0;
                if (std::isnan(target.y))
                    target.y = 0;
                addToHistory(
                    {info.source_page, info.source_loc.x, info.source_loc.y});
                addToHistory(target);
                GotoLocation(target);
                setFitMode(FitMode::Height);
            }
            break;

        case BrowseLinkItem::LinkType::Page:
            if (info.target_page >= 0)
            {
                PageLocation target{info.target_page, 0, 0};
                addToHistory(
                    {info.source_page, info.source_loc.x, info.source_loc.y});
                addToHistory(target);
                GotoLocation(target);
            }
            break;

        case BrowseLinkItem::LinkType::Section:
        case BrowseLinkItem::LinkType::Location:
            if (info.target_page >= 0)
            {
                PageLocation target{info.target_page, info.target_loc.x,
                                    info.target_loc.y};
                if (std::isnan(target.x))
                    target.x = 0;
                if (std::isnan(target.y))
                    target.y = 0;
                addToHistory(
                    {info.source_page, info.source_loc.x, info.source_loc.y});
                addToHistory(target);
                GotoLocation(target);
            }
            break;
    }
}

// Show file properties dialog
void
DocumentView::FileProperties() noexcept
{
    if (!m_model->success())
        return;

    PropertiesWidget *propsWidget = new PropertiesWidget(this);
    const auto props              = m_model->properties();
    propsWidget->setProperties(props);
    propsWidget->exec();
}

// Save the current file
void
DocumentView::SaveFile() noexcept
{
    if (!m_model->hasUnsavedChanges())
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::SaveFile(): Saving file with unsaved changes.";
#endif

    stopPendingRenders();
    if (m_model->SaveChanges())
    {
        m_cancelled->store(false);
        clearDocumentItems();
        cachePageStride();
        updateSceneRect();
        renderPages();
        setModified(false);
    }
    else
    {
        QMessageBox::critical(
            this, "Saving failed",
            "Could not save the current file. Try 'Save As' instead.");
        m_cancelled->store(false);
    }
}

// Save the current file as a new file
void
DocumentView::SaveAsFile() noexcept
{
    const QString filename
        = QFileDialog::getSaveFileName(this, "Save as", QString());

    if (filename.isEmpty())
        return;

    if (!m_model->SaveAs(filename))
    {
        QMessageBox::critical(
            this, "Saving as failed",
            "Could not perform save as operation on the file");
    }
}

// Close the current file
void
DocumentView::CloseFile() noexcept
{
    clearDocumentItems();
    resetConnections();
    m_model->close();
}

// Toggle auto-resize mode
void
DocumentView::ToggleAutoResize() noexcept
{
    m_auto_resize = !m_auto_resize;
}

// Toggle text highlight mode
void
DocumentView::ToggleTextHighlight() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::TextHighlight)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::TextHighlight;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

void
DocumentView::ToggleTextSelection() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::TextSelection)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::TextSelection;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle region selection mode
void
DocumentView::ToggleRegionSelect() noexcept
{
    const auto newMode
        = (m_gview->mode() == GraphicsView::Mode::RegionSelection)
              ? m_gview->getDefaultMode()
              : GraphicsView::Mode::RegionSelection;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle annotation rectangle mode
void
DocumentView::ToggleAnnotRect() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::AnnotRect)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::AnnotRect;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle annotation selection mode
void
DocumentView::ToggleAnnotSelect() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::AnnotSelect)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::AnnotSelect;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Toggle annotation popup mode
void
DocumentView::ToggleAnnotPopup() noexcept
{
    const auto newMode = (m_gview->mode() == GraphicsView::Mode::AnnotPopup)
                             ? m_gview->getDefaultMode()
                             : GraphicsView::Mode::AnnotPopup;

    m_gview->setMode(newMode);
    emit selectionModeChanged(newMode);
}

// Clear keyboard hints overlay
void
DocumentView::ClearKBHintsOverlay() noexcept
{
    if (!m_gscene)
        return;

    for (auto *hint : m_kb_link_hints)
    {
        m_gscene->removeItem(hint);
        delete hint;
    }

    m_kb_link_hints.clear();
}

void
DocumentView::UpdateKBHintsOverlay(const QString &input) noexcept
{
    if (!m_gscene)
        return;

    for (auto *hint : m_kb_link_hints)
    {
        if (auto *hintItem = qgraphicsitem_cast<LinkHint *>(hint))
            hintItem->setInputPrefix(input);
    }
}

// Clear the current text selection
void
DocumentView::ClearTextSelection() noexcept
{
    if (!has_text_selection())
        return;

#ifndef NDEBUG
    qDebug() << "ClearTextSelection(): Clearing text selection";
#endif

    if (m_selection_path_item)
    {
        m_selection_path_item->setPath(QPainterPath());
        m_selection_path_item->hide();
    }

    m_selection_start = QPointF();
    m_selection_end   = QPointF();

    m_selection_start_page = -1;
    m_selection_end_page   = -1;
}

// Yank the current text selection to clipboard
void
DocumentView::YankSelection(bool formatted) noexcept
{
    if (!has_text_selection())
        return;

    QString fullText;

    // Copy the state so we can normalize it safely
    QPointF start = m_selection_start;
    QPointF end   = m_selection_end;
    int startP    = m_selection_start_page;
    int endP      = m_selection_end_page;

    for (int p = startP; p <= endP; ++p)
    {
        GraphicsImageItem *item = m_page_items_hash.value(p, nullptr);
        assert(item && "Page is not yet in the hash map");

        QString text;
        if (p == startP && p == endP)
        {
            text = m_model->get_selected_text(p, item->mapFromScene(start),
                                              item->mapFromScene(end),
                                              formatted);
        }
        else if (p == startP)
        {
            // From start point to END of page
            text = m_model->get_selected_text(
                p, item->mapFromScene(start),
                QPointF(item->boundingRect().bottomRight()), formatted);
        }
        else if (p == endP)
        {
            // From START of page to end point
            text = m_model->get_selected_text(
                p, QPointF(0, 0), item->mapFromScene(end), formatted);
        }
        else
        {
            // Full page
            text = m_model->get_selected_text(
                p, QPointF(0, 0), QPointF(item->boundingRect().bottomRight()),
                formatted);
        }

        fullText += text;

        // Add a newline between pages to prevent text merging
        if (p < endP && !text.isEmpty())
            fullText += "\n";
    }

    QGuiApplication::clipboard()->setText(fullText);
}

// Go to the first page
void
DocumentView::GotoFirstPage() noexcept
{
    GotoPageWithHistory(0);
    m_vscroll->setValue(0);
}
// Go to the last page
void
DocumentView::GotoLastPage() noexcept
{
    GotoPageWithHistory(m_model->numPages() - 1);
    m_vscroll->setValue(m_vscroll->maximum());
}

// Go back in history
void
DocumentView::GoBackHistory() noexcept
{
    if (m_loc_history_index <= 0
        || m_loc_history_index >= (int)m_loc_history.size())
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::GoBackHistory(): Going back in history";
#endif

    m_loc_history_index -= 1;
    const PageLocation target = m_loc_history[m_loc_history_index];
    GotoLocation(target);
}

// Go forward in history
void
DocumentView::GoForwardHistory() noexcept
{
    if (m_loc_history_index < 0
        || m_loc_history_index + 1 >= (int)m_loc_history.size())
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::GoForwardHistory(): Going forward in history";
#endif

    m_loc_history_index += 1;
    const PageLocation target = m_loc_history[m_loc_history_index];
    GotoLocation(target);
}

const std::set<int> &
DocumentView::getVisiblePages() noexcept
{
    if (!m_visible_pages_dirty)
        return m_visible_pages_cache;

    m_visible_pages_cache.clear();

    if (m_model->numPages() == 0
        || m_page_offsets.size() < static_cast<size_t>(m_model->numPages() + 1))
    {
        m_visible_pages_dirty = false;
        return m_visible_pages_cache;
    }

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        m_visible_pages_cache.insert(
            std::clamp(m_pageno, 0, m_model->numPages() - 1));
        m_visible_pages_dirty = false;
        return m_visible_pages_cache;
    }

    const QRectF visibleSceneRect
        = m_gview->mapToScene(m_gview->viewport()->rect()).boundingRect();

    double a0, a1;
    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        a0 = visibleSceneRect.left();
        a1 = visibleSceneRect.right();
    }
    else
    {
        a0 = visibleSceneRect.top();
        a1 = visibleSceneRect.bottom();
    }

    const int N = m_model->numPages();

    if (m_layout_mode == LayoutMode::BOOK)
    {
        // Iterate by ROW to avoid problems with duplicate offsets in
        // spreads. Row 0 = page 0 (cover), Row 1 = pages 1-2, Row 2 = pages
        // 3-4, etc.
        for (int i = 0; i < N;)
        {
            const double rowStart = m_page_offsets[i];

            int rowEnd_idx;
            if (i == 0)
                rowEnd_idx = 1; // cover is alone
            else
                rowEnd_idx = std::min(i + 2, N); // spread pair

            const double rowEnd
                = (rowEnd_idx < N)
                      ? m_page_offsets[rowEnd_idx]
                      : m_page_offsets[N]; // use sentinel for last row

            // Overlap test: rowStart < a1 && rowEnd > a0
            if (rowStart < a1 && rowEnd > a0)
            {
                // Add all pages in this row
                for (int p = i; p < rowEnd_idx; ++p)
                    m_visible_pages_cache.insert(p);
            }

            // Early exit: if this row starts beyond viewport, no more
            // visible
            if (rowStart >= a1)
                break;

            // Advance to next row
            if (i == 0)
                i = 1;
            else
                i += 2;
        }
    }
    else
    {
        // (offsets are strictly increasing, binary search is safe)
        auto it_last = std::lower_bound(m_page_offsets.begin(),
                                        m_page_offsets.end(), a1);

        auto it_first = std::upper_bound(m_page_offsets.begin(),
                                         m_page_offsets.end(), a0);
        if (it_first != m_page_offsets.begin())
            --it_first;

        int firstPage = std::max(0, static_cast<int>(std::distance(
                                        m_page_offsets.begin(), it_first)));
        int lastPage  = std::max(
            0, static_cast<int>(std::distance(m_page_offsets.begin(), it_last))
                   - 1);

        firstPage = std::clamp(firstPage, 0, N - 1);
        lastPage  = std::clamp(lastPage, 0, N - 1);

        const double spacingScene = m_spacing * m_current_zoom;

        for (int pageno = firstPage; pageno <= lastPage; ++pageno)
        {
            double pageStart = m_page_offsets[pageno];
            double pageEnd   = (pageno + 1 < (int)m_page_offsets.size())
                                   ? m_page_offsets[pageno + 1] - spacingScene
                                   : pageStart + pageStride(pageno);

            if (pageEnd > a0 && pageStart < a1)
                m_visible_pages_cache.insert(pageno);
        }
    }

    m_visible_pages_dirty = false;
    return m_visible_pages_cache;
}

void
DocumentView::invalidateVisiblePagesCache() noexcept
{
    m_visible_pages_dirty = true;
}

// Clear links for a specific page
void
DocumentView::clearLinksForPage(int pageno) noexcept
{
    if (!m_page_links_hash.contains(pageno))
        return;

    auto links = m_page_links_hash.take(pageno); // removes from hash
    for (auto *link : links)
    {
        if (!link)
            continue;

        // Remove from scene if still present
        if (link->scene() == m_gscene)
            m_gscene->removeItem(link);

        delete link; // safe: we "own" these
    }
}

void
DocumentView::clearSearchItemsForPage(int pageno) noexcept
{
    if (!m_search_items.contains(pageno))
        return;

    QGraphicsPathItem *item
        = m_search_items.take(pageno); // removes item from hash
    if (item)
    {
        if (item->scene() == m_gscene)
            m_gscene->removeItem(item);
        delete item;
    }
}

// Clear links for a specific page
void
DocumentView::clearAnnotationsForPage(int pageno) noexcept
{
    if (!m_page_annotations_hash.contains(pageno))
        return;

    auto annotations
        = m_page_annotations_hash.take(pageno); // removes from hash
    for (auto *annotation : annotations)
    {
        if (!annotation)
            continue;

        // Remove from scene if still present
        if (annotation->scene() == m_gscene)
            m_gscene->removeItem(annotation);

        delete annotation; // safe: we "own" these
    }
}

// Render all visible pages
void
DocumentView::renderPages() noexcept
{
    // Guard
    if (m_layout_mode == LayoutMode::SINGLE)
    {
        renderPage();
        return;
    }

    const std::set<int> &visiblePages = getVisiblePages();
    const std::set<int> preloadPages  = getPreloadPages();

    std::set<int> pages = visiblePages;
    pages.insert(preloadPages.begin(), preloadPages.end());

#ifndef NDEBUG
    qDebug() << "DocumentView::renderPages(): Rendering pages:" << pages;
#endif

    m_gview->setUpdatesEnabled(false);
    m_gscene->blockSignals(true);
    {
        prunePendingRenders(pages);
        removeUnusedPageItems(pages);

        // Prioritize visible pages for rendering, but also include preload
        // pages in the queue
        for (int pageno : visiblePages)
            requestPageRender(pageno);

        // Preload pages
        for (int pageno : preloadPages)
            requestPageRender(pageno);

        updateSceneRect();
    }
    m_gscene->blockSignals(false);
    m_gview->setUpdatesEnabled(true);

    updateCurrentHitHighlight();

    if (m_visual_line_mode)
    {
        snap_visual_line(false);
    }
}

// Render a specific page (used when LayoutMode is SINGLE)
void
DocumentView::renderPage() noexcept
{
    m_gview->setUpdatesEnabled(false);
    m_gscene->blockSignals(true);
    {
        prunePendingRenders({m_pageno});
        removeUnusedPageItems({m_pageno});

        // Promote preload item to visible if available — instant display
        if (m_page_items_hash.contains(m_pageno))
        {
            GraphicsImageItem *item = m_page_items_hash[m_pageno];
            if (item->data(0).toString() == QStringLiteral("preload_page"))
            {
                item->setData(0, QVariant()); // clear tag
                item->show();
                updateSceneRect();
                m_gscene->blockSignals(false);
                m_gview->setUpdatesEnabled(true);
                updateCurrentHitHighlight();
                // Still request a fresh render in case zoom changed etc,
                // but the preload gives instant feedback
                requestPageRender(m_pageno);
                return;
            }
        }

        requestPageRender(m_pageno);
        updateSceneRect();
    }
    m_gscene->blockSignals(false);
    m_gview->setUpdatesEnabled(true);

    updateCurrentHitHighlight();
}

void
DocumentView::startNextRenderJob() noexcept
{
    // Get current visible pages for prioritization
    const std::set<int> &visiblePages = getVisiblePages();

    while (m_renders_in_flight < MAX_CONCURRENT_RENDERS
           && !m_render_queue.isEmpty())
    {
        // Prioritize visible pages first
        int pageno = -1;
        for (int i = 0; i < m_render_queue.size(); ++i)
        {
            int candidate = m_render_queue[i];
            if (visiblePages.contains(candidate))
            {
                pageno = candidate;
                m_render_queue.removeAt(i);
                break;
            }
        }

        // If no visible pages in queue, take the next one
        if (pageno == -1)
            pageno = m_render_queue.dequeue();

        if (!m_pending_renders.contains(pageno))
            continue;

        // pageno = m_render_queue.dequeue();
        // if (!m_pending_renders.contains(pageno))
        //     continue;
        //
        ++m_renders_in_flight;
        auto job = m_model->createRenderJob(pageno);

        auto cancelled = m_cancelled; // shared_ptr copy

        m_model->requestPageRender(
            job,
            [this, pageno, cancelled](const Model::PageRenderResult &result)
        {
            --m_renders_in_flight;
            m_pending_renders.remove(pageno);

            if (cancelled->load())
                return;

            // Use QImage directly - avoid expensive QPixmap::fromImage()
            // conversion
            const QImage &image = result.image;

            if (!image.isNull())
            {
                if (m_layout_mode == LayoutMode::SINGLE && pageno != m_pageno)
                {
                    // Store as a hidden preload item in the scene for
                    // instant display later
                    m_gscene->blockSignals(true);
                    setUpdatesEnabled(false);
                    {
                        renderPageFromImage(pageno, image);
                        // tag it as preload and hide it
                        if (m_page_items_hash.contains(pageno))
                        {
                            m_page_items_hash[pageno]->setData(
                                0, QStringLiteral("preload_page"));
                            m_page_items_hash[pageno]->hide();
                        }
                    }
                    setUpdatesEnabled(true);
                    renderLinks(pageno, result.links);
                    m_gscene->blockSignals(false);
                    startNextRenderJob();
                    return;
                }

                m_gscene->blockSignals(true);
                setUpdatesEnabled(false);
                {
                    renderPageFromImage(pageno, image);
                    renderLinks(pageno, result.links);
                    renderAnnotations(pageno, result.annotations);
                    renderSearchHitsForPage(pageno);
                }
                setUpdatesEnabled(true);
                m_gscene->blockSignals(false);
                // m_gview->viewport()->update();

                if (m_pending_jump.pageno == pageno)
                    GotoLocation(m_pending_jump);

                if (m_scroll_to_hit_pending && m_search_index >= 0
                    && !m_search_hit_flat_refs.empty()
                    && m_search_hit_flat_refs[m_search_index].page == pageno)
                {
                    m_scroll_to_hit_pending = false;
                    updateCurrentHitHighlight();
                    // Stop timers before centerOn so the scroll signal it
                    // generates doesn't trigger another renderPages.
                    m_scroll_page_update_timer->stop();
                    m_hq_render_timer->stop();
                    scrollToCurrentHit();
                }
            }

            startNextRenderJob();
        });
    }
}

// Remove pending renders for pages that are no longer visible and not
// in-flight
void
DocumentView::prunePendingRenders(const std::set<int> &visiblePages) noexcept
{
    for (auto it = m_pending_renders.begin(); it != m_pending_renders.end();)
        it = visiblePages.count(*it) ? ++it : m_pending_renders.erase(it);

    if (m_render_queue.isEmpty())
        return;

    QQueue<int> filtered;
    while (!m_render_queue.isEmpty())
    {
        const int pageno = m_render_queue.dequeue();
        if (visiblePages.contains(pageno))
            filtered.enqueue(pageno);
    }
    m_render_queue = std::move(filtered);
}

// void
// DocumentView::removeUnusedPageItems(const std::set<int> &visibleSet)
// noexcept
// {
//     // Copy keys first to avoid iterator invalidation
//     const QList<int> trackedPages = m_page_items_hash.keys();
//     for (int pageno : trackedPages)
//     {
//         if (visibleSet.count(pageno))
//             continue;
//
//         clearLinksForPage(pageno);
//         clearAnnotationsForPage(pageno);
//         clearSearchItemsForPage(pageno);
//
//         auto *item = m_page_items_hash.take(pageno);
//         if (item)
//         {
//             if (item->scene() == m_gscene)
//                 m_gscene->removeItem(item);
//             delete item;
//         }
//     }
// }

void
DocumentView::removeUnusedPageItems(const std::set<int> &visibleSet) noexcept
{
    // Copy keys first to avoid iterator invalidation
    const QList<int> trackedPages = m_page_items_hash.keys();
    for (int pageno : trackedPages)
    {
        if (visibleSet.count(pageno))
            continue;

        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);

        auto *item = m_page_items_hash.value(pageno, nullptr);
        if (!item)
            continue;

        const QString tag = item->data(0).toString();

        // Keep placeholders to avoid flicker during fast scroll; only
        // remove actual rendered pages. For placeholders, just hide them so
        // they don't cause repaints but remain ready to be replaced when
        // rendering finishes.
        if (tag == "placeholder_page" || tag == "scroll_placeholder")
        {
            if (item->scene() == m_gscene)
                item->hide();
            continue;
        }

        // Remove and delete real page item
        m_page_items_hash.remove(pageno);
        if (item->scene() == m_gscene)
            m_gscene->removeItem(item);
        delete item;
    }
}

// Remove a page item from the scene and delete it
void
DocumentView::removePageItem(int pageno) noexcept
{
    if (m_page_items_hash.contains(pageno))
    {
        GraphicsImageItem *item = m_page_items_hash.take(pageno);
        if (item->scene() == m_gscene)
            m_gscene->removeItem(item);
        delete item;
    }
}

void
DocumentView::cachePageStride() noexcept
{
    const int N = m_model->numPages();
    if (N <= 0)
        return;

    m_page_offsets.resize(N + 1);

    const double spacingScene = m_spacing * m_current_zoom;
    const double rot          = std::fmod(std::abs(m_model->rotation()), 360.0);
    const bool rotated        = (rot == 90.0 || rot == 270.0);

    // Helper to get extent quickly
    auto getExtents = [&](int p, double &w, double &h)
    {
        const auto dim = m_model->page_dimension_pts(p);
        w = (dim.width_pts / 72.0) * m_model->DPI() * m_current_zoom;
        h = (dim.height_pts / 72.0) * m_model->DPI() * m_current_zoom;
        if (rotated)
            std::swap(w, h);
    };

    double cursor   = 0.0;
    double maxCross = 0.0;

    if (m_layout_mode == LayoutMode::BOOK)
    {
        for (int i = 0; i < N;)
        {
            if (i == 0)
            { // Cover
                double w, h;
                getExtents(i, w, h);
                m_page_offsets[i] = cursor;
                maxCross          = std::max(maxCross, w * 2.0);
                cursor += h + spacingScene;
                i++;
            }
            else
            {
                // Spreads
                double w1, h1, w2 = 0, h2 = 0;
                getExtents(i, w1, h1);
                if (i + 1 < N)
                    getExtents(i + 1, w2, h2);

                m_page_offsets[i] = cursor;
                if (i + 1 < N)
                    m_page_offsets[i + 1] = cursor;

                maxCross = std::max(maxCross, w1 + w2);
                cursor += std::max(h1, h2) + spacingScene;
                i += 2;
            }
        }
    }
    else
    {
        const bool horizontal = (m_layout_mode == LayoutMode::LEFT_TO_RIGHT);
        for (int i = 0; i < N; ++i)
        {
            m_page_offsets[i] = cursor;
            double w, h;
            getExtents(i, w, h);
            cursor += (horizontal ? w : h) + spacingScene;
            maxCross = std::max(maxCross, horizontal ? h : w); // <-- add this
        }
    }

    m_page_offsets[N]       = cursor;
    m_max_page_cross_extent = maxCross;
    invalidateVisiblePagesCache();
}

std::set<int>
DocumentView::getPreloadPages() noexcept
{
    const std::set<int> &visiblePages = getVisiblePages();
    if (visiblePages.empty())
        return {};

    const int numPages     = m_model->numPages();
    const int firstVisible = *visiblePages.begin();
    const int lastVisible  = *visiblePages.rbegin();

    // Use the stride of the current page as the preload lookahead distance
    const double preloadDistance
        = pageStride(std::clamp(m_pageno, 0, numPages - 1))
          * m_config.behavior.preload_pages;

    const double ahead  = m_page_offsets[lastVisible] + preloadDistance;
    const double behind = m_page_offsets[firstVisible] - preloadDistance;

    std::set<int> preloadPages;

    for (int p = firstVisible - 1; p >= 0; --p)
    {
        if (m_page_offsets[p] < behind)
            break;
        preloadPages.insert(p);
    }

    for (int p = lastVisible + 1; p < numPages; ++p)
    {
        if (m_page_offsets[p] > ahead)
            break;
        preloadPages.insert(p);
    }

    return preloadPages;
}

// Update the scene rect based on number of pages and page stride
void
DocumentView::updateSceneRect() noexcept
{
    const double viewW = m_gview->viewport()->width();
    const double viewH = m_gview->viewport()->height();

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        const QSizeF page    = pageSceneSize(m_pageno);
        const double xMargin = std::max(0.0, (viewW - page.width()) / 2.0);
        const double yMargin = std::max(0.0, (viewH - page.height()) / 2.0);
        const double sceneW  = std::max(viewW, page.width());
        const double sceneH  = std::max(viewH, page.height());
        m_gview->setSceneRect(-xMargin, -yMargin, sceneW, sceneH);
    }
    else if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const double totalWidth = totalPageExtent();
        const double sceneH     = std::max(viewH, m_max_page_cross_extent);
        const double xMargin    = std::max(0.0, (viewW - totalWidth) / 2.0);
        // yMargin relative to current page so scrolling to a shorter page
        // doesn't shift content
        const double yMargin
            = std::max(0.0, (viewH - pageSceneSize(m_pageno).height()) / 2.0);
        m_gview->setSceneRect(-xMargin, -yMargin, totalWidth + 2.0 * xMargin,
                              sceneH);
    }
    else if (m_layout_mode == LayoutMode::BOOK)
    {
        const double totalHeight  = totalPageExtent();
        const double sceneW       = std::max(viewW, m_max_page_cross_extent);
        const double cappedSceneW = std::min(sceneW, 20000.0);
        const double yMargin
            = std::max(0.0, (viewH - pageSceneSize(m_pageno).height()) / 2.0);
        m_gview->setSceneRect(0, -yMargin, cappedSceneW,
                              totalHeight + 2.0 * yMargin);
    }
    else
    { // TOP_TO_BOTTOM
        const double totalHeight = totalPageExtent();
        const double sceneW      = std::max(viewW, m_max_page_cross_extent);
        const double yMargin
            = std::max(0.0, (viewH - pageSceneSize(m_pageno).height()) / 2.0);
        m_gview->setSceneRect(0, -yMargin, sceneW, totalHeight + 2.0 * yMargin);
    }
}

void
DocumentView::enterEvent(QEnterEvent *e)
{
    if (m_config.split.focus_follows_mouse)
    {
        container()->focusView(this);
    }

    QWidget::enterEvent(e);
}

void
DocumentView::resizeEvent(QResizeEvent *event)
{
    // TODO: Maybe do this only when auto resize is enabled ?
    invalidateVisiblePagesCache();

    if (m_resize_timer)
        m_resize_timer->start();

    QWidget::resizeEvent(event);
}

void
DocumentView::handleDeferredResize() noexcept
{
    // clearDocumentItems();
    cachePageStride();
    updateSceneRect();

    if (m_layout_mode == LayoutMode::SINGLE)
        renderPage();
    else
        renderPages();

    if (m_auto_resize)
    {
        setFitMode(m_fit_mode);
        fitModeChanged(m_fit_mode);
    }
}

void
DocumentView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (!m_deferred_fit)
        return;

    setFitMode(m_fit_mode);
    m_deferred_fit = false;
}

bool
DocumentView::pageAtScenePos(QPointF scenePos, int &outPageIndex,
                             GraphicsImageItem *&outPageItem) const noexcept
{
    outPageIndex = -1;
    outPageItem  = nullptr;

    const int N = m_model->numPages();
    if (N <= 0 || m_page_offsets.size() < static_cast<size_t>(N + 1))
        return false;

    // ── SINGLE mode: only one page is ever in the scene
    // ─────────────────────
    if (m_layout_mode == LayoutMode::SINGLE)
    {
        auto it = m_page_items_hash.find(m_pageno);
        if (it != m_page_items_hash.end() && it.value()
            && it.value()->sceneBoundingRect().contains(scenePos))
        {
            outPageIndex = m_pageno;
            outPageItem  = it.value();
            return true;
        }
        return false;
    }

    // ── Multi-page modes: binary search the prefix-sum array
    // ───────────────── pageOffset(i) is the main-axis start of page i.
    // upper_bound(coord) gives the first entry strictly greater than coord,
    // so the page that owns coord is one slot before that iterator.
    const double coord = (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
                             ? scenePos.x()
                             : scenePos.y();

    const auto it1 = std::upper_bound(m_page_offsets.cbegin(),
                                      m_page_offsets.cend(), coord);
    // Candidate is the page whose slot contains coord on the main axis.
    int candidate
        = static_cast<int>(std::distance(m_page_offsets.cbegin(), it1) - 1);
    candidate = std::clamp(candidate, 0, N - 1);

    // ── Try candidate, then expand outward
    // ─────────────────────────────────── With variable page sizes the
    // binary search is exact for the main axis, but the cross-axis check
    // (sceneBoundingRect) can still miss e.g. during a zoom animation
    // frame. Expanding ±1 covers that transient case without ever needing
    // more — the binary search already pins the main-axis page correctly,
    // so ±1 is now a genuine safety net rather than the primary mechanism.

    std::vector<int> candidates;
    if (m_layout_mode == LayoutMode::BOOK)
    {
        candidates.push_back(candidate);
        // Spread partner
        if (candidate == 0)
        {
            // Cover is alone, but check page 1 as neighbor
            candidates.push_back(1);
        }
        else if (candidate % 2 != 0)
        {
            // Odd page (left side) → partner is candidate+1
            candidates.push_back(candidate + 1);
            candidates.push_back(candidate - 1);
        }
        else
        {
            // Even page (right side) → partner is candidate-1
            candidates.push_back(candidate - 1);
            candidates.push_back(candidate + 1);
        }
    }
    else
    {
        candidates = {candidate, candidate - 1, candidate + 1};
    }

    for (int pg : candidates)
    {
        if (pg < 0 || pg >= N)
            continue;
        auto jt = m_page_items_hash.find(pg);
        if (jt != m_page_items_hash.end() && jt.value()
            && jt.value()->sceneBoundingRect().contains(scenePos))
        {
            outPageIndex = pg;
            outPageItem  = jt.value();
            return true;
        }
    }

    return false;
}

void
DocumentView::clearVisiblePages() noexcept
{
    // m_page_items_hash
    for (auto it = m_page_items_hash.begin(); it != m_page_items_hash.end();
         ++it)
    {
        GraphicsImageItem *item = it.value();
        if (item->scene() == m_gscene)
            m_gscene->removeItem(item);
    }
    m_page_items_hash.clear();
}

void
DocumentView::clearVisibleLinks() noexcept
{
    QList<int> trackedPages = m_page_links_hash.keys();
    for (int pageno : trackedPages)
    {
        for (auto *link : m_page_links_hash.take(pageno))
        {
            if (link->scene() == m_gscene)
                m_gscene->removeItem(link);
            delete link; // only if you own the memory
        }
    }
}

void
DocumentView::clearVisibleAnnotations() noexcept
{
    QList<int> trackedPages = m_page_annotations_hash.keys();
    for (int pageno : trackedPages)
    {
        for (auto *annot : m_page_annotations_hash.take(pageno))
        {
            if (annot->scene() == m_gscene)
                m_gscene->removeItem(annot);
            delete annot; // only if you own the memory
        }
    }
}

void
DocumentView::handleContextMenuRequested(const QPoint &globalPos,
                                         bool *handled) noexcept
{

#ifndef NDEBUG
    qDebug() << "DocumentView::handleContextMenuRequested(): Context menu "
             << "requested at global position:" << globalPos;
#endif
    QMenu *menu    = new QMenu(this);
    auto addAction = [this, &menu](const QString &text, const auto &slot)
    {
        QAction *action = new QAction(text, menu); // sets parent = menu
        connect(action, &QAction::triggered, this, slot);
        menu->addAction(action);
    };

    const bool selectionActive
        = m_selection_path_item && !m_selection_path_item->path().isEmpty();
    const bool annotModeActive
        = m_gview->mode() == GraphicsView::Mode::AnnotSelect
          || m_gview->mode() == GraphicsView::Mode::AnnotPopup;
    const auto selectedAnnots = annotModeActive
                                    ? getSelectedAnnotations()
                                    : decltype(getSelectedAnnotations()){};
    const bool hasAnnots      = !selectedAnnots.empty();
    bool hasActions           = false;

    // if (selectionActive &&
    // m_selection_path_item->path().contains(scenePos))
    //     emit textSelectionRightClickRequested(globalPos, scenePos);

    if (selectionActive)
    {
        addAction("Copy Text", [this]() { YankSelection(true); });
        addAction("Copy Unformatted Text", [this]() { YankSelection(false); });
        addAction("Highlight Text",
                  &DocumentView::TextHighlightCurrentSelection);
        hasActions = true;
    }

    if (hasAnnots)
    {
        if (hasActions)
            menu->addSeparator();

        // Delete selected annotations
        addAction("Delete Annotations", [this, selectedAnnots]()
        {
            QHash<int, QSet<int>> objNumsByPage;
            for (const auto &[pageno, annot] : selectedAnnots)
            {
                if (!annot)
                    continue;
                objNumsByPage[pageno].insert(annot->index());
            }

            for (auto it = objNumsByPage.cbegin(); it != objNumsByPage.cend();
                 ++it)
            {
                m_model->undoStack()->push(new DeleteAnnotationsCommand(
                    m_model, it.key(), it.value()));
            }
            setModified(true);
        });

        // Change color of the selected annotations
        // TODO: Put this under a undo command
        addAction("Change Color", [this]()
        {
            auto newColor = QColorDialog::getColor(
                Qt::white, this, "Annotation Color",
                QColorDialog::ColorDialogOption::ShowAlphaChannel);

            if (newColor.isValid())
                changeColorOfSelectedAnnotations(newColor);
        });
        hasActions = true;
    }

    if (!hasActions)
        return;

    if (handled)
        *handled = true;

    menu->popup(globalPos);
}

void
DocumentView::TextHighlightCurrentSelection() noexcept
{
    handleTextHighlightRequested(m_selection_start, m_selection_end);
}

void
DocumentView::updateCurrentHitHighlight() noexcept
{
    if (m_search_index < 0 || m_search_index >= m_search_hit_flat_refs.size())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    const float scale = m_model->logicalScale();
    const HitRef ref  = m_search_hit_flat_refs[m_search_index];
    const auto &hit   = m_search_hits[ref.page][ref.indexInPage];

    GraphicsImageItem *pageItem = m_page_items_hash.value(ref.page, nullptr);
    if (!pageItem || !pageItem->scene())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    QPolygonF poly;
    poly.reserve(4);
    poly << QPointF(hit.quad.ul.x * scale, hit.quad.ul.y * scale)
         << QPointF(hit.quad.ur.x * scale, hit.quad.ur.y * scale)
         << QPointF(hit.quad.lr.x * scale, hit.quad.lr.y * scale)
         << QPointF(hit.quad.ll.x * scale, hit.quad.ll.y * scale);

    QPainterPath path;
    const QTransform toScene = pageItem->sceneTransform();

    path.addPolygon(toScene.map(poly));

    m_current_search_hit_item->setPath(path);
}

void
DocumentView::scrollToCurrentHit() noexcept
{
    if (m_search_index < 0 || m_search_index >= m_search_hit_flat_refs.size())
        return;

    const QPainterPath &path = m_current_search_hit_item->path();
    if (path.isEmpty())
        return;

    m_gview->centerOn(path.boundingRect().center());
}

// Private helper — O(log N), call only in multi-page modes.
int
DocumentView::pageAtAxisCoord(double coord) const noexcept
{
    const auto it = std::upper_bound(m_page_offsets.cbegin(),
                                     m_page_offsets.cend(), coord);
    const int candidate
        = static_cast<int>(std::distance(m_page_offsets.cbegin(), it) - 1);
    return std::clamp(candidate, 0, m_model->numPages() - 1);
}

void
DocumentView::updateCurrentPage() noexcept
{
    ensureVisiblePagePlaceholders();

#ifndef NDEBUG
    qDebug() << "DocumentView::updateCurrentPage(): Updating current page based "
             << "on scroll position. Current page:" << m_pageno + 1;
#endif

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        emit currentPageChanged(m_pageno + 1);
        return;
    }

    const int viewportHalf = (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
                                 ? m_gview->viewport()->width() / 2
                                 : m_gview->viewport()->height() / 2;

    const int scrollPos = (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
                              ? m_hscroll->value()
                              : m_vscroll->value();

    // Map the viewport centre into scene coordinates on the layout axis.
    // QGraphicsView scroll values are in scene-pixel units, so this is
    // direct.
    const double centerCoord = static_cast<double>(scrollPos + viewportHalf);

    const int new_page = pageAtAxisCoord(centerCoord);
    if (new_page == m_pageno)
        return;

    m_pageno = new_page;
    emit currentPageChanged(new_page + 1);

    // Reset index when scrolling into a new page
    if (m_visual_line_mode)
    {
        m_visual_line_index = -1;
        snap_visual_line(false);
    }
}

void
DocumentView::ensureVisiblePagePlaceholders() noexcept
{
    const std::set<int> &visiblePages = getVisiblePages();

    // Quick check - if we already have all pages, return early
    bool allExist = true;
    for (int pageno : visiblePages)
    {
        if (!m_page_items_hash.contains(pageno))
        {
            allExist = false;
            break;
        }
    }

    if (allExist)
        return;

    for (int pageno : visiblePages)
    {
        if (!m_page_items_hash.contains(pageno))
            createAndAddPlaceholderPageItem(pageno);
    }
}

void
DocumentView::clearDocumentItems() noexcept
{
    invalidateVisiblePagesCache();

    m_page_annotations_hash.clear();
    m_page_links_hash.clear();
    m_page_items_hash.clear();
    m_search_items.clear();
    m_pending_renders.clear();
    m_render_queue.clear();

    // Remove all items from scene EXCEPT the persistent ones
    // (selection path, search hit, etc.)
    const auto items = m_gscene->items();
    for (auto *item : items)
    {
        if (item != m_selection_path_item && item != m_current_search_hit_item
            && item != m_jump_marker && item != m_visual_line_item)
        {
            m_gscene->removeItem(item);
            delete item;
        }
    }

    ClearTextSelection();
    m_renders_in_flight = 0;
    m_gscene->setSceneRect(QRectF()); // Reset scene bounds
}

// Request rendering of a specific page (ASYNC)
void
DocumentView::requestPageRender(int pageno) noexcept
{
    if (m_pending_renders.contains(pageno))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::requestPageRender(): Requesting page render for "
                "pageno = "
             << pageno;
#endif

    m_pending_renders.insert(pageno);
    createAndAddPlaceholderPageItem(pageno);

    m_render_queue.enqueue(pageno);
    startNextRenderJob();
}

void
DocumentView::renderPageFromImage(int pageno, const QImage &image) noexcept
{
    // Remove old item (placeholder OR real page) BEFORE adding the new
    // item, since createAndAddPageItem overwrites the hash entry.  Without
    // this, re-renders after zoom leave orphaned items in the scene
    // (visible stale pages and unbounded memory growth).
    auto it = m_page_items_hash.find(pageno);
    if (it != m_page_items_hash.end())
    {
        GraphicsImageItem *old = it.value();
        if (old && old->scene() == m_gscene)
        {
            m_gscene->removeItem(old);
            delete old;
        }
        m_page_items_hash.remove(pageno);
    }

    createAndAddPageItem(pageno, image);

    clearLinksForPage(pageno);
    clearAnnotationsForPage(pageno);
    clearSearchItemsForPage(pageno);
}

void
DocumentView::createAndAddPlaceholderPageItem(int pageno) noexcept
{
    if (m_page_items_hash.contains(pageno))
        return;

    const QSizeF logicalSize = pageSceneSize(pageno);
    if (logicalSize.isEmpty())
        return;

    // Create a minimal 1x1 image for placeholder (memory efficient)
    QImage img(1, 1, QImage::Format_RGB32);
    img.fill(m_model->invertColor() ? Qt::black : Qt::white);

    auto *item = new GraphicsImageItem();
    item->setImage(img);
    item->setTransform(
        QTransform::fromScale(logicalSize.width() / img.width(),
                              logicalSize.height() / img.height()));

    const double pageW = logicalSize.width();
    const double pageH = logicalSize.height();
    const QRectF sr    = m_gview->sceneRect();

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const double yOffset = (m_max_page_cross_extent - pageH) / 2.0;
        const double xPos    = pageOffset(pageno);
        item->setPos(xPos, yOffset);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        const double xPos = sr.x() + (sr.width() - pageW) / 2.0;
        const double yPos = sr.y() + (sr.height() - pageH) / 2.0;
        item->setPos(xPos, yPos);
    }
    else
    {
        // BOOK or TOP_TO_BOTTOM
        item->setPos(pageXOffset(pageno, pageW, sr.width()),
                     pageOffset(pageno));
    }

    m_gscene->addItem(item);
    m_page_items_hash[pageno] = item;
    item->setData(0, QStringLiteral("placeholder_page"));
}

void
DocumentView::createAndAddPageItem(int pageno, const QImage &img) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::createAndAddPageItem(): Adding page item for "
             << "pageno = " << pageno;
#endif
    auto *item = new GraphicsImageItem();
    item->setImage(img);

    // Logical scene size of the rendered image.
    const QSizeF logicalSize = pageSceneSize(pageno);
    const double pageW       = logicalSize.width();
    const double pageH       = logicalSize.height();
    const QRectF sr          = m_gview->sceneRect();

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const double yPos = (m_max_page_cross_extent - pageH) / 2.0;
        item->setPos(pageOffset(pageno), yPos);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        item->setPos(sr.x() + (sr.width() - pageW) / 2.0,
                     sr.y() + (sr.height() - pageH) / 2.0);
    }
    else // TOP_TO_BOTTOM & BOOK
    {
        item->setPos(pageXOffset(pageno, pageW, sr.width()),
                     pageOffset(pageno));
    }

    m_gscene->addItem(item);
    m_page_items_hash[pageno] = item;
}

void
DocumentView::renderLinks(int pageno,
                          const std::vector<Model::RenderLink> &links,
                          bool append) noexcept
{
    if (!append && m_page_links_hash.contains(pageno))
        return;

    GraphicsImageItem *pageItem = m_page_items_hash[pageno];

    if (!pageItem)
        return;

    for (const auto &link : links)
    {
        auto *item
            = new BrowseLinkItem(link.rect, link.uri, link.type, link.boundary);
        item->setSourceLocation(link.source_loc);

        if (link.type == BrowseLinkItem::LinkType::Page)
            item->setGotoPageNo(link.target_page);

        if (link.type == BrowseLinkItem::LinkType::Location)
        {
            item->setGotoPageNo(link.target_page);
            item->setTargetLocation(link.target_loc);
        }

        switch (item->linkType())
        {
            case BrowseLinkItem::LinkType::FitH:
            {
                connect(
                    item, &BrowseLinkItem::horizontalFitRequested, this,
                    [this](int pageno, const BrowseLinkItem::PageLocation &loc)
                {
                    const PageLocation sourceLocation = CurrentLocation();
                    if (sourceLocation.pageno != -1)
                        addToHistory(sourceLocation);
                    PageLocation target{pageno, loc.x, loc.y};
                    if (std::isnan(target.x))
                        target.x = 0;
                    if (std::isnan(target.y))
                        target.y = 0;
                    addToHistory(target);
                    GotoLocation(target);
                    setFitMode(FitMode::Width);
                });
            }
            break;

            case BrowseLinkItem::LinkType::FitV:
            {
                connect(
                    item, &BrowseLinkItem::verticalFitRequested, this,
                    [this](int pageno, const BrowseLinkItem::PageLocation &loc)
                {
                    const PageLocation sourceLocation = CurrentLocation();
                    if (sourceLocation.pageno != -1)
                        addToHistory(sourceLocation);
                    PageLocation target{pageno, loc.x, loc.y};
                    if (std::isnan(target.x))
                        target.x = 0;
                    if (std::isnan(target.y))
                        target.y = 0;
                    addToHistory(target);
                    GotoLocation(target);
                    setFitMode(FitMode::Height);
                });
            }
            break;

            case BrowseLinkItem::LinkType::Page:
            {
                connect(item, &BrowseLinkItem::jumpToPageRequested, this,
                        [this, pageno](int targetPageno,
                                       const BrowseLinkItem::PageLocation
                                           &sourceLocationOfLink)
                {
                    const DocumentView::PageLocation targetLocation{
                        targetPageno, 0, 0};
                    const DocumentView::PageLocation sourceLocation{
                        pageno, sourceLocationOfLink.x, sourceLocationOfLink.y};
                    addToHistory(sourceLocation);
                    addToHistory(targetLocation);
                    GotoLocation(targetLocation);
                });
            }
            break;

            case BrowseLinkItem::LinkType::Location:
            {

                connect(item, &BrowseLinkItem::jumpToLocationRequested, this,
                        [this, pageno](int targetPageno,
                                       const BrowseLinkItem::PageLocation
                                           &targetLocationOfLink,
                                       const BrowseLinkItem::PageLocation
                                           &sourceLocationOfLink)
                {
                    const DocumentView::PageLocation targetLocation{
                        targetPageno, targetLocationOfLink.x,
                        targetLocationOfLink.y};

                    const DocumentView::PageLocation sourceLocation{
                        pageno, sourceLocationOfLink.x, sourceLocationOfLink.y};
                    PageLocation target = targetLocation;
                    if (std::isnan(target.x))
                        target.x = 0;
                    if (std::isnan(target.y))
                        target.y = 0;
                    addToHistory(sourceLocation);
                    addToHistory(target);
                    GotoLocation(target);
                });
            }
            break;

            default:
                break;
        }

        connect(item, &BrowseLinkItem::linkCopyRequested, this,
                [this](const QString &link)
        {
            if (link.startsWith("#"))
            {
                auto equal_pos = link.indexOf("=");
                emit clipboardContentChanged(m_model->filePath() + "#"
                                             + link.mid(equal_pos + 1));
            }
            else
            {
                emit clipboardContentChanged(link);
            }
        });

        // Map link rect to scene coordinates
        const QRectF sceneRect
            = pageItem->mapToScene(item->rect()).boundingRect();
        item->setRect(sceneRect);
        item->setZValue(ZVALUE_LINK);

        m_gscene->addItem(item);
        m_page_links_hash[pageno].push_back(item);
    }
}

void
DocumentView::renderAnnotations(
    const int pageno,
    const std::vector<Model::RenderAnnotation> &annotations) noexcept
{
    if (m_page_annotations_hash.contains(pageno))
        return;

    GraphicsImageItem *pageItem = m_page_items_hash[pageno];
    if (!pageItem)
        return;

    for (const auto &annot : annotations)
    {
        Annotation *annot_item = nullptr;
        switch (annot.type)
        {
            case PDF_ANNOT_HIGHLIGHT:
                annot_item = new HighlightAnnotation(annot.rect,
                                                     annot.index); // no color
                break;

            case PDF_ANNOT_SQUARE:
                annot_item
                    = new RectAnnotation(annot.rect, annot.index, annot.color);
                break;

            case PDF_ANNOT_TEXT:
            {
                auto *textAnnot = new TextAnnotation(annot.rect, annot.index,
                                                     annot.color, annot.text);
                annot_item      = textAnnot;

                // Connect edit signal for text annotations
                connect(textAnnot, &TextAnnotation::editRequested,
                        [this, textAnnot, pageno]()
                {
                    bool ok;
                    QString newText = QInputDialog::getMultiLineText(
                        this, tr("Edit Note"), tr("Edit annotation text:"),
                        textAnnot->text(), &ok);

                    if (ok && !newText.isEmpty())
                    {
                        m_model->setTextAnnotationContents(
                            pageno, textAnnot->index(), newText);
                        setModified(true);
                    }
                });
            }
            break;

            case PDF_ANNOT_POPUP:
                break;

            default:
                break;
        }

        if (!annot_item)
            continue;

        annot_item->setZValue(ZVALUE_ANNOTATION);
        annot_item->setPos(pageItem->pos());
        m_gscene->addItem(annot_item);

        connect(annot_item, &Annotation::annotDeleteRequested,
                [this, annot_item, pageno]()
        {
            m_model->undoStack()->push(new DeleteAnnotationsCommand(
                m_model, pageno, {annot_item->index()}));
            setModified(true);
        });

        connect(annot_item, &Annotation::annotColorChangeRequested,
                [this, annot_item, pageno]()
        {
            auto color = QColorDialog::getColor(
                annot_item->data(3).value<QColor>(), this, "Highlight Color",
                QColorDialog::ColorDialogOption::ShowAlphaChannel);
            if (color.isValid())
            {
                m_model->annotChangeColor(pageno, annot_item->index(), color);
                setModified(true);
                // requestPageRender(pageno);
            }
        });

        m_page_annotations_hash[pageno].push_back(annot_item);
    }
}

void
DocumentView::setModified(bool modified) noexcept
{
    if (m_is_modified == modified)
        return;

    m_is_modified = modified;
    QString title = m_config.window.title_format;
    QString fileName;
    if (!m_config.statusbar.file_name_only)
        fileName = filePath();
    else
        fileName = this->fileName();

    if (modified)
    {
        if (!title.endsWith("*"))
            title.append("*");
        if (!fileName.endsWith("*"))
            fileName.append("*");
    }
    else
    {
        if (title.endsWith("*"))
            title.chop(1);
        if (fileName.endsWith("*"))
            fileName.chop(1);
    }

    title = title.arg(this->fileName());

    emit panelNameChanged(fileName);
    this->setWindowTitle(title);
}

bool
DocumentView::EncryptDocument() noexcept
{
    Model::EncryptInfo encryptInfo;
    bool ok;
    QString password = QInputDialog::getText(
        this, "Encrypt Document", "Enter password:", QLineEdit::Password,
        QString(), &ok);
    if (!ok || password.isEmpty())
        return false;
    encryptInfo.user_password = password;
    return m_model->encrypt(encryptInfo);
}

bool
DocumentView::DecryptDocument() noexcept
{
    if (fz_needs_password(m_model->m_ctx, m_model->m_doc))
    {
        bool ok;
        QString password;

        while (true)
        {
            password = QInputDialog::getText(
                this, "Decrypt Document",
                "Enter password:", QLineEdit::Password, QString(), &ok);
            if (!ok)
                return false;

            if (fz_authenticate_password(m_model->m_ctx, m_model->m_doc,
                                         password.toStdString().c_str()))
                return m_model->decrypt();
        }
    }
    return true;
}

void
DocumentView::renderSearchHitsForPage(int pageno) noexcept
{
    if (!m_search_hits.contains(pageno))
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::renderSearchHitsForPage(): Rendering search "
             << "hits for page:" << pageno;
#endif

    const auto &hits = m_search_hits.value(pageno); // Local copy

    // 2. Validate the Page Item still exists in the scene
    GraphicsImageItem *pageItem = m_page_items_hash[pageno];

    if (!pageItem)
        return;

    QGraphicsPathItem *item = ensureSearchItemForPage(pageno);
    if (!item)
        return;

    QPainterPath allPath;

    const QTransform toScene = pageItem->sceneTransform();

    const auto scale = m_model->logicalScale();

    for (unsigned int i = 0; i < hits.size(); ++i)
    {
        const Model::SearchHit &hit = hits[i];
        QPolygonF poly;
        poly.reserve(4);
        poly << QPointF(hit.quad.ul.x * scale, hit.quad.ul.y * scale)
             << QPointF(hit.quad.ur.x * scale, hit.quad.ur.y * scale)
             << QPointF(hit.quad.lr.x * scale, hit.quad.lr.y * scale)
             << QPointF(hit.quad.ll.x * scale, hit.quad.ll.y * scale);

        allPath.addPolygon(toScene.map(poly));
    }

    // Set colors
    item->setPath(allPath);
    item->setBrush(rgbaToQColor(m_config.colors.search_match));
}

void
DocumentView::renderSearchHitsInScrollbar() noexcept
{
    m_vscroll->setSearchMarkers({});
    m_hscroll->setSearchMarkers({});

    if (m_search_hit_flat_refs.empty())
        return;

    // SINGLE mode has no scrollbar to mark — only one page is ever shown.
    if (m_layout_mode == LayoutMode::SINGLE)
        return;

    // Scene coordinates are logical pixels, so use logicalScale, not
    // physicalScale (which is DPR-multiplied and overshoots on HiDPI).
    const double pdfToSceneScale = m_model->logicalScale();

    std::vector<double> markers;
    markers.reserve(m_search_hit_flat_refs.size());
    if (m_layout_mode == LayoutMode::TOP_TO_BOTTOM
        || m_layout_mode == LayoutMode::BOOK)
    {
        for (const auto &hitRef : m_search_hit_flat_refs)
        {
            const auto &hit = m_search_hits[hitRef.page][hitRef.indexInPage];
            markers.push_back(pageOffset(hitRef.page)
                              + hit.quad.ul.y
                                    * pdfToSceneScale); // ← was missing
        }
        m_vscroll->setSearchMarkers(std::move(markers));
    }
    else // LEFT_TO_RIGHT
    {
        for (const auto &hitRef : m_search_hit_flat_refs)
        {
            const auto &hit = m_search_hits[hitRef.page][hitRef.indexInPage];
            markers.push_back(pageOffset(hitRef.page)
                              + hit.quad.ul.x * pdfToSceneScale);
        }
        m_hscroll->setSearchMarkers(std::move(markers));
    }
}

QGraphicsPathItem *
DocumentView::ensureSearchItemForPage(int pageno) noexcept
{
    if (m_search_items.contains(pageno))
        return m_search_items[pageno];

    auto *item = m_gscene->addPath(QPainterPath());
    item->setBrush(QColor(255, 230, 150, 120));
    item->setPen(Qt::NoPen);
    item->setZValue(ZVALUE_SEARCH_HITS);

    m_search_items[pageno] = item;
    return item;
}

void
DocumentView::ReselectLastTextSelection() noexcept
{
    // TODO: Implement this!
}

void
DocumentView::addToHistory(const PageLocation &location) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::addLocationToHistory(): Adding location to "
             << "history: Page =" << location.pageno << ", x =" << location.x
             << ", y =" << location.y;
#endif
    if (location.pageno < 0)
        return;

    if (m_loc_history_index + 1 < (int)m_loc_history.size())
    {
        m_loc_history.erase(m_loc_history.begin() + m_loc_history_index + 1,
                            m_loc_history.end());
    }

    if (!m_loc_history.empty()
        && locationsEqual(m_loc_history.back(), location))
    {
        m_loc_history_index = (int)m_loc_history.size() - 1;
        return;
    }

    m_loc_history.push_back(location);
    m_loc_history_index = (int)m_loc_history.size() - 1;
}

void
DocumentView::setInvertColor(bool invert) noexcept
{
    m_model->setInvertColor(invert);
    if (m_layout_mode == LayoutMode::SINGLE)
        renderPage();
    else
        renderPages();
}

void
DocumentView::handleAnnotSelectClearRequested() noexcept
{

#ifndef NDEBUG
    qDebug() << "DocumentView::handleAnnotSelectClearRequested(): Clearing "
             << "all annotation selections.";
#endif

    for (auto it = m_page_annotations_hash.begin();
         it != m_page_annotations_hash.end(); ++it)
    {
        const auto &annotations = it.value();
        for (auto *annot : annotations)
        {
            if (!annot)
                continue;

            annot->restoreBrushPen();
            annot->setSelected(false);
        }
    }
}

void
DocumentView::handleAnnotSelectRequested(QRectF sceneRect) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(sceneRect.center(), pageno, pageItem))
        return;

    const QRectF pageLocalRect
        = pageItem->mapFromScene(sceneRect).boundingRect();

    const QRectF annotSearchRect = pageLocalRect;

    const auto annotsInArea = annotationsInArea(pageno, annotSearchRect);
    if (annotsInArea.empty())
        return;

    for (auto *annot : annotsInArea)
        annot->select(Qt::black);
}

void
DocumentView::handleAnnotSelectRequested(QPointF scenePos) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(scenePos, pageno, pageItem))
        return;

    const QPointF searchPos = pageItem->mapFromScene(scenePos);
    const auto annotAtPoint = annotationAtPoint(pageno, searchPos);

    if (!annotAtPoint)
        return;

    annotAtPoint->select(Qt::black);
}

std::vector<Annotation *>
DocumentView::annotationsInArea(int pageno, QRectF area) noexcept
{
    std::vector<Annotation *> annotsInArea;
    if (!m_page_annotations_hash.contains(pageno))
        return annotsInArea;

    const auto &annotations = m_page_annotations_hash[pageno];
    for (auto *annot : annotations)
    {
        if (!annot)
            continue;

        if (area.intersects(annot->boundingRect()))
        {
            annotsInArea.push_back(annot);
        }
    }
#ifndef NDEBUG
    qDebug() << "DocumentView::annotationsInArea(): Found"
             << annotsInArea.size() << "annotations in area:" << area
             << "on page:" << pageno;
#endif
    return annotsInArea;
}

Annotation *
DocumentView::annotationAtPoint(int pageno, QPointF point) noexcept
{
    Annotation *foundAnnot{nullptr};
    if (!m_page_annotations_hash.contains(pageno))
        return foundAnnot;

    const auto &annotations = m_page_annotations_hash[pageno];
    for (auto *annot : annotations)
    {
        if (!annot)
            continue;

        if (annot->boundingRect().contains(point))
        {
            foundAnnot = annot;
            break;
        }
    }
#ifndef NDEBUG
    qDebug() << "DocumentView::annotationAtPoint(): Searching for annotation "
             << "at point:" << point << "on page:" << pageno;
#endif

    return foundAnnot;
}

std::vector<std::pair<int, Annotation *>>
DocumentView::getSelectedAnnotations() noexcept
{
    std::vector<std::pair<int, Annotation *>> selectedAnnotations;

    for (auto it = m_page_annotations_hash.begin();
         it != m_page_annotations_hash.end(); ++it)
    {
        const int pageno        = it.key();
        const auto &annotations = it.value();
        for (auto *annot : annotations)
        {
            if (!annot)
                continue;

            if (annot->isSelected())
            {
                selectedAnnotations.push_back({pageno, annot});
            }
        }
    }

#ifndef NDEBUG
    qDebug() << "DocumentView::getSelectedAnnotations(): Found"
             << selectedAnnotations.size() << "selected annotations.";
#endif

    return selectedAnnotations;
}

void
DocumentView::changeColorOfSelectedAnnotations(const QColor &color) noexcept
{
    const auto selectedAnnots = getSelectedAnnotations();
    if (selectedAnnots.empty())
        return;

    for (const auto &[pageno, annot] : selectedAnnots)
    {
        m_model->annotChangeColor(pageno, annot->index(), color);
    }

    setModified(true);
}

// Returns the current location in the document
DocumentView::PageLocation
DocumentView::CurrentLocation() noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;
    QPointF sceneCenter = m_gview->mapToScene(
        m_gview->viewport()->width() / 2, m_gview->viewport()->height() / 2);

    if (!pageAtScenePos(sceneCenter, pageno, pageItem))
        return {-1, 0, 0};

    const QPointF pageLocalPos = pageItem->mapFromScene(sceneCenter);
    return {pageno, (float)pageLocalPos.x(), (float)pageLocalPos.y()};
}

namespace
{
bool
mapRegionToPageRects(QRectF area, GraphicsImageItem *pageItem,
                     QRectF &outLogical, QRect &outPixels) noexcept
{
    if (!pageItem)
        return false;

    const QRectF pageRect = pageItem->mapFromScene(area).boundingRect();
    const qreal dpr       = pageItem->devicePixelRatio();
    const QSize pixSize   = QSize(pageItem->width(), pageItem->height());
    const QRectF logicalBounds(
        QPointF(0.0, 0.0),
        QSizeF(pixSize.width() / dpr, pixSize.height() / dpr));

    outLogical = pageRect.intersected(logicalBounds);
    if (outLogical.isEmpty())
        return false;

    const QRectF pixelRect(outLogical.x() * dpr, outLogical.y() * dpr,
                           outLogical.width() * dpr, outLogical.height() * dpr);
    const QRectF pixmapBounds(QPointF(0.0, 0.0), QSizeF(pixSize));
    const QRectF clippedPixels = pixelRect.intersected(pixmapBounds);
    if (clippedPixels.isEmpty())
        return false;

    outPixels = clippedPixels.toRect();
    return true;
}
} // namespace

void
DocumentView::CopyTextFromRegion(QRectF area) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    const QPointF pageStart = pageItem->mapFromScene(area.topLeft());
    const QPointF pageEnd   = pageItem->mapFromScene(area.bottomRight());

    const std::string text = m_model->getTextInArea(pageno, pageStart, pageEnd);

    QClipboard *clip = QApplication::clipboard();
    clip->setText(QString::fromStdString(text));
}

void
DocumentView::CopyRegionAsImage(QRectF area) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;
    const QImage img = pageItem->image().copy(pixelRect);

    if (!img.isNull())
    {
        QClipboard *clip = QApplication::clipboard();
        clip->setImage(img);
    }
}

void
DocumentView::SaveRegionAsImage(QRectF area) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;
    const QImage img = pageItem->image().copy(pixelRect);

    if (img.isNull())
        return;

    QFileDialog fd(this);
    const QString fileName
        = fd.getSaveFileName(this, "Save Image", "",
                             "PNG Image (*.png), "
                             "JPEG Image (*.jpg *.jpeg), "
                             "BMP Image (*.bmp);; All Files (*)");
    if (fileName.isEmpty())
        return;
    QString format;
    if (fileName.endsWith(".png", Qt::CaseInsensitive))
        format = "PNG";
    else if (fileName.endsWith(".jpg", Qt::CaseInsensitive)
             || fileName.endsWith(".jpeg", Qt::CaseInsensitive))
        format = "JPEG";
    else if (fileName.endsWith(".bmp", Qt::CaseInsensitive))
        format = "BMP";
    else
        format = "PNG"; // Default to PNG
    img.save(fileName, format.toStdString().c_str());
}

void
DocumentView::OpenRegionInExternalViewer(QRectF area) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;
    openImageInExternalViewer(pageItem->image().copy(pixelRect));
}

void
DocumentView::openImageInExternalViewer(const QImage &img) noexcept
{
    if (img.isNull())
        return;

    // Save to a temporary file
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    if (!tempFile.open())
        return;

    img.save(&tempFile, "PNG");
    tempFile.close();

    QDesktopServices::openUrl(QUrl::fromLocalFile(tempFile.fileName()));
}

void
DocumentView::setAutoReload(bool state) noexcept
{
    m_auto_reload          = state;
    const QString filepath = m_model->filePath();
    if (m_auto_reload)
    {
        if (!m_file_watcher)
            m_file_watcher = new QFileSystemWatcher(this);

        if (!m_file_watcher->files().contains(filepath))
            m_file_watcher->addPath(filepath);

        connect(m_file_watcher, &QFileSystemWatcher::fileChanged, this,
                &DocumentView::onFileReloadRequested, Qt::UniqueConnection);
    }
    else
    {
        if (m_file_watcher)
        {
            m_file_watcher->removePath(filepath);
            m_file_watcher->deleteLater();
            m_file_watcher = nullptr;
        }
    }
}

// Wait until the file is readable and fully written. This is needed because
// when using latex with continuous compilation, the PDF file is often
// replaced by a new file that is first created with 0 bytes and then
// written to. If we try to reload while the file is still 0 bytes, it will
// fail.
bool
DocumentView::waitUntilReadableAsync() noexcept
{
    const QString filepath = m_model->filePath();
    QFileInfo a(filepath);
    if (!a.exists() || a.size() == 0)
        return false;

    QFileInfo b(filepath);
    return b.exists() && a.size() == b.size();
}

// Slot to handle file change notifications for auto-reload
void
DocumentView::onFileReloadRequested(const QString &path) noexcept
{
    if (path != m_model->filePath())
        return;

    tryReloadLater(0);
}

// Try to reload the document, if the file is not yet readable, wait and try
// again a few times before giving up.
void
DocumentView::tryReloadLater(int attempt) noexcept
{
    if (attempt > 15) // ~15 * 100ms = 1.5s
        return;       // give up

    if (waitUntilReadableAsync())
    {
        if (!m_model->reloadDocument())
        {
            QMessageBox::warning(this, "Auto-reload failed",
                                 "Could not reload the document.");
            return;
        }
        else
        {
#ifdef HAS_SYNCTEX
            initSynctex();
#endif
            m_cancelled->store(false);
            clearDocumentItems();
            cachePageStride();
            updateSceneRect();
            renderPages();
            setModified(false);
            // emit totalPageCountChanged(m_model->m_page_count);
        }

        const QString &filepath = m_model->filePath();
        // IMPORTANT: file may have been removed and replaced → watcher
        // loses it
        if (m_file_watcher && !m_file_watcher->files().contains(filepath))
            m_file_watcher->addPath(filepath);

        return;
    }

    QTimer::singleShot(100, this,
                       [this, attempt]() { tryReloadLater(attempt + 1); });
}

void
DocumentView::handleRegionSelectRequested(QRectF area) noexcept
{
    QMenu *menu = new QMenu(this);
    connect(menu, &QMenu::aboutToHide, this, [this, menu]()
    {
        m_gview->clearRubberBand();
        menu->deleteLater();
    });
    menu->addAction("Copy Region as Image",
                    [this, area]() { CopyRegionAsImage(area); });
    menu->addAction("Save Region as Image",
                    [this, area]() { SaveRegionAsImage(area); });
    menu->addAction("Open Region in external viewer",
                    [this, area]() { OpenRegionInExternalViewer(area); });
    menu->addAction("Copy Text from Region",
                    [this, area]() { CopyTextFromRegion(area); });

    menu->popup(QCursor::pos());
}

// Handle annotation rectangle requested
void
DocumentView::handleAnnotRectRequested(QRectF area) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    const QRectF pageLocalRect = pageItem->mapFromScene(area).boundingRect();

    // Convert from pixel space to PDF space using the model's transform
    const fz_point topLeft
        = m_model->toPDFSpace(pageno, pageLocalRect.topLeft());
    const fz_point bottomRight
        = m_model->toPDFSpace(pageno, pageLocalRect.bottomRight());

    const fz_rect rect = {
        topLeft.x,
        topLeft.y,
        bottomRight.x,
        bottomRight.y,
    };

    m_model->undoStack()->push(
        new RectAnnotationCommand(m_model, pageno, rect));
    setModified(true);
}

// Handle annotation popup (text/sticky note) requested
void
DocumentView::handleAnnotPopupRequested(QPointF scenePos) noexcept
{
    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(scenePos, pageno, pageItem))
        return;

    // Show input dialog for annotation text
    bool ok;
    QString text = QInputDialog::getMultiLineText(
        this, tr("Add Note"), tr("Enter annotation text:"), QString(), &ok);

    if (!ok || text.isEmpty())
        return;

    const QPointF pageLocalPos = pageItem->mapFromScene(scenePos);

    // Convert from pixel space to PDF space using the model's transform
    const fz_point pdfPos = m_model->toPDFSpace(pageno, pageLocalPos);

    // Create a small rect at the click position for the text annotation
    // icon
    constexpr float annotSize = 24.0f;
    const fz_rect rect        = {
        pdfPos.x,
        pdfPos.y,
        pdfPos.x + annotSize,
        pdfPos.y + annotSize,
    };

    m_model->undoStack()->push(
        new TextAnnotationCommand(m_model, pageno, rect, text));
    setModified(true);
}

// Re display the jump marker (e.g. after a jump link is activated), useful
// if user lost track of it for example.
void
DocumentView::Reshow_jump_marker() noexcept
{
    m_jump_marker->showAt(m_old_jump_marker_pos);
}

void
DocumentView::zoomHelper() noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::zoomHelper(): Zooming from" << m_current_zoom
             << "to" << m_target_zoom;
#endif

    // ── Anchor: remember which fraction of the centre page we're looking
    // at ──
    int anchorPageIndex           = -1;
    GraphicsImageItem *anchorItem = nullptr;
    QPointF anchorFrac{0.0, 0.0};
    bool hasAnchor = false;

    const QPointF centerScene
        = m_gview->mapToScene(m_gview->viewport()->rect().center());

    if (pageAtScenePos(centerScene, anchorPageIndex, anchorItem))
    {
        const QPointF localPos = anchorItem->mapFromScene(centerScene);
        const QRectF bounds    = anchorItem->boundingRect();
        if (!bounds.isEmpty())
        {
            anchorFrac = QPointF(localPos.x() / bounds.width(),
                                 localPos.y() / bounds.height());
            hasAnchor  = true;
        }
    }

    // Commit zoom, rebuild stride cache and scene rect
    m_current_zoom = m_target_zoom;
    m_model->setZoom(
        m_current_zoom); // must be before cachePageStride/updateSceneRect
                         // so pageSceneSize() uses the new zoom
    cachePageStride();
    updateSceneRect();
    m_gview->flashScrollbars();

    // Reposition every live page item at the new zoom
    const QRectF sr = m_gview->sceneRect(); // constant for the whole loop

    // For TOP_TO_BOTTOM, each page may have a different width so we must
    // compute the centering offset per-page inside the loop. Using a single
    // offset based on m_pageno mispositions all pages that differ in width.
    for (auto it = m_page_items_hash.begin(); it != m_page_items_hash.end();
         ++it)
    {
        const int i             = it.key();
        GraphicsImageItem *item = it.value();

        if (!item)
            continue;

        const bool isPlaceholder
            = (item->data(0).toString() == "placeholder_page");

        double pageWidthScene  = 0.0;
        double pageHeightScene = 0.0;

        if (isPlaceholder)
        {
            // Use this page's own dimensions, not the current page's.
            const QSizeF logicalSize = pageSceneSize(i);
            const QImage &img        = item->image();
            if (!img.isNull() && img.width() > 0 && img.height() > 0)
            {
                item->setScale(1.0);
                item->setTransform(
                    QTransform::fromScale(logicalSize.width() / img.width(),
                                          logicalSize.height() / img.height()));
            }
            pageWidthScene  = logicalSize.width();
            pageHeightScene = logicalSize.height();
        }
        else
        {
            // Scale the existing image so its height matches the target
            // physical pixel height for *this* page at the new zoom level.
            const double targetPixelHeight
                = m_model->page_dimension_pts(i).height_pts * m_model->DPR()
                  * m_current_zoom * m_model->DPI() / 72.0;

            const QImage &img = item->image();
            if (img.isNull() || img.height() == 0 || img.width() == 0)
                continue;

            const double currentImageHeight
                = static_cast<double>(item->height());

            if (currentImageHeight <= 0.0)
                continue; // avoid division by zero

            item->setScale(targetPixelHeight / currentImageHeight);

            pageWidthScene  = item->boundingRect().width() * item->scale();
            pageHeightScene = item->boundingRect().height() * item->scale();
        }

        if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
        {
            const double yOffset
                = (m_max_page_cross_extent - pageHeightScene) / 2.0;
            const double xPos = pageOffset(i);
            item->setPos(xPos, yOffset);
        }
        else if (m_layout_mode == LayoutMode::SINGLE)
        {
            item->setPos(sr.x() + (sr.width() - pageWidthScene) / 2.0,
                         sr.y() + (sr.height() - pageHeightScene) / 2.0);
        }
        else // TOP_TO_BOTTOM
        {
            item->setPos(pageXOffset(i, pageWidthScene, sr.width()),
                         pageOffset(i));
        }
    }

    // Invalidate render caches for all repositioned pages
    for (const int pageno : m_page_items_hash.keys())
    {
        m_model->invalidatePageCache(pageno);
        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);
    }

    renderSearchHitsInScrollbar();

    // Restore viewport to the same relative position within the anchor page
    if (hasAnchor && m_page_items_hash.contains(anchorPageIndex))
    {
        GraphicsImageItem *pageItem = m_page_items_hash[anchorPageIndex];
        const QRectF bounds         = pageItem->boundingRect();
        if (!bounds.isEmpty())
        {
            const QPointF restoredLocal(anchorFrac.x() * bounds.width(),
                                        anchorFrac.y() * bounds.height());
            m_gview->centerOn(pageItem->mapToScene(restoredLocal));
        }
    }

    ClearTextSelection();

    m_hq_render_timer->start();
}

void
DocumentView::handleHScrollValueChanged(int value) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleHScrollValueChanged(): Scrollbar value "
             << "changed to" << value;
#endif

    // During fast scrolling, only invalidate cache, don't trigger render
    invalidateVisiblePagesCache();

    updateCurrentPage();

    // Always restart the timer (debouncing)
    m_scroll_page_update_timer->start();

    // Don't trigger HQ render during rapid scrolling
    m_hq_render_timer->stop();
}

void
DocumentView::handleVScrollValueChanged(int value) noexcept
{
    // During fast scrolling, only invalidate cache, don't trigger render
    invalidateVisiblePagesCache();

    updateCurrentPage();

    // Always restart the timer (debouncing)
    m_scroll_page_update_timer->start();

    // Don't trigger HQ render during rapid scrolling
    m_hq_render_timer->stop();
}

void
DocumentView::stopPendingRenders() noexcept
{
    m_cancelled->store(true);
    m_pending_renders.clear();
    m_render_queue.clear();
}

// Handle password for password-protected files
void
DocumentView::handle_password_required() noexcept
{
    bool ok                = false;
    const QString password = QInputDialog::getText(
        this, tr("Password Required"), tr("Enter password:"),
        QLineEdit::Password, {}, &ok);

    if (!ok)
    {
        // user cancelled → abort open cleanly
        m_model->cancelOpen();
        CloseFile();
        return;
    }

    // fire-and-forget; result comes via signals
    m_model->submitPassword(password);
}

// Handle wrong entered password
void
DocumentView::handle_wrong_password() noexcept
{
    QMessageBox::warning(this, tr("Incorrect Password"),
                         tr("The password you entered is incorrect."));

    // Ask again
    handle_password_required();
}

// Copy current page as image to clipboard
void
DocumentView::Copy_page_image() noexcept
{
    if (!m_model)
        return;

    int pageno{-1};
    GraphicsImageItem *pageItem{nullptr};

    if (!pageAtScenePos(m_gview->viewport()->rect().center(), pageno, pageItem))
        return;

    const QPointF sceneCenter = m_gview->mapToScene(
        m_gview->viewport()->width() / 2, m_gview->viewport()->height() / 2);

    if (!pageAtScenePos(sceneCenter, pageno, pageItem))
        return;

    const QImage img = pageItem->image().copy();

    if (!img.isNull())
    {
        QClipboard *clip = QApplication::clipboard();
        clip->setImage(img);
    }
}

// Helper: O(1) start position of page i in scene axis coordinates
double
DocumentView::pageOffset(int pageno) const noexcept
{
    if (pageno < 0 || pageno >= static_cast<int>(m_page_offsets.size()) - 1)
        return 0.0;
    return m_page_offsets[pageno];
}

double
DocumentView::pageXOffset(int pageno, double pageW,
                          double sceneW) const noexcept
{
    if (m_layout_mode == LayoutMode::BOOK)
    {
        const double spacingScene = m_spacing * m_current_zoom;
        const double spineX       = sceneW / 2.0;
        if (pageno == 0)
            return spineX + spacingScene; // Cover is on the right
        return (pageno % 2 != 0)
                   ? (spineX - pageW)
                   : spineX + spacingScene; // Odd=Left, Even=Right
    }
    return (sceneW - pageW) / 2.0; // Centered for Single/Top-to-Bottom
}

// Helper: stride (extent + spacing) of a specific page
double
DocumentView::pageStride(int pageno) const noexcept
{
    if (pageno < 0 || pageno >= static_cast<int>(m_page_offsets.size()) - 1)
        return 0.0;

    if (m_layout_mode == LayoutMode::BOOK)
    {
        // Look ahead to the index of the next row
        int nextIdx = (pageno == 0) ? 1 : pageno + (pageno % 2 != 0 ? 2 : 1);
        nextIdx
            = std::min(nextIdx, static_cast<int>(m_page_offsets.size()) - 1);
        return m_page_offsets[nextIdx] - m_page_offsets[pageno];
    }

    return m_page_offsets[pageno + 1] - m_page_offsets[pageno];
}

void
DocumentView::visual_line_move(Direction direction) noexcept
{
    if (!m_visual_line_mode)
        return;

    if (m_visual_lines.empty())
        m_visual_lines = m_model->get_text_lines(m_pageno);

    switch (direction)
    {

        case LEFT:
        case RIGHT:
            PPRINT("Not yet implemented");
            break;

        case UP:
        {
            if (m_visual_line_index == 0)
            {
                GotoPrevPage();
                return;
            }
            else
            {
                m_visual_line_index--;
            }
        }
        break;

        case DOWN:
        {
            if (m_visual_lines.empty()
                || m_visual_line_index == m_visual_lines.size() - 1)
            {
                GotoNextPage();
                return;
            }
            else
            {
                m_visual_line_index++;
            }
        }
        break;
    }

    snap_visual_line();
}

void
DocumentView::snap_visual_line(bool centerView) noexcept
{
    // Ensure we have lines for the current page
    if (m_visual_lines.empty() || m_visual_lines.front().pageno != m_pageno)
    {
        m_visual_lines = m_model->get_text_lines(m_pageno);
    }

    if (m_visual_lines.empty())
        return;

    // If index is -1 (uninitialized), set it to the first line (0)
    if (m_visual_line_index == -1)
    {
        m_visual_line_index = 0;
    }

    if (m_visual_line_index >= 0
        && m_visual_line_index < (int)m_visual_lines.size())
    {
        const Model::VisualLineInfo &info
            = m_visual_lines.at(m_visual_line_index);
        GraphicsImageItem *pageItem
            = m_page_items_hash.value(info.pageno, nullptr);

        if (!pageItem)
            return;

        const float scale = m_model->logicalScale();
        QRectF scaledBbox(info.bbox.x() * scale, info.bbox.y() * scale,
                          info.bbox.width() * scale,
                          info.bbox.height() * scale);
        QRectF sceneBbox = pageItem->mapRectToScene(scaledBbox);

        QPainterPath path;
        path.addRect(sceneBbox);

        if (!m_visual_line_item)
        {
            m_visual_line_item = m_gscene->addPath(path);
            m_visual_line_item->setBrush(QBrush(rgbaToQColor(0xFFFFFF33)));
            m_visual_line_item->setPen(Qt::NoPen);
            m_visual_line_item->setZValue(ZVALUE_TEXT_SELECTION);
        }
        else
        {
            m_visual_line_item->setPath(path);
            m_visual_line_item->setVisible(true);
        }

        m_gview->set_visual_line_rect(sceneBbox);

        // Only center the view if explicitly requested (Manual Navigation)
        if (centerView)
        {
            m_gview->centerOn(m_visual_line_item);
        }
    }
}

void
DocumentView::set_visual_line_mode(bool state) noexcept
{
    if (m_visual_line_mode == state)
        return;

    m_visual_line_mode = state;

    if (m_visual_line_mode)
    {
        m_gview->setMode(GraphicsView::Mode::VisualLine);
        snap_visual_line();
    }
    else
    {
        if (m_visual_line_item)
        {
            m_visual_line_item->hide();
            m_gview->set_visual_line_rect(QRectF());
        }
        m_gview->setMode(m_gview->getDefaultMode());
    }
    m_gview->update();
}

void
DocumentView::handleReloadRequested(int pageno) noexcept
{
    if (pageno == -1)
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::handleReloadRequested(): Reload requested for "
             << "page:" << pageno;
#endif
    // Remove only the affected page item so it gets re-rendered
    if (m_page_items_hash.contains(pageno))
    {
        if (auto *item = m_page_items_hash.take(pageno))
        {
            m_gscene->removeItem(item);
            delete item;
        }
    }

    m_pending_renders.remove(pageno);
    invalidateVisiblePagesCache();
    requestPageRender(pageno);
}
