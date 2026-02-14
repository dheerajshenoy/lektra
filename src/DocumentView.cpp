#include "DocumentView.hpp"

#include "Annotations/HighlightAnnotation.hpp"
#include "Annotations/RectAnnotation.hpp"
#include "Annotations/TextAnnotation.hpp"
#include "BrowseLinkItem.hpp"
#include "GraphicsPixmapItem.hpp"
#include "GraphicsView.hpp"
#include "LinkHint.hpp"
#include "PropertiesWidget.hpp"
#include "WaitingSpinnerWidget.hpp"
#include "commands/DeleteAnnotationsCommand.hpp"
#include "commands/RectAnnotationCommand.hpp"
#include "commands/TextAnnotationCommand.hpp"
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

DocumentView::DocumentView(const Config &config, QWidget *parent) noexcept
    : QWidget(parent), m_config(config)
{

#ifndef NDEBUG
    qDebug() << "DocumentView::DocumentView(): Initializing DocumentView";
#endif

    m_model = new Model(this);
    connect(m_model, &Model::openFileFinished, this,
            &DocumentView::handleOpenFileFinished);
    connect(m_model, &Model::openFileFailed, this,
            [this]() { emit openFileFailed(this); });

    initGui();
#ifdef HAS_SYNCTEX
    initSynctex();
#endif
}

DocumentView::~DocumentView() noexcept
{
#ifdef HAS_SYNCTEX
    synctex_scanner_free(m_synctex_scanner);
#endif
    clearDocumentItems();

    m_model->cleanup();

    m_gscene->removeItem(m_jump_marker);
    m_gscene->removeItem(m_selection_path_item);
    m_gscene->removeItem(m_current_search_hit_item);

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
    m_hq_render_timer->setInterval(200);
    m_hq_render_timer->setSingleShot(true);

    m_scroll_page_update_timer = new QTimer(this);
    m_scroll_page_update_timer->setInterval(100);
    m_scroll_page_update_timer->setSingleShot(true);

    m_resize_timer = new QTimer(this);
    m_resize_timer->setInterval(150);
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
    m_model->setDPI(m_config.rendering.dpi);
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
    m_gview->setScrollbarIdleTimeout(m_config.scrollbars.hide_timeout);

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
DocumentView::currentPageSceneSize() const noexcept
{
    // pts -> inches (/72) -> pixels (*DPI) -> zoom
    double w
        = (m_model->pageWidthPts() / 72.0) * m_model->DPI() * m_current_zoom;
    double h
        = (m_model->pageHeightPts() / 72.0) * m_model->DPI() * m_current_zoom;

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

    m_layout_mode = mode;
    invalidateVisiblePagesCache();

    initConnections();
    // Reset view state
    clearDocumentItems();

    // Recompute stride + scene rect
    cachePageStride();
    updateSceneRect();

    // Make sure scrollbars start sane
    if (m_layout_mode == LayoutMode::SINGLE)
    {
        // Keep current page, but only render it.
        if (m_pageno < 0)
            m_pageno = 0;
        if (m_pageno >= m_model->numPages())
            m_pageno = m_model->numPages() - 1;
        renderPage();
    }
    else
    {
        // Put viewport near current page along the main axis
        GotoPage(m_pageno);
        renderVisiblePages();
    }
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
DocumentView::openAsync(const QString &filePath,
                        const QString &password) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::openAsync(): Opening file:" << filePath;
#endif
    m_spinner->start();
    m_spinner->show();
    m_model->openAsync(filePath, password);
}

void
DocumentView::handleOpenFileFinished() noexcept
{
    m_spinner->stop();
    m_spinner->hide();

    if (m_model->passwordRequired())
    {
        bool ok = false;
        QString password;
        while (true)
        {
            password = QInputDialog::getText(
                this, "Open Document", "Enter password:", QLineEdit::Password,
                QString(), &ok);
            if (!ok)
            {
                emit openFileFailed(this);
                return;
            }
            if (authenticate(password))
                break;

            QMessageBox::warning(this, "Open Document", "Incorrect password.");
        }
    }

    m_pageno = 0;

    if (m_config.layout.mode == "single")
        setLayoutMode(LayoutMode::SINGLE);
    else if (m_config.layout.mode == "left_to_right")
        setLayoutMode(LayoutMode::LEFT_TO_RIGHT);
    else
        setLayoutMode(LayoutMode::TOP_TO_BOTTOM);

    initConnections();

    FitMode initialFit;
    if (m_config.layout.initial_fit == "height")
        initialFit = FitMode::Height;
    else if (m_config.layout.initial_fit == "width")
        initialFit = FitMode::Width;
    else if (m_config.layout.initial_fit == "window")
        initialFit = FitMode::Window;
    else
        initialFit = FitMode::Height;

    m_fit_mode = initialFit;
    if (isVisible())
    {
        setFitMode(initialFit);
        m_deferred_fit = false;
    }
    else
    {
        m_deferred_fit = true;
    }

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
    // GraphicsPixmapItem *pageItem = nullptr;

    // if (!pageAtScenePos(scenePos, pageIndex, pageItem))
    //     return; // selection start outside visible pages?

    // const QPointF pagePos = pageItem->mapFromScene(scenePos);
    // m_hit_pixmap = m_model->hitTestImage(pageIndex, pagePos,
    // m_current_zoom,
    //                                      m_rotation);
    // });
    connect(m_model, &Model::searchResultsReady, this,
            &DocumentView::handleSearchResults);

    connect(m_model, &Model::reloadRequested, this, &DocumentView::reloadPage);

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        connect(m_hscroll, &QScrollBar::valueChanged,
                m_scroll_page_update_timer,
                static_cast<void (QTimer::*)()>(&QTimer::start));

        connect(m_hscroll, &QScrollBar::valueChanged, this,
                &DocumentView::invalidateVisiblePagesCache);

        connect(m_hscroll, &QScrollBar::valueChanged, this,
                &DocumentView::updateCurrentPage);

        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderVisiblePages);

        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::renderVisiblePages);
    }
    else if (m_layout_mode == LayoutMode::TOP_TO_BOTTOM)
    {
        connect(m_vscroll, &QScrollBar::valueChanged,
                m_scroll_page_update_timer,
                static_cast<void (QTimer::*)()>(&QTimer::start));

        connect(m_vscroll, &QScrollBar::valueChanged, this,
                &DocumentView::invalidateVisiblePagesCache);

        connect(m_vscroll, &QScrollBar::valueChanged, this,
                &DocumentView::updateCurrentPage);

        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderVisiblePages);

        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::renderVisiblePages);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPage);
    }

    /* Graphics View Signals */
    connect(m_gview, &GraphicsView::textHighlightRequested, this,
            &DocumentView::handleTextHighlightRequested);
    connect(m_gview,
            QOverload<const QRectF &>::of(&GraphicsView::annotSelectRequested),
            this, [this](const QRectF &sceneRect)
    { handleAnnotSelectRequested(sceneRect); });

    connect(m_gview,
            QOverload<const QPointF &>::of(&GraphicsView::annotSelectRequested),
            this, [this](const QPointF &scenePos)
    { handleAnnotSelectRequested(scenePos); });

    connect(m_gview, &GraphicsView::annotSelectClearRequested, this,
            &DocumentView::handleAnnotSelectClearRequested);

    connect(m_gview, &GraphicsView::textSelectionDeletionRequested, this,
            &DocumentView::ClearTextSelection);

    connect(m_gview, &GraphicsView::textSelectionRequested, this,
            &DocumentView::handleTextSelection);

    connect(m_gview, &GraphicsView::doubleClickRequested, this,
            [this](const QPointF &pos) { handleClickSelection(2, pos); });

    connect(m_gview, &GraphicsView::tripleClickRequested, this,
            [this](const QPointF &pos) { handleClickSelection(3, pos); });

    connect(m_gview, &GraphicsView::quadrupleClickRequested, this,
            [this](const QPointF &pos) { handleClickSelection(4, pos); });

    connect(m_gview, &GraphicsView::contextMenuRequested, this,
            &DocumentView::handleContextMenuRequested);

    connect(m_gview, &GraphicsView::regionSelectRequested, this,
            &DocumentView::handleRegionSelectRequested);

    connect(m_gview, &GraphicsView::annotRectRequested, this,
            &DocumentView::handleAnnotRectRequested);

    connect(m_gview, &GraphicsView::annotPopupRequested, this,
            &DocumentView::handleAnnotPopupRequested);
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
    // Clear previous search hits
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
    renderVisiblePages();
    updateCurrentHitHighlight();

    if (m_config.scrollbars.search_hits)
        renderSearchHitsInScrollbar();
    emit searchCountChanged(m_model->searchMatchesCount());

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

        for (int i = 0; i < hits.size(); ++i)
            m_search_hit_flat_refs.push_back({page, i});
    }
}

void
DocumentView::handleClickSelection(int clickType,
                                   const QPointF &scenePos) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleClickSelection(): Handling click type"
             << clickType << "at scene position" << scenePos;
#endif
    int pageIndex                = -1;
    GraphicsPixmapItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageIndex, pageItem))
        return;

    // Map to page-local coordinates
    const QPointF pagePos = pageItem->mapFromScene(scenePos);
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

    updateSelectionPath(pageIndex, quads);
}

// Handle SyncTeX jump request
#ifdef HAS_SYNCTEX
void
DocumentView::handleSynctexJumpRequested(const QPointF &scenePos) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::handleSynctexJumpRequested(): Handling "
             << "SyncTeX jump to scene position" << scenePos;
#endif

    if (m_synctex_scanner)
    {
        int pageIndex                = -1;
        GraphicsPixmapItem *pageItem = nullptr;

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
DocumentView::handleTextHighlightRequested() noexcept
{
    if (m_selection_start.isNull())
        return;

    // 1. Get the page index where the selection happened
    int pageIndex = m_selection_path_item->data(0).toInt();

    // 2. Find the corresponding PageItem in the scene
    GraphicsPixmapItem *pageItem = m_page_items_hash.value(pageIndex, nullptr);
    if (!pageItem)
        return;

    m_model->highlightTextSelection(pageIndex, m_selection_start,
                                    m_selection_end);

    setModified(true);
}

// Handle text selection from GraphicsView
void
DocumentView::handleTextSelection(const QPointF &start,
                                  const QPointF &end) noexcept
{

    int pageIndex                = -1;
    GraphicsPixmapItem *pageItem = nullptr;

    if (!pageAtScenePos(start, pageIndex, pageItem))
        return; // selection start outside visible pages?

#ifndef NDEBUG
    qDebug() << "DocumentView::handleTextSelection(): Handling text selection"
             << "from" << start << "to" << end;
#endif

    // 🔴 CRITICAL FIX: map to page-local coordinates
    const QPointF pageStart = pageItem->mapFromScene(start);
    const QPointF pageEnd   = pageItem->mapFromScene(end);

    const std::vector<QPolygonF> quads
        = m_model->computeTextSelectionQuad(pageIndex, pageStart, pageEnd);

    updateSelectionPath(pageIndex, quads);
}

void
DocumentView::updateSelectionPath(int pageno,
                                  std::vector<QPolygonF> quads) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::updateSelectionPath(): Updating selection path"
             << "for page" << pageno << "with" << quads.size() << "polygons.";
#endif

    // Batch all polygons into ONE path
    QPainterPath path;
    GraphicsPixmapItem *pageItem = m_page_items_hash.value(pageno, nullptr);
    if (!pageItem)
        return;

    for (const QPolygonF &poly : quads)
    {
        // We map to scene here, or better yet, make pageItem the parent
        // of m_selection_path_item once to avoid mapping every frame.
        path.addPolygon(pageItem->mapToScene(poly));
    }

    m_selection_path_item->setPath(path);
    m_selection_path_item->show();
    m_last_selection_page = pageno;
    m_selection_path_item->setData(0, pageno); // store page number

    const auto selectionRange = m_model->getTextSelectionRange();
    m_selection_start = QPointF(selectionRange.first.x, selectionRange.first.y);
    m_selection_end = QPointF(selectionRange.second.x, selectionRange.second.y);
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
    for (int pageno : trackedPages)
    {
        m_model->invalidatePageCache(pageno);
        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);
    }

    renderVisiblePages();
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

    const double baseW = (m_model->pageWidthPts() / 72.0) * m_model->DPI();
    const double baseH = (m_model->pageHeightPts() / 72.0) * m_model->DPI();
    double rot         = static_cast<double>(m_model->rotation());
    rot                = std::fmod(rot, 360.0);
    if (rot < 0)
        rot += 360.0;

    const double t     = deg2rad(rot);
    const double c     = std::abs(std::cos(t));
    const double s     = std::abs(std::sin(t));
    const double bboxW = baseW * c + baseH * s;
    const double bboxH = baseW * s + baseH * c;

    switch (mode)
    {
        case FitMode::None:
            break;

        case FitMode::Width:
        {
            const int viewWidth = m_gview->viewport()->width();

            // Calculate using the unzoomed page size so fit is absolute.
            const double newZoom = static_cast<double>(viewWidth) / bboxW;

            setZoom(newZoom);
            renderVisiblePages();
        }
        break;

        case FitMode::Height:
        {
            const int viewHeight = m_gview->viewport()->height();

            const double newZoom = static_cast<double>(viewHeight) / bboxH;

            setZoom(newZoom);
            renderVisiblePages();
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
            renderVisiblePages();
        }
        break;

        default:
            break;
    }
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
    GraphicsPixmapItem *pageItem = m_page_items_hash[targetLocation.pageno];
    if (!pageItem)
        return;
    if (pageItem->data(0).toString() == "placeholder_page")
    {
        m_pending_jump = targetLocation;
        GotoPage(targetLocation.pageno);
        return;
    }

    QPointF targetPixelPos = m_model->toPixelSpace(
        targetLocation.pageno, {targetLocation.x, targetLocation.y});

    QPointF scenePos = pageItem->mapToScene(targetPixelPos);

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        if (m_pageno != targetLocation.pageno)
            GotoPage(targetLocation.pageno);
    }

    m_gview->centerOn(scenePos);

    m_jump_marker->showAt(scenePos.x(), scenePos.y());
    m_pending_jump = {-1, 0, 0};
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
        return;
    }

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const double x = pageno * m_page_stride + m_page_stride / 2.0;
        m_gview->centerOn(QPointF(x, m_gview->sceneRect().height() / 2.0));
    }
    else
    {
        const double y = pageno * m_page_stride + m_page_stride / 2.0;
        m_gview->centerOn(QPointF(m_gview->sceneRect().width() / 2.0, y));
    }
}

// Go to next page
void
DocumentView::GotoNextPage() noexcept
{
    if (m_pageno >= m_model->numPages() - 1)
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::GotoNextPage(): Going to next page from"
             << m_pageno;
#endif
    GotoPage(m_pageno + 1);
}

// Go to previous page
void
DocumentView::GotoPrevPage() noexcept
{
    if (m_pageno == 0)
        return;
    GotoPage(m_pageno - 1);
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
DocumentView::Search(const QString &term) noexcept
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

    emit searchBarSpinnerShow(true);
    // Check if term has atleast one uppercase letter
    bool caseSensitive = std::any_of(term.cbegin(), term.cend(),
                                     [](QChar c) { return c.isUpper(); });

    // m_search_hits = m_model->search(term);
    m_model->search(term, caseSensitive);
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

// Function that is common to zoom-in and zoom-out
void
DocumentView::zoomHelper() noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::zoomHelper(): Zooming from" << m_current_zoom
             << "to" << m_target_zoom;
#endif

    // Record the current center position as a fraction of the page pixmap.
    int anchorPageIndex            = -1;
    GraphicsPixmapItem *anchorItem = nullptr;
    QPointF anchorFrac{0.0, 0.0};
    bool hasAnchor = false;

    const QPointF centerScene
        = m_gview->mapToScene(m_gview->viewport()->rect().center());

    if (pageAtScenePos(centerScene, anchorPageIndex, anchorItem))
    {
        const QPointF localPos = anchorItem->mapFromScene(centerScene);
        const QPixmap pix      = anchorItem->pixmap();
        if (!pix.isNull() && pix.width() > 0 && pix.height() > 0)
        {
            anchorFrac = QPointF(localPos.x() / pix.width(),
                                 localPos.y() / pix.height());
            hasAnchor  = true;
        }
    }

    m_current_zoom = m_target_zoom;
    cachePageStride();
    updateSceneRect();

    // Show scrollbars after scene rect is updated so handle size is correct
    m_gview->flashScrollbars();

    const int targetPixelHeight = m_model->pageHeightPts() * m_model->DPR()
                                  * m_target_zoom * m_model->DPI() / 72.0;

    for (auto it = m_page_items_hash.begin(); it != m_page_items_hash.end();
         ++it)
    {
        int i                    = it.key();
        GraphicsPixmapItem *item = it.value();

        const QPixmap pix = item->pixmap();
        const bool isPlaceholder
            = (item->data(0).toString() == "placeholder_page");

        double pageWidthScene  = 0.0;
        double pageHeightScene = 0.0;
        if (isPlaceholder)
        {
            const QSizeF logicalSize = currentPageSceneSize();
            if (!pix.isNull() && pix.width() > 0 && pix.height() > 0)
            {
                item->setScale(1.0);
                item->setTransform(
                    QTransform::fromScale(logicalSize.width() / pix.width(),
                                          logicalSize.height() / pix.height()));
            }
            pageWidthScene  = logicalSize.width();
            pageHeightScene = logicalSize.height();
        }
        else
        {
            // Calculate scale based on ACTUAL pixmap height vs TARGET pixel
            // height This ensures the item perfectly fills the 'pixelHeight'
            // portion of the stride
            double currentPixmapHeight = item->pixmap().height();
            double perfectScale
                = static_cast<double>(targetPixelHeight) / currentPixmapHeight;
            item->setScale(perfectScale);

            pageWidthScene  = item->boundingRect().width() * item->scale();
            pageHeightScene = item->boundingRect().height() * item->scale();
        }
        const QRectF sr = m_gview->sceneRect();

        if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
        {
            const double yOffset = (sr.height() - pageHeightScene) / 2.0;
            item->setPos(i * m_page_stride, yOffset);
        }
        else if (m_layout_mode == LayoutMode::SINGLE)
        {
            const double xOffset = (sr.width() - pageWidthScene) / 2.0;
            const double yOffset = (sr.height() - pageHeightScene) / 2.0;
            item->setPos(xOffset, yOffset);
        }
        else
        {
            m_page_x_offset = (sr.width() - pageWidthScene) / 2.0;
            item->setPos(m_page_x_offset, i * m_page_stride);
        }
    }

    m_model->setZoom(m_current_zoom);

    QList<int> trackedPages = m_page_links_hash.keys();
    for (int pageno : trackedPages)
    {
        m_model->invalidatePageCache(pageno);
        clearLinksForPage(pageno);
        clearAnnotationsForPage(pageno);
        clearSearchItemsForPage(pageno);
    }

    renderSearchHitsInScrollbar();

    // Restore viewport to the same relative point within the page.
    if (hasAnchor && m_page_items_hash.contains(anchorPageIndex))
    {
        GraphicsPixmapItem *pageItem = m_page_items_hash[anchorPageIndex];
        const QPixmap pix            = pageItem->pixmap();
        if (!pix.isNull() && pix.width() > 0 && pix.height() > 0)
        {
            const QPointF restoredLocalPos(anchorFrac.x() * pix.width(),
                                           anchorFrac.y() * pix.height());
            const QPointF restoredScenePos
                = pageItem->mapToScene(restoredLocalPos);
            m_gview->centerOn(restoredScenePos);
        }
    }

    m_hq_render_timer->start();
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
#ifndef NDEBUG
    qDebug() << "DocumentView::GotoHit(): Going to search hit index:" << index;
#endif

    if (index < 0)
        index = m_search_hit_flat_refs.size() - 1;
    else if (index >= m_search_hit_flat_refs.size())
        index = 0;
    const HitRef ref = m_search_hit_flat_refs[index];

    m_search_index = index;
    m_pageno       = ref.page;

    GotoPage(ref.page);
    updateCurrentHitHighlight();
    emit searchIndexChanged(index);
    emit currentPageChanged(ref.page + 1);
}

// Scroll left by a fixed amount
void
DocumentView::ScrollLeft() noexcept
{
    m_hscroll->setValue(m_hscroll->value() - 50);
}

// Scroll right by a fixed amount
void
DocumentView::ScrollRight() noexcept
{
    m_hscroll->setValue(m_hscroll->value() + 50);
}

// Scroll up by a fixed amount
void
DocumentView::ScrollUp() noexcept
{
    m_vscroll->setValue(m_vscroll->value() - 50);
}

// Scroll down by a fixed amount
void
DocumentView::ScrollDown() noexcept
{
    m_vscroll->setValue(m_vscroll->value() + 50);
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
    const std::set<int> visiblePages = getVisiblePages();
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

    PropertiesWidget *propsWidget{nullptr};
    if (!propsWidget)
    {
        propsWidget = new PropertiesWidget(this);
        auto props  = m_model->properties();
        propsWidget->setProperties(props);
    }
    propsWidget->exec();
}

// Save the current file
void
DocumentView::SaveFile() noexcept
{
    if (m_model->SaveChanges())
        setModified(false);
    else
    {
        QMessageBox::critical(
            this, "Saving failed",
            "Could not save the current file. Try 'Save As' instead.");
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

// Highlight the current text selection
void
DocumentView::TextHighlightCurrentSelection() noexcept
{
    if (m_selection_start.isNull())
        return;

    // 1. Get the page index where the selection happened
    int pageIndex = m_selection_path_item->data(0).toInt();

    // 2. Find the corresponding PageItem in the scene
    GraphicsPixmapItem *pageItem = m_page_items_hash.value(pageIndex, nullptr);
    if (!pageItem)
        return;

    m_model->highlightTextSelection(pageIndex, m_selection_start,
                                    m_selection_end);

    setModified(true);
    // Render page where selection exists
}

// Clear keyboard hints overlay
void
DocumentView::ClearKBHintsOverlay() noexcept
{
    if (!m_gscene)
        return;

    const auto items = m_gscene->items();
    for (auto *item : items)
    {
        if (!item)
            continue;

        if (item->data(0).toString() == "kb_link_overlay")
        {
            m_gscene->removeItem(item);
            delete item;
        }
    }
}

void
DocumentView::UpdateKBHintsOverlay(const QString &input) noexcept
{
    if (!m_gscene)
        return;

    const auto items = m_gscene->items();
    for (auto *item : items)
    {
        if (!item)
            continue;

        if (auto *hintItem = qgraphicsitem_cast<LinkHint *>(item))
            hintItem->setInputPrefix(input);
    }
}

// Clear the current text selection
void
DocumentView::ClearTextSelection() noexcept
{
    if (m_selection_start.isNull())
        return;

#ifndef NDEBUG
    qDebug() << "ClearTextSelection(): Clearing text selection";
#endif

    if (m_selection_path_item)
    {
        m_selection_path_item->setPath(QPainterPath());
        m_selection_path_item->hide();
        m_selection_path_item->setData(0, -1);
    }
    m_selection_start = QPointF();
    m_selection_end   = QPointF();
}

// Yank the current text selection to clipboard
void
DocumentView::YankSelection(bool formatted) noexcept
{
    if (m_selection_start.isNull())
        return;

    const int pageIndex    = selectionPage();
    QClipboard *clipboard  = QGuiApplication::clipboard();
    const auto range       = m_model->getTextSelectionRange();
    const std::string text = m_model->getSelectedText(pageIndex, range.first,
                                                      range.second, formatted);
    clipboard->setText(text.c_str());
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

// Get the list of currently visible pages
const std::set<int> &
DocumentView::getVisiblePages() noexcept
{
    if (!m_visible_pages_dirty)
        return m_visible_pages_cache;

    m_visible_pages_cache.clear();

    if (m_model->numPages() == 0)
    {
        m_visible_pages_dirty = false;
        return m_visible_pages_cache;
    }

#ifndef NDEBUG
    qDebug() << "DocumentView::getVisiblePages(): Calculating visible pages";
#endif

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        m_visible_pages_cache.insert(
            std::clamp(m_pageno, 0, m_model->numPages() - 1));
        m_visible_pages_dirty = false;
        return m_visible_pages_cache;
    }

    const QRectF visibleSceneRect
        = m_gview->mapToScene(m_gview->viewport()->rect()).boundingRect();

    double a0, a1; // main axis interval
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

    a0 -= m_preload_margin;
    a1 += m_preload_margin;

    int firstPage = static_cast<int>(std::floor(a0 / m_page_stride));
    int lastPage  = static_cast<int>(std::floor(a1 / m_page_stride));

    firstPage = std::max(0, firstPage);
    lastPage  = std::min(m_model->numPages() - 1, lastPage);

    for (int pageno = firstPage; pageno <= lastPage; ++pageno)
        m_visible_pages_cache.insert(pageno);

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

// Render all visible pages, optionally forcing re-render
void
DocumentView::renderVisiblePages() noexcept
{
    std::set<int> visiblePages = getVisiblePages();

    prunePendingRenders(visiblePages);
    removeUnusedPageItems(visiblePages);
    // ClearTextSelection();

    for (int pageno : visiblePages)
        requestPageRender(pageno);

    updateSceneRect();
    updateCurrentHitHighlight();
}

// Render a specific page (used when LayoutMode is SINGLE)
void
DocumentView::renderPage() noexcept
{
    prunePendingRenders({m_pageno});
    removeUnusedPageItems({m_pageno});
    requestPageRender(m_pageno);

    updateSceneRect();
    updateCurrentHitHighlight();
}

void
DocumentView::prunePendingRenders(const std::set<int> &visiblePages) noexcept
{
    if (m_pending_renders.isEmpty() && m_render_queue.isEmpty())
        return;

    QSet<int> visibleSet;
    for (int pageno : visiblePages)
        visibleSet.insert(pageno);

    const int inFlightPage = m_render_in_flight ? m_render_in_flight_page : -1;

    for (auto it = m_pending_renders.begin(); it != m_pending_renders.end();)
    {
        const int pageno = *it;
        if (pageno == inFlightPage || visibleSet.contains(pageno))
        {
            ++it;
        }
        else
        {
            it = m_pending_renders.erase(it);
        }
    }

    if (m_render_queue.isEmpty())
        return;

    QQueue<int> filtered;
    while (!m_render_queue.isEmpty())
    {
        const int pageno = m_render_queue.dequeue();
        if (pageno == inFlightPage || visibleSet.contains(pageno))
            filtered.enqueue(pageno);
    }
    m_render_queue = std::move(filtered);
}

void
DocumentView::removeUnusedPageItems(const std::set<int> &visibleSet) noexcept
{
    // Copy keys first to avoid iterator invalidation
    QList<int> trackedPages = m_page_links_hash.keys();
    for (int pageno : trackedPages)
    {
        if (visibleSet.find(pageno) == visibleSet.end())
        {
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

            auto *item = m_page_items_hash.take(pageno);
            if (item)
            {
                if (item->scene() == m_gscene)
                    m_gscene->removeItem(item);
                delete item;
            }

            auto annots
                = m_page_annotations_hash.take(pageno); // removes from hash
            for (auto *annot : annots)
            {
                if (!annot)
                    continue;

                // Remove from scene if still present
                if (annot->scene() == m_gscene)
                    m_gscene->removeItem(annot);

                delete annot; // safe: we "own" these
            }

            // Remove search hits for this page
            clearSearchItemsForPage(pageno);
        }
    }
}

// Remove a page item from the scene and delete it
void
DocumentView::removePageItem(int pageno) noexcept
{
    if (m_page_items_hash.contains(pageno))
    {
        GraphicsPixmapItem *item = m_page_items_hash.take(pageno);
        if (item->scene() == m_gscene)
            m_gscene->removeItem(item);
        delete item;
    }
}

void
DocumentView::cachePageStride() noexcept
{
    const double spacingScene = m_spacing * m_current_zoom;

    double w
        = (m_model->pageWidthPts() / 72.0) * m_model->DPI() * m_current_zoom;
    double h
        = (m_model->pageHeightPts() / 72.0) * m_model->DPI() * m_current_zoom;

    double rot = static_cast<double>(m_model->rotation());
    // rot += m_model->pageRotationDeg(); // if applicable

    rot = std::fmod(rot, 360.0);
    if (rot < 0)
        rot += 360.0;

    const double t = deg2rad(rot);
    const double c = std::abs(std::cos(t));
    const double s = std::abs(std::sin(t));

    const double bboxW = w * c + h * s;
    const double bboxH = w * s + h * c;

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
        m_page_stride = bboxW + spacingScene;
    else
        m_page_stride = bboxH + spacingScene;

    m_preload_margin = m_page_stride;
    if (m_config.behavior.cache_pages > 0)
        m_preload_margin *= m_config.behavior.cache_pages;

    invalidateVisiblePagesCache();
}

// Update the scene rect based on number of pages and page stride
void
DocumentView::updateSceneRect() noexcept
{
    const double viewW = m_gview->viewport()->width();
    const double viewH = m_gview->viewport()->height();

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        // Allow scrollbars to pan within the page
        const QSizeF page   = currentPageSceneSize();
        const double sceneW = std::max(viewW, page.width());
        const double sceneH = std::max(viewH, page.height());
        m_gview->setSceneRect(0, 0, sceneW, sceneH);
        return;
    }

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const QSizeF page       = currentPageSceneSize();
        const double totalWidth = m_model->numPages() * m_page_stride;
        const double sceneH     = std::max(viewH, page.height());
        const double xMargin    = std::max(0.0, (viewW - page.width()) / 2.0);
        m_gview->setSceneRect(-xMargin, 0, totalWidth + 2.0 * xMargin, sceneH);
    }
    else
    {
        const QSizeF page        = currentPageSceneSize();
        const double totalHeight = m_model->numPages() * m_page_stride;
        const double sceneW      = std::max(viewW, page.width());
        const double yMargin     = std::max(0.0, (viewH - page.height()) / 2.0);
        m_gview->setSceneRect(0, -yMargin, sceneW, totalHeight + 2.0 * yMargin);
    }
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
DocumentView::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    if (m_config.split.focus_follows_mouse && !hasFocus())
        emit requestFocus(this);
}

void
DocumentView::handleDeferredResize() noexcept
{
    clearDocumentItems();
    if (m_layout_mode == LayoutMode::SINGLE)
        renderPage();
    else
        renderVisiblePages();

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

// Check if a scene position is within any page item
bool
DocumentView::pageAtScenePos(const QPointF &scenePos, int &outPageIndex,
                             GraphicsPixmapItem *&outPageItem) const noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::pageAtScenePos(): Checking for page at scene "
             << "position:" << scenePos;
#endif

    for (auto it = m_page_items_hash.begin(); it != m_page_items_hash.end();
         ++it)
    {
        GraphicsPixmapItem *item = it.value();
        if (!item)
            continue;

        if (item->sceneBoundingRect().contains(scenePos))
        {
            outPageIndex = it.key();
            outPageItem  = item;
            return true;
        }
    }

    outPageIndex = -1;
    outPageItem  = nullptr;
    return false;
}

void
DocumentView::clearVisiblePages() noexcept
{
    // m_page_items_hash
    for (auto it = m_page_items_hash.begin(); it != m_page_items_hash.end();
         ++it)
    {
        GraphicsPixmapItem *item = it.value();
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
    const QPoint viewPos   = m_gview->mapFromGlobal(globalPos);
    const QPointF scenePos = m_gview->mapToScene(viewPos);

    QMenu *menu    = new QMenu(this);
    auto addAction = [this, &menu](const QString &text, const auto &slot)
    {
        QAction *action = new QAction(text, menu); // sets parent = menu
        connect(action, &QAction::triggered, this, slot);
        menu->addAction(action);
    };

    const bool selectionActive
        = m_selection_path_item && !m_selection_path_item->path().isEmpty();
    const auto selectedAnnots = getSelectedAnnotations();
    const bool hasAnnots      = !selectedAnnots.empty();
    bool hasActions           = false;

    // if (selectionActive && m_selection_path_item->path().contains(scenePos))
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
DocumentView::updateCurrentHitHighlight() noexcept
{
    if (m_search_index < 0 || m_search_index >= m_search_hit_flat_refs.size())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    const HitRef ref = m_search_hit_flat_refs[m_search_index];
    const auto &hit  = m_search_hits[ref.page][ref.indexInPage];

    GraphicsPixmapItem *pageItem = m_page_items_hash.value(ref.page, nullptr);
    if (!pageItem || !pageItem->scene())
    {
        m_current_search_hit_item->setPath(QPainterPath());
        return;
    }

    QPolygonF poly;
    poly.reserve(4);
    poly << QPointF(hit.quad.ul.x * m_current_zoom,
                    hit.quad.ul.y * m_current_zoom)
         << QPointF(hit.quad.ur.x * m_current_zoom,
                    hit.quad.ur.y * m_current_zoom)
         << QPointF(hit.quad.lr.x * m_current_zoom,
                    hit.quad.lr.y * m_current_zoom)
         << QPointF(hit.quad.ll.x * m_current_zoom,
                    hit.quad.ll.y * m_current_zoom);

    QPainterPath path;
    path.addPolygon(pageItem->mapToScene(poly));

    // 4. Only update the underlying QGraphicsPathItem if the path has
    // actually changed
    if (m_current_search_hit_item->path() != path)
    {
        m_current_search_hit_item->setPath(path);

        // Center on the current hit in the view
        const QRectF hitBounds  = path.boundingRect();
        const QPointF hitCenter = hitBounds.center();
        m_gview->centerOn(hitCenter);
    }
}

void
DocumentView::updateCurrentPage() noexcept
{
    ensureVisiblePagePlaceholders();

    if (m_layout_mode == LayoutMode::SINGLE)
    {
        // The current page is explicit in single-page mode.
        emit currentPageChanged(m_pageno + 1);
        return;
    }

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const int scrollX = m_hscroll->value();
        const int viewW   = m_gview->viewport()->width();
        const int centerX = scrollX + viewW / 2;

        int page = centerX / m_page_stride;
        page     = std::clamp(page, 0, m_model->numPages() - 1);

        if (page == m_pageno)
            return;

        m_pageno = page;
        emit currentPageChanged(page + 1);
        return;
    }

    // default vertical
    const int scrollY = m_vscroll->value();
    const int viewH   = m_gview->viewport()->height();
    const int centerY = scrollY + viewH / 2;

    int page = centerY / m_page_stride;
    page     = std::clamp(page, 0, m_model->numPages() - 1);

    if (page == m_pageno)
        return;

    m_pageno = page;
    emit currentPageChanged(page + 1);
}

void
DocumentView::ensureVisiblePagePlaceholders() noexcept
{
    const std::set<int> &visiblePages = getVisiblePages();
    for (int pageno : visiblePages)
        createAndAddPlaceholderPageItem(pageno);
}

void
DocumentView::clearDocumentItems() noexcept
{
    invalidateVisiblePagesCache();
    for (QGraphicsItem *item : m_gscene->items())
    {
        if (item != m_jump_marker && item != m_selection_path_item
            && item != m_current_search_hit_item)
        {
            m_gscene->removeItem(item);
            delete item;
        }
    }

    m_page_items_hash.clear();
    ClearTextSelection();
    m_search_items.clear();
    m_page_links_hash.clear();
    m_page_annotations_hash.clear();
    m_pending_renders.clear();
    m_render_queue.clear();
    m_render_in_flight      = false;
    m_render_in_flight_page = -1;
}

// Request rendering of a specific page (ASYNC)
void
DocumentView::requestPageRender(int pageno) noexcept
{
    // if (m_page_items_hash.contains(pageno))
    //     return;

    if (m_pending_renders.contains(pageno))
        return;

    m_pending_renders.insert(pageno);
    createAndAddPlaceholderPageItem(pageno);

    m_render_queue.enqueue(pageno);
    startNextRenderJob();
}

void
DocumentView::startNextRenderJob() noexcept
{
    if (m_render_in_flight)
        return;

    while (!m_render_queue.isEmpty())
    {
        const int pageno = m_render_queue.dequeue();
        if (!m_pending_renders.contains(pageno))
            continue;

        m_render_in_flight      = true;
        m_render_in_flight_page = pageno;
        auto job                = m_model->createRenderJob(pageno);

        m_model->requestPageRender(
            job, [this, pageno](const Model::PageRenderResult &result)
        {
            m_pending_renders.remove(pageno);
            m_render_in_flight      = false;
            m_render_in_flight_page = -1;

            const QImage &image = result.image;
            if (!image.isNull())
            {
                const std::set<int> &visiblePages = getVisiblePages();
                if (visiblePages.find(pageno) != visiblePages.end())
                {
                    renderPageFromImage(pageno, result.image);
                    renderLinks(pageno, result.links);
                    renderAnnotations(pageno, result.annotations);
                    renderSearchHitsForPage(pageno);
                }

                if (m_pending_jump.pageno == pageno)
                {
                    GotoLocation(m_pending_jump);
                }

                // If the page we just rendered is the page in the current
                // search.
                if (m_search_index != -1 && !m_search_hit_flat_refs.empty()
                    && m_search_hit_flat_refs[m_search_index].page == pageno)
                {
                    updateCurrentHitHighlight();
                }
            }

            startNextRenderJob();
        });
        break;
    }
}

void
DocumentView::renderPageFromImage(int pageno, const QImage &image) noexcept
{
    clearLinksForPage(pageno);
    clearAnnotationsForPage(pageno);
    removePageItem(pageno);
    createAndAddPageItem(pageno, QPixmap::fromImage(image));
}

void
DocumentView::createAndAddPlaceholderPageItem(int pageno) noexcept
{
    if (m_page_items_hash.contains(pageno))
        return;

    const QSizeF logicalSize = currentPageSceneSize();
    if (logicalSize.isEmpty())
        return;

    QPixmap pix(1, 1);
    pix.fill(m_model->invertColor() ? Qt::black : Qt::white);

    auto *item = new GraphicsPixmapItem();
    item->setPixmap(pix);
    item->setTransform(
        QTransform::fromScale(logicalSize.width() / pix.width(),
                              logicalSize.height() / pix.height()));

    const double pageW = logicalSize.width();
    const double pageH = logicalSize.height();
    const QRectF sr    = m_gview->sceneRect();

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const double yOffset = (sr.height() - pageH) / 2.0;
        const double xPos    = pageno * m_page_stride;
        item->setPos(xPos, yOffset);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        const double xOffset = (sr.width() - pageW) / 2.0;
        const double yOffset = (sr.height() - pageH) / 2.0;
        item->setPos(xOffset, yOffset);
    }
    else
    {
        const double xOffset = (sr.width() - pageW) / 2.0;
        const double yPos    = pageno * m_page_stride;
        item->setPos(xOffset, yPos);
    }

    m_gscene->addItem(item);
    m_page_items_hash[pageno] = item;
    item->setData(0, QStringLiteral("placeholder_page"));
}

void
DocumentView::createAndAddPageItem(int pageno, const QPixmap &pix) noexcept
{
    auto *item = new GraphicsPixmapItem();
    item->setPixmap(pix);

    const double pageW = pix.width() / pix.devicePixelRatio();
    const double pageH = pix.height() / pix.devicePixelRatio();

    const QRectF sr = m_gview->sceneRect();

    if (m_layout_mode == LayoutMode::LEFT_TO_RIGHT)
    {
        const double yOffset = (sr.height() - pageH) / 2.0;
        const double xPos    = pageno * m_page_stride;
        item->setPos(xPos, yOffset);
    }
    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        // Always place the current page in the center-ish of the viewport
        // scene.
        const double xOffset = (sr.width() - pageW) / 2.0;
        const double yOffset = (sr.height() - pageH) / 2.0;
        item->setPos(xOffset, yOffset);
    }
    else
    {
        const double xOffset = (sr.width() - pageW) / 2.0;
        const double yPos    = pageno * m_page_stride;
        item->setPos(xOffset, yPos);
    }

    m_gscene->addItem(item);
    m_page_items_hash[pageno] = item;
}

void
DocumentView::renderLinks(int pageno,
                          const std::vector<Model::RenderLink> &links) noexcept
{
    if (m_page_links_hash.contains(pageno))
        return;

    clearLinksForPage(pageno);
    GraphicsPixmapItem *pageItem = m_page_items_hash[pageno];

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

    clearAnnotationsForPage(pageno);
    GraphicsPixmapItem *pageItem = m_page_items_hash[pageno];
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

void
DocumentView::reloadPage(int pageno) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::reloadPage(): Reloading page:" << pageno;
#endif
    removePageItem(pageno);
    requestPageRender(pageno);
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
    if (m_model->passwordRequired())
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

            if (authenticate(password))
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

    const auto hits = m_search_hits.value(pageno); // Local copy

    // 2. Validate the Page Item still exists in the scene
    GraphicsPixmapItem *pageItem = m_page_items_hash[pageno];

    if (!pageItem)
        return;

    QGraphicsPathItem *item = ensureSearchItemForPage(pageno);
    if (!item)
        return;

    QPainterPath allPath;

    for (int i = 0; i < hits.size(); ++i)
    {
        const Model::SearchHit &hit = hits[i];
        QPolygonF poly;
        poly << QPointF(hit.quad.ul.x * m_current_zoom,
                        hit.quad.ul.y * m_current_zoom)
             << QPointF(hit.quad.ur.x * m_current_zoom,
                        hit.quad.ur.y * m_current_zoom)
             << QPointF(hit.quad.lr.x * m_current_zoom,
                        hit.quad.lr.y * m_current_zoom)
             << QPointF(hit.quad.ll.x * m_current_zoom,
                        hit.quad.ll.y * m_current_zoom);

        allPath.addPolygon(pageItem->mapToScene(poly));
    }

    // Set colors
    item->setPath(allPath);
    item->setBrush(rgbaToQColor(m_config.colors.search_match));
}

// Render search hits in the scrollbar
void
DocumentView::renderSearchHitsInScrollbar() noexcept
{
    // Clear markers for both scrollbars first
    m_vscroll->setSearchMarkers({});
    m_hscroll->setSearchMarkers({});

    if (m_search_hit_flat_refs.empty())
        return;

    std::vector<double> search_markers_pos;
    search_markers_pos.reserve(m_search_hit_flat_refs.size());

    // Scale factor to convert PDF points to current scene pixels
    const double pdfToSceneScale = m_model->viewScale();

    switch (m_layout_mode)
    {
        case LayoutMode::SINGLE:
        case LayoutMode::TOP_TO_BOTTOM:
        {
            // Vertical scrolling - calculate Y positions
            for (const auto &hitRef : m_search_hit_flat_refs)
            {
                const auto &hit
                    = m_search_hits[hitRef.page][hitRef.indexInPage];

                // 1. Calculate the start of the page in the scene
                double pageTopInScene = hitRef.page * m_page_stride;

                // 2. Calculate the Y offset within the page
                double yOffsetInScene = hit.quad.ul.y * pdfToSceneScale;

                search_markers_pos.push_back(pageTopInScene + yOffsetInScene);
            }
            m_vscroll->setSearchMarkers(search_markers_pos);
            break;
        }

        case LayoutMode::LEFT_TO_RIGHT:
        {
            // Horizontal scrolling - calculate X positions
            for (const auto &hitRef : m_search_hit_flat_refs)
            {
                const auto &hit
                    = m_search_hits[hitRef.page][hitRef.indexInPage];

                // 1. Calculate the start of the page in the scene
                // (horizontally)
                double pageLeftInScene = hitRef.page * m_page_stride;

                // 2. Calculate the X offset within the page
                double xOffsetInScene = hit.quad.ul.x * pdfToSceneScale;

                search_markers_pos.push_back(pageLeftInScene + xOffsetInScene);
            }
            m_hscroll->setSearchMarkers(search_markers_pos);
            break;
        }

        default:
            break;
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
    if (m_selection_start.isNull())
        return;

    // Re-apply the selection path item
    if (m_selection_path_item)
        m_selection_path_item->show();
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
        renderVisiblePages();
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
DocumentView::handleAnnotSelectRequested(const QRectF &sceneRect) noexcept
{
    int pageno;
    GraphicsPixmapItem *pageItem;
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
DocumentView::handleAnnotSelectRequested(const QPointF &scenePos) noexcept
{
    int pageno;
    GraphicsPixmapItem *pageItem;
    if (!pageAtScenePos(scenePos, pageno, pageItem))
        return;

    const QPointF searchPos = pageItem->mapFromScene(scenePos);
    const auto annotAtPoint = annotationAtPoint(pageno, searchPos);

    if (!annotAtPoint)
        return;

    annotAtPoint->select(Qt::black);
}

std::vector<Annotation *>
DocumentView::annotationsInArea(int pageno, const QRectF &area) noexcept
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
DocumentView::annotationAtPoint(int pageno, const QPointF &point) noexcept
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
    GraphicsPixmapItem *pageItem;
    QPointF sceneCenter = m_gview->mapToScene(
        m_gview->viewport()->width() / 2, m_gview->viewport()->height() / 2);

    if (!pageAtScenePos(sceneCenter, pageno, pageItem))
        return {-1, 0, 0};

    const QPointF &pageLocalPos = pageItem->mapFromScene(sceneCenter);
    return {pageno, (float)pageLocalPos.x(), (float)pageLocalPos.y()};
}

namespace
{
bool
mapRegionToPageRects(const QRectF &area, GraphicsPixmapItem *pageItem,
                     QRectF &outLogical, QRect &outPixels) noexcept
{
    if (!pageItem)
        return false;

    const QRectF pageRect = pageItem->mapFromScene(area).boundingRect();
    const qreal dpr       = pageItem->pixmap().devicePixelRatio();
    const QSize pixSize   = pageItem->pixmap().size();
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
DocumentView::CopyTextFromRegion(const QRectF &area) noexcept
{
    int pageno;
    GraphicsPixmapItem *pageItem;
    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    // 🔴 CRITICAL FIX: map to page-local coordinates
    const QPointF pageStart = pageItem->mapFromScene(area.topLeft());
    const QPointF pageEnd   = pageItem->mapFromScene(area.bottomRight());

    const std::string text = m_model->getTextInArea(pageno, pageStart, pageEnd);

    QClipboard *clip = QApplication::clipboard();
    clip->setText(QString::fromStdString(text));
}

void
DocumentView::CopyRegionAsImage(const QRectF &area) noexcept
{
    int pageno;
    GraphicsPixmapItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;
    const QImage img = pageItem->pixmap().copy(pixelRect).toImage();

    if (!img.isNull())
    {
        QClipboard *clip = QApplication::clipboard();
        clip->setImage(img);
    }
}

void
DocumentView::SaveRegionAsImage(const QRectF &area) noexcept
{

    int pageno;
    GraphicsPixmapItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;
    const QImage img = pageItem->pixmap().copy(pixelRect).toImage();

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
DocumentView::OpenRegionInExternalViewer(const QRectF &area) noexcept
{
    int pageno;
    GraphicsPixmapItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    QRectF pageRect;
    QRect pixelRect;
    if (!mapRegionToPageRects(area, pageItem, pageRect, pixelRect))
        return;
    openImageInExternalViewer(pageItem->pixmap().copy(pixelRect).toImage());
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

bool
DocumentView::waitUntilReadableAsync() noexcept
{
    const QString filepath = m_model->filePath();
    QFileInfo a(filepath);
    if (!a.exists() || a.size() == 0)
        return false;

    QThread::msleep(80);

    QFileInfo b(filepath);
    return b.exists() && a.size() == b.size();
}

void
DocumentView::onFileReloadRequested(const QString &path) noexcept
{
    if (path != m_model->filePath())
        return;

    tryReloadLater(0);
}

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
            renderVisiblePages();
            emit totalPageCountChanged(m_model->m_page_count);
#ifdef HAS_SYNCTEX
            initSynctex();
#endif
        }

        const QString &filepath = m_model->filePath();
        // IMPORTANT: file may have been removed and replaced → watcher loses it
        if (m_file_watcher && !m_file_watcher->files().contains(filepath))
            m_file_watcher->addPath(filepath);

        return;
    }

    QTimer::singleShot(100, this,
                       [this, attempt]() { tryReloadLater(attempt + 1); });
}

void
DocumentView::handleRegionSelectRequested(const QRectF &area) noexcept
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
DocumentView::handleAnnotRectRequested(const QRectF &area) noexcept
{
    int pageno;
    GraphicsPixmapItem *pageItem;

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
DocumentView::handleAnnotPopupRequested(const QPointF &scenePos) noexcept
{
    int pageno;
    GraphicsPixmapItem *pageItem;

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

    // Create a small rect at the click position for the text annotation icon
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
