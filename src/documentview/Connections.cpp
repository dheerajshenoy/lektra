#include "DocumentView.hpp"

#include <QMovie>

// Annotations
#include "Annotations/HighlightAnnotation.hpp"
#include "Annotations/PopupAnnotation.hpp"
#include "Annotations/RectAnnotation.hpp"
#include "BrowseLinkItem.hpp"

// Commands
#include "Commands/AnnotColorCommand.hpp"
#include "Commands/AnnotCommentCommand.hpp"
#include "Commands/DeleteAnnotationsCommand.hpp"
#include "Commands/RectAnnotationCommand.hpp"
#include "Commands/TextAnnotationCommand.hpp"

// Other
#include "Config.hpp"
#include "DispatchType.hpp"
#include "DocumentContainer.hpp"
#include "GraphicsImageItem.hpp"
#include "GraphicsView.hpp"
#include "InputDialog.hpp"
#include "Lektra.hpp"
#include "LinkHint.hpp"
#include "PropertiesWidget.hpp"
#include "WaitingSpinnerWidget.hpp"
#include "utils.hpp"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
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
#ifdef WITH_SYNCTEX

void
DocumentView::initSynctex() noexcept
{
    if (m_model->fileType() != Model::FileType::PDF)
        return;

    if (m_synctex_scanner)
    {
        synctex_scanner_free(m_synctex_scanner);
        m_synctex_scanner = nullptr;
    }

    const QByteArray pathBytes = m_model->filePath().toUtf8();
    const std::string filePathStr(pathBytes.constData(), pathBytes.size());
    m_synctex_scanner
        = synctex_scanner_new_with_output_file(filePathStr.c_str(), nullptr, 1);
    if (!m_synctex_scanner)
        return;

    #ifndef NDEBUG
    qDebug()
        << "DocumentView::initSynctex(): Initialized SyncTeX scanner for file"
        << m_model->filePath();
    #endif
}
#endif

void
DocumentView::connectModelFailureSignals() noexcept
{
    connect(m_model, &Model::openFileFailed, this,
            &DocumentView::handleOpenFileFailed, Qt::UniqueConnection);

    connect(m_model, &Model::passwordRequired, this,
            &DocumentView::handle_password_required, Qt::UniqueConnection);

    connect(m_model, &Model::wrongPassword, this,
            &DocumentView::handle_wrong_password, Qt::UniqueConnection);
}

void
DocumentView::openAsync(const QString &filePath) noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::openAsync(): Opening file:" << filePath;
#endif

    CloseFile();

    // CloseFile() -> resetConnections() does a blanket
    // m_model->disconnect(this) that also severs these — re-establish them for
    // this open attempt.
    connectModelFailureSignals();

    m_gview->forceHideScrollbars();
    m_spinner->start();
    m_spinner->show();

    // Options for this kind of document must be in place before it opens:
    // MuPDF styles reflowable text (EPUB font, line spacing) while loading.
    applyFiletypeOverrides(filePath);
    m_model->setReflowStyle(m_config.reflow.font_family,
                            m_config.reflow.line_spacing);
    m_reflow_style_key = QString("%1|%2").arg(m_config.reflow.font_family)
                             .arg(m_config.reflow.line_spacing);

    // Order matters: disconnect any previous watcher connection FIRST, then
    // establish the new one, THEN attach the future. If setFuture ran
    // before we reconnected, a rapid consecutive openAsync could see the
    // previous watcher's `finished` race the new connect and either fire
    // handleOpenFileFinished twice or against stale model state.
    m_open_future_watcher.disconnect(this);
    connect(&m_open_future_watcher, &QFutureWatcher<void>::finished, this,
            &DocumentView::handleOpenFileFinished, Qt::SingleShotConnection);
    QFuture<void> future = m_model->openAsync(QDir::cleanPath(filePath));
    m_open_future_watcher.setFuture(future);
}

void
DocumentView::handleOpenFileFailed() noexcept
{
    m_spinner->stop();
    m_spinner->hide();
    m_gview->forceHideScrollbars();

    // Captured before emitting openFileFailed(this) below — Lektra's
    // handler for that signal calls CloseFile(), which clears the model's
    // filepath, so the name must be grabbed now to show in the tab's
    // content area.
    const QString name = fileName();
    m_gview->setOpenFailedMessage(
        name.isEmpty()
            ? tr("Failed to open file.\nPlease check if it exists and is a "
                 "supported format.")
            : tr("Failed to open \"%1\".\nPlease check if it exists and is "
                 "a supported format.")
                  .arg(name));

    // No blocking modal dialog here anymore — the failure is already
    // visible in-place (this message, plus the red tab title Lektra sets in
    // response to the signal below), without stalling the whole app on a
    // dismiss click.
    emit openFileFailed(this);
}

void
DocumentView::handleOpenFileFinished() noexcept
{
#ifndef NDEBUG
    qDebug()
        << "DocumentView::handleOpenFileFinished(): File opened successfully";
#endif

    // This future-watcher slot fires whenever the QFuture completes,
    // including the trivial no-op future used by Model::openAsync()'s
    // FileType::NONE fast-fail path — i.e. it fires for failed opens too,
    // racing handleOpenFileFailed() (which sets the message below). Only
    // clear it here once we know this was an actual success.
    if (!m_model->success())
    {
        m_spinner->stop();
        m_spinner->hide();
        m_pending_page = -1;
        return;
    }

    m_gview->setOpenFailedMessage(QString());

    stopGifPlayback();

    m_pageno = 0;

    if (m_model->isImage())
    {
        setLayoutMode(LayoutMode::SINGLE);
        initConnections();

        // Always defer fitmode to next event loop tick so geometry is settled
        setFitMode(m_config.layout.initial_fit);

        if (m_model->isAnimated())
        {
            startGifPlayback();
        }
        // QTimer::singleShot(0, this, [this]() { renderImage(); });

        // Image rendering above happens synchronously, so content is
        // already showing by this point.
        m_spinner->stop();
        m_spinner->hide();
    }
    else
    {
        // Block scroll signals to prevent jumping during layout swap
        m_vscroll->blockSignals(true);
        m_hscroll->blockSignals(true);

        // First, clear old items and caches
        clearDocumentItems();
        invalidateVisiblePagesCache();
        if (!m_thumbnail_mode)
            setLayoutMode(m_config.layout.mode);

        m_vscroll->blockSignals(false);
        m_hscroll->blockSignals(false);

        initConnections();

        // Rendering the first page is dispatched asynchronously below, so
        // keep the spinner up until that first page image actually arrives
        // (see startNextRenderJob()) instead of hiding it now, which would
        // leave a blank view (just scrollbars) for however long the render
        // takes.
        m_awaiting_first_render = true;

        // Always defer fitmode to next event loop tick so geometry is settled
        QTimer::singleShot(0, this, [this]()
        {
            setFitMode(m_config.layout.initial_fit);
            renderPages();
            if (m_pending_page >= 0)
            {
                GotoPage(m_pending_page);
                m_pending_page = -1;
            }
        });

        // Reflowable documents (EPUB/FB2/MOBI) start at MuPDF's built-in
        // default layout (420x595pt @ 11pt em) until the user explicitly
        // asks for a different font size via
        // ReflowFontSizeIncrease()/Decrease() — reflow is opt-in, not
        // automatic.
        m_reflow_em = kReflowFontSizeDefault;
        applyReflowStyle(true);

#ifdef WITH_SYNCTEX
        if (m_model->fileType() == Model::FileType::PDF)
        {
            initSynctex();
        }
#endif
    }

    setAutoReload(m_config.behavior.auto_reload);
    emit openFileFinished(this, m_model->fileType());

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnFileOpen);
#endif
}

void
DocumentView::startGifPlayback() noexcept
{
    if (m_thumbnail_mode || !m_model->isAnimated())
        return;

    QMovie *movie = m_model->movie();
    if (!movie || movie->state() == QMovie::Running)
        return;

    connect(movie, &QMovie::frameChanged, this,
            [this](int /* frame */) { renderImage(); });
    movie->start();
}

void
DocumentView::stopGifPlayback() noexcept
{
    QMovie *movie = m_model->movie();
    if (!movie)
        return;

    movie->stop();
    movie->disconnect(this);
}

void
DocumentView::resetConnections() noexcept
{
#ifndef NDEBUG
    qDebug() << "DocumentView::resetConnections(): Clearing existing "
                "connections";
#endif

    // Disconnect specific objects that signal INTO this DocumentView
    if (m_model)
    {
        m_model->disconnect(this);
    }

    if (m_gview)
    {
        m_gview->disconnect(this);
    }

    // Disconnect UI elements that are layout-dependent
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

#ifdef WITH_SYNCTEX
    connect(m_gview, &GraphicsView::synctexJumpRequested, this,
            &DocumentView::handleSynctexJumpRequested);
#endif

#ifdef WITH_LUA
    connect(m_gview, &GraphicsView::modeChanged, this,
            [this](GraphicsView::Mode) { dispatchLuaEvent(DispatchType::OnModeChanged); });
#endif

    if (m_model->isImage())
    {
        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderImage);

        connect(m_gview, &GraphicsView::zoomInRequested, this,
                &DocumentView::ZoomIn);

        // Pinch zoom
        connect(m_gview, &GraphicsView::zoomRequested, this,
                [this](float factor, QPointF anchorScenePos)
        { setZoomAnchored(m_current_zoom * factor, anchorScenePos); });

        connect(m_gview, &GraphicsView::zoomOutRequested, this,
                &DocumentView::ZoomOut);

        connect(m_gview, &GraphicsView::regionSelectRequested, this,
                &DocumentView::handleRegionSelectRequested);

        return;
    }

    if (m_thumbnail_mode)
    {
        connect(m_vscroll, &QScrollBar::valueChanged, this,
                &DocumentView::handleVScrollValueChanged);
        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPages);
        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::refreshVisiblePages);
        return;
    }

    // Only wire up what this kind of document can produce.
    if (m_model->supports_annotations())
        connect(m_model, &Model::undoStackCleanChanged, this,
                [this](bool clean) { setModified(!clean); });

    if (m_model->supports_text_search())
    {
        connect(m_model, &Model::searchResultsReady, this,
                &DocumentView::handleSearchResults);

        connect(m_model, &Model::searchPartialResultsReady, this,
                &DocumentView::handlePartialSearchResults);
    }

    if (m_model->supports_links())
        connect(m_model, &Model::urlLinksReady, this,
                [this](int pageno, std::vector<Model::RenderLink> links)
        { renderLinks(pageno, links, true); });

    connect(m_model, &Model::reloadRequested, this,
            &DocumentView::handleReloadRequested, Qt::UniqueConnection);

    connect(m_model, &Model::reloadPasswordRequired, this,
            &DocumentView::handleReloadPasswordRequired, Qt::UniqueConnection);

    connect(m_model, &Model::documentRelayouted, this,
            &DocumentView::handleDocumentRelayouted, Qt::UniqueConnection);

    if (m_layout_mode == LayoutMode::HORIZONTAL)
    {
        connect(m_hscroll, &QScrollBar::valueChanged, this,
                &DocumentView::handleHScrollValueChanged, Qt::UniqueConnection);

        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPages, Qt::UniqueConnection);

        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::refreshVisiblePages, Qt::UniqueConnection);
    }
    else if (m_layout_mode == LayoutMode::VERTICAL
             || m_layout_mode == LayoutMode::BOOK)
    {
        connect(m_vscroll, &QScrollBar::valueChanged, this,
                &DocumentView::handleVScrollValueChanged, Qt::UniqueConnection);

        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPages, Qt::UniqueConnection);

        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::refreshVisiblePages, Qt::UniqueConnection);
    }

    else if (m_layout_mode == LayoutMode::SINGLE)
    {
        connect(m_hq_render_timer, &QTimer::timeout, this,
                &DocumentView::renderPage, Qt::UniqueConnection);

        // Zooming only rescales the page that is already there; the sharp
        // render follows once the zoom settles.
        connect(m_scroll_page_update_timer, &QTimer::timeout, this,
                &DocumentView::renderPage, Qt::UniqueConnection);

        // A deeply zoomed page is only rendered around the visible part, so
        // scrolling can leave that part and needs a fresh render.
        auto refreshRegion = [this]
        {
            if (regionNeedsRefresh(m_pageno))
                m_scroll_page_update_timer->start();
        };
        connect(m_vscroll, &QScrollBar::valueChanged, this, refreshRegion);
        connect(m_hscroll, &QScrollBar::valueChanged, this, refreshRegion);
    }

    /* Graphics View Signals */
    if (m_model->supports_text_selection())
        connect(m_gview, &GraphicsView::textHighlightRequested, this,
                &DocumentView::handleTextHighlightRequested);

    connect(m_gview, &GraphicsView::zoomInRequested, this,
            &DocumentView::ZoomIn);

    // Pinch zoom
    connect(m_gview, &GraphicsView::zoomRequested, this,
            [this](float factor, QPointF anchorScenePos)
    { setZoomAnchored(m_current_zoom * factor, anchorScenePos); });

    connect(m_gview, &GraphicsView::zoomOutRequested, this,
            &DocumentView::ZoomOut);

    // Annotation handlers are only wired up for documents that can have
    // annotations; otherwise the signals have no receiver at all.
    if (m_model->supports_annotations())
    {
        connect(m_gview,
                QOverload<QRectF>::of(&GraphicsView::annotSelectRequested),
                this, [this](QRectF sceneRect)
        { handleAnnotSelectRequested(sceneRect); });

        connect(m_gview,
                QOverload<QPointF>::of(&GraphicsView::annotSelectRequested),
                this, [this](QPointF scenePos)
        { handleAnnotSelectRequested(scenePos); });

        connect(m_gview, &GraphicsView::annotSelectClearRequested, this,
                &DocumentView::handleAnnotSelectClearRequested);

        connect(m_gview, &GraphicsView::annotRectRequested, this,
                &DocumentView::handleAnnotRectRequested);

        connect(m_gview, &GraphicsView::annotPopupRequested, this,
                &DocumentView::handleAnnotPopupRequested);
    }

    if (m_model->supports_text_selection())
    {
        connect(m_gview, &GraphicsView::textSelectionRequested, this,
                &DocumentView::handleTextSelection);

        connect(m_gview, &GraphicsView::textSelectionDeletionRequested, this,
                &DocumentView::ClearTextSelection);

        connect(m_gview, &GraphicsView::clickRequested, this,
                &DocumentView::handleClickSelection);
    }

    connect(m_gview, &GraphicsView::contextMenuRequested, this,
            &DocumentView::handleContextMenuRequested);

    connect(m_gview, &GraphicsView::regionSelectRequested, this,
            &DocumentView::handleRegionSelectRequested);

    if (m_model->supports_links())
    {
        connect(m_gview, &GraphicsView::linkCtrlClickRequested, this,
                &DocumentView::handleLinkCtrlClickRequested);

        connect(m_gview, &GraphicsView::linkPreviewRequested, this,
                &DocumentView::handleLinkPreviewRequested);

        connect(m_gview, &GraphicsView::linkMiddleClickRequested, this,
                &DocumentView::handleLinkMiddleClickRequested);
    }
}
#ifdef WITH_SYNCTEX

// Handle SyncTeX jump request
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
        fz_point pdfPos       = {float(pagePos.x()), float(pagePos.y())};

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
            QMessageBox::warning(this, tr("SyncTeX Error"),
                                 tr("No matching source found!"));
        }
    }
    else
    {
        QMessageBox::warning(this, tr("SyncTex"),
                             tr("Not a valid synctex document"));
    }
}
#endif
#ifdef WITH_SYNCTEX

void
DocumentView::synctexLocateInDocument(const char *texFileName,
                                      int line) noexcept
{
    QString tmp = m_config.synctex.editor_command;
    if (!tmp.contains("%f") || !tmp.contains("%l"))
    {
        QMessageBox::critical(this, tr("SyncTeX error"),
                              tr("Invalid SyncTeX editor command: missing "
                                 "placeholders (%l and/or %f)."));
        return;
    }

    auto args   = QProcess::splitCommand(tmp);
    auto editor = args.takeFirst();
    args.replaceInStrings("%l", QString::number(line));
    args.replaceInStrings("%f", texFileName);

    QProcess::startDetached(editor, args);
}
#endif
#ifdef WITH_SYNCTEX

void
DocumentView::synctexForwardSearch(const QString &texPath, int line,
                                   int col) noexcept
{
    if (!m_synctex_scanner)
    {
        qWarning()
            << "DocumentView::synctexForwardSearch(): no synctex scanner";
        return;
    }

    const int hits = synctex_display_query(
        m_synctex_scanner, texPath.toUtf8().constData(), line, col, -1);

    if (hits <= 0)
    {
        qWarning() << "DocumentView::synctexForwardSearch(): no result for"
                   << texPath << "line" << line;
        return;
    }

    synctex_node_p node = synctex_scanner_next_result(m_synctex_scanner);
    if (!node)
        return;

    const int page = synctex_node_page(node) - 1; // 0-based
    const float x  = synctex_node_box_visible_h(node);
    const float y  = synctex_node_box_visible_v(node);

    GotoLocation({page, x, y});
}
#endif

// Show file properties dialog
void
DocumentView::FileProperties() noexcept
{
    const auto props = m_model->properties();
    if (props.empty())
    {
        QMessageBox::information(
            this, tr("No properties"),
            tr("No metadata properties available for this file."));
        return;
    }
    PropertiesWidget *propsWidget = new PropertiesWidget(this);
    propsWidget->setProperties(props);
    propsWidget->exec();
}

// Save the current file
void
DocumentView::SaveFile() noexcept
{
    if (!m_is_modified || !m_model->supports_save())
        return;

#ifndef NDEBUG
    qDebug() << "DocumentView::SaveFile(): Saving file with unsaved changes.";
#endif

    stopPendingRenders();

    if (m_model->SaveChanges())
    {
        {
            // clearDocumentItems();
            // cachePageStride();
            // updateSceneRect();
            invalidateVisiblePagesCache();
            renderPages();
        }

        m_model->undoStack()->setClean();

#ifndef NDEBUG
        qDebug() << "DocumentView::SaveFile(): Save successful";
#endif
#ifdef WITH_LUA
        dispatchLuaEvent(DispatchType::OnFileSaved);
#endif
    }
    else
    {
        QMessageBox::critical(
            this, tr("Saving failed"),
            tr("Could not save the current file. Try 'Save As' instead."));
    }
}

// Save the current file as a new file
void
DocumentView::SaveAsFile() noexcept
{
    if (!m_model->supports_save())
        return;

    const QString filename
        = QFileDialog::getSaveFileName(this, "Save as", QString());

    if (filename.isEmpty())
        return;

    if (!m_model->SaveAs(filename))
    {
        QMessageBox::critical(
            this, tr("Saving as failed"),
            tr("Could not perform save as operation on the file"));
        return;
    }
#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnFileSaved);
#endif
}

// Close the current file
void
DocumentView::CloseFile() noexcept
{
    stopGifPlayback();

    // Disconnect watcher before cancelling to prevent handleOpenFileFinished
    // firing for the old future
    // m_open_future_watcher.disconnect();
    m_open_future_watcher.cancel();

    stopPendingRenders();
    clearDocumentItems();
    resetConnections();
    m_model->close();
    m_awaiting_first_render = false;

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnFileClose);
#endif
}

// Re-paginate a reflowable document (EPUB/FB2/MOBI) at the given font
// size (layout_em, in points), saving the current reading position as a
// fraction so handleDocumentRelayouted() can restore roughly the same
// place once the new pagination is live. Only called explicitly, from
// ReflowFontSizeIncrease()/Decrease() — not wired to resize or
// document-open, so a document's pagination never changes underneath the
// user without them asking for it. Deliberately keeps the page box size
// (width/height) exactly as it currently is — Model::layoutWidthPts()/
// layoutHeightPts() — so changing text size never also changes the page
// size; only em varies.
void
DocumentView::applyReflow(float em) noexcept
{
    if (!m_model || !m_model->supports_reflow())
        return;

    const int pageCount = m_model->numPages();
    m_relayout_saved_fraction
        = pageCount > 0 ? double(m_pageno) / double(pageCount) : 0.0;

    m_model->relayoutForViewport(m_model->layoutWidthPts(),
                                 m_model->layoutHeightPts(), em);
}

// Applies the [reflow] options (font, size, line spacing). At open it does
// nothing unless something differs from MuPDF's defaults, so documents are
// only re-paginated when the user asked for a style.
void
DocumentView::applyReflowStyle(bool atOpen) noexcept
{
    if (!m_model || !m_model->supports_reflow())
        return;

    const auto &e = m_config.reflow;
    if (atOpen && qFuzzyCompare(e.font_size, kReflowFontSizeDefault))
        return;

    m_reflow_em = std::clamp(e.font_size, kReflowFontSizeMin,
                             kReflowFontSizeMax);

    // Font and line spacing are applied while MuPDF loads the text, so
    // changing them on an open document means loading it again.
    const QString key
        = QString("%1|%2").arg(e.font_family).arg(e.line_spacing);
    if (!atOpen && key != m_reflow_style_key)
    {
        m_model->setReflowStyle(e.font_family, e.line_spacing);
        m_reflow_style_key = key;
        reloadFile();
        return;
    }

    applyReflow(m_reflow_em);
}

void
DocumentView::ReflowFontSizeIncrease() noexcept
{
    if (!m_model || !m_model->supports_reflow())
        return;

    m_reflow_em = std::clamp(m_reflow_em + kReflowFontSizeStep,
                             kReflowFontSizeMin, kReflowFontSizeMax);
    applyReflow(m_reflow_em);
}

void
DocumentView::ReflowFontSizeDecrease() noexcept
{
    if (!m_model || !m_model->supports_reflow())
        return;

    m_reflow_em = std::clamp(m_reflow_em - kReflowFontSizeStep,
                             kReflowFontSizeMin, kReflowFontSizeMax);
    applyReflow(m_reflow_em);
}

void
DocumentView::ReflowFontSizeReset() noexcept
{
    if (!m_model || !m_model->supports_reflow())
        return;

    m_reflow_em = std::clamp(m_config.reflow.font_size, kReflowFontSizeMin,
                             kReflowFontSizeMax);
    applyReflow(m_reflow_em);
}

void
DocumentView::handleDocumentRelayouted() noexcept
{
    const int newPageCount = m_model->numPages();
    emit totalPageCountChanged(newPageCount);

    const int targetPage
        = newPageCount > 0
              ? qBound(0, qRound(m_relayout_saved_fraction * newPageCount),
                       newPageCount - 1)
              : 0;

    m_vscroll->blockSignals(true);
    m_hscroll->blockSignals(true);

    clearDocumentItems();
    invalidateVisiblePagesCache();
    cachePageStride();
    updateSceneRect();

    m_vscroll->blockSignals(false);
    m_hscroll->blockSignals(false);

    m_pageno = -1; // force GotoPage to actually move under the new layout
    GotoPage(targetPage);
    renderPages();
}

void
DocumentView::setModified(bool modified) noexcept
{
    if (!m_model->supports_save())
        return;

    if (m_is_modified == modified)
        return;

    m_is_modified = modified;
    QString title = m_global.window.title_format;
    QString fileName;
    if (m_global.statusbar.component.filename.full_path)
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

    emit statusbarNameChanged(fileName);
    emit modifiedChanged(modified);
    this->setWindowTitle(title);

#ifndef NDEBUG
    qDebug() << "DocumentView::setModified(): Setting modified state to"
             << modified;
#endif
}

bool
DocumentView::EncryptDocument() noexcept
{
    if (!m_model->supports_encryption())
        return false;

    Model::EncryptInfo encryptInfo;
    bool ok;
    QString password = QInputDialog::getText(
        this, tr("Encrypt Document"), tr("Enter password:"),
        QLineEdit::Password, QString(), &ok);
    if (!ok || password.isEmpty())
        return false;
    encryptInfo.user_password = password;
    return m_model->encrypt(encryptInfo);
}

bool
DocumentView::DecryptDocument() noexcept
{
    if (!m_model->supports_decryption())
        return false;

    if (fz_needs_password(m_model->m_ctx, m_model->m_doc))
    {
        bool ok;
        QString password;

        while (true)
        {
            password = QInputDialog::getText(
                this, tr("Decrypt Document"), tr("Enter password:"),
                QLineEdit::Password, QString(), &ok);
            if (!ok)
                return false;

            if (fz_authenticate_password(m_model->m_ctx, m_model->m_doc,
                                         password.toLatin1().constData()))
                return m_model->decrypt();
        }
    }
    return true;
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

// Slot to handle file change notifications for auto-reload
void
DocumentView::onFileReloadRequested(const QString &path) noexcept
{
    if (path != m_model->filePath())
        return;

    if (m_reload_pending)
        return;

    m_reload_pending            = true;
    m_last_reload_observed_size = -1;
    tryReloadLater(0);
}

// Try to reload the document, if the file is not yet readable, wait and try
// again a few times before giving up.
// We compare the file size across two consecutive attempts to detect when the
// write has stabilised — a single non-zero size reading is not enough because
// tools like latexmk first truncate the file to 0 bytes and then write to it.
void
DocumentView::tryReloadLater(int attempt) noexcept
{
    if (attempt > 15) // ~15 * 100ms = 1.5s
    {
        m_reload_pending = false;
        QMessageBox::warning(this, tr("Auto-reload failed"),
                             tr("Could not reload the document."));
        return;
    }

    const QString filepath = m_model->filePath();
    QFileInfo fi(filepath);
    const qint64 currentSize = fi.exists() ? fi.size() : 0;

    // File must be non-empty and its size must be stable across two ticks
    const bool stable
        = currentSize > 0 && currentSize == m_last_reload_observed_size;
    m_last_reload_observed_size = currentSize;

    if (!stable)
    {
        QTimer::singleShot(100, this,
                           [this, attempt]() { tryReloadLater(attempt + 1); });
        return;
    }

    // IMPORTANT: file may have been removed and replaced → watcher loses it.
    // Re-add unconditionally here so future saves are caught even if the
    // reload below fails.
    if (m_file_watcher && !m_file_watcher->files().contains(filepath))
        m_file_watcher->addPath(filepath);

    m_reload_pending = false;

    if (!m_model->reloadDocument())
        return;

    if (m_model->isImage())
    {
        renderImage();
    }
    else
    {
#ifdef WITH_SYNCTEX
        initSynctex();
#endif
        clearDocumentItems();
        cachePageStride();
        updateSceneRect();
        invalidateVisiblePagesCache();
        renderPages();
        emit totalPageCountChanged(m_model->m_page_count);
    }
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

void
DocumentView::localConfigChanged(const QString &section) noexcept
{
    const bool first = m_pending_config_sections.isEmpty();
    m_pending_config_sections.insert(section);
    // Batch: several option writes in one Lua chunk or one event-loop tick
    // cost a single apply (and at most one re-render).
    if (first)
        QTimer::singleShot(0, this, &DocumentView::applyLocalConfigChanges);
}

void
DocumentView::applyFiletypeOverrides(const QString &filePath) noexcept
{
    if (m_thumbnail_mode || !m_global.filetype_overrides)
        return;

    const std::string type
        = Model::fileTypeName(Model::getFileTypeForPath(filePath), filePath)
              .toLower()
              .toStdString();
    if (type == m_override_type)
        return;

    // Switching file type in a view that already took another type's
    // overrides: start again from the defaults.
    if (!m_override_type.empty())
        *m_local_config = m_global;
    m_override_type = type;

    const auto it = m_global.filetype_overrides->find(type);
    if (it == m_global.filetype_overrides->end())
        return;

    it->second(*m_local_config);
    for (const char *section :
         {"page", "behavior", "rendering", "scrollbars", "selection", "search",
          "jump_marker", "annotations", "links", "layout"})
        localConfigChanged(section);
}

// Pushes changed options into the objects that copied them at construction.
// Options that are read on use (zoom step, link hint keys, ...) need nothing.
void
DocumentView::applyLocalConfigChanges() noexcept
{
    const QSet<QString> sections = std::exchange(m_pending_config_sections, {});
    if (sections.isEmpty())
        return;

    bool rerender = false;
    bool relayout = false;

    if (sections.contains("page"))
    {
        m_model->setBackgroundColor(m_config.page.bg);
        m_model->setForegroundColor(m_config.page.fg);
        rerender = true;
    }

    if (sections.contains("behavior"))
    {
        m_model->setInvertColor(m_config.behavior.invert_mode);
        m_model->setCacheCapacity(m_config.behavior.cache_pages);
        m_model->undoStack()->setUndoLimit(m_config.behavior.undo_limit);
        rerender = true;
    }

    if (sections.contains("reflow"))
        applyReflowStyle(false);

    if (sections.contains("rendering"))
    {
        m_gview->setRenderHint(QPainter::Antialiasing,
                               m_config.rendering.antialiasing);
        m_gview->setRenderHint(QPainter::SmoothPixmapTransform,
                               m_config.rendering.smooth_pixmap_transform);
        m_gview->setRenderHint(QPainter::TextAntialiasing,
                               m_config.rendering.text_antialiasing);
        rerender = true;
    }

    if (sections.contains("scrollbars"))
    {
        m_vscroll->setSize(m_config.scrollbars.size);
        m_hscroll->setSize(m_config.scrollbars.size);
        m_gview->setScrollbarSize(m_config.scrollbars.size);
        m_gview->setScrollbarIdleTimeout(m_config.scrollbars.hide_timeout
                                         * 1000);
        m_gview->setVerticalScrollbarEnabled(m_config.scrollbars.vertical);
        m_gview->setHorizontalScrollbarEnabled(m_config.scrollbars.horizontal);
        m_gview->setAutoHideScrollbars(m_config.scrollbars.auto_hide);
    }

    if (sections.contains("selection"))
    {
        m_model->setSelectionColor(rgbaToQColor(m_config.selection.color));
        if (m_selection_path_item)
            m_selection_path_item->setBrush(
                QBrush(rgbaToQColor(m_config.selection.color)));
    }

    if (sections.contains("search") && m_current_search_hit_item)
        m_current_search_hit_item->setBrush(
            rgbaToQColor(m_config.search.index_color));

    if (sections.contains("jump_marker") && m_jump_marker)
    {
        m_jump_marker->setColor(rgbaToQColor(m_config.jump_marker.color));
        m_jump_marker->setFadeDuration(m_config.jump_marker.fade_duration);
    }

    if (sections.contains("annotations"))
    {
        m_model->setAnnotRectColor(
            rgbaToQColor(m_config.annotations.rect.color).toRgb());
        m_model->setHighlightColor(
            rgbaToQColor(m_config.annotations.highlight.color));
    }

    if (sections.contains("links"))
    {
        m_model->setDetectUrlLinks(m_config.links.detect_urls);
        m_model->setUrlLinkRegex(m_config.links.url_regex);
        rerender = true;
    }

    if (sections.contains("layout"))
    {
        m_auto_resize = m_config.layout.auto_resize;
        if (m_spacing != m_config.layout.spacing)
        {
            m_spacing = m_config.layout.spacing;
            relayout  = true;
        }
    }

    if (relayout && !m_model->isImage())
    {
        cachePageStride();
        updateSceneRect();
        repositionPages();
        rerender = true;
    }

    if (rerender)
    {
        m_model->invalidatePageCaches();
        m_page_render_keys.clear();
        if (m_model->isImage())
            renderImage();
        else
            renderPages();
    }

    m_gview->viewport()->update();
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
    requestPageRender(pageno, true);
}

void
DocumentView::handleReloadPasswordRequired() noexcept
{
    const QString msg
        = m_config.behavior.cache_password
              ? tr("Auto-reload failed: the document is password-protected "
                   "and the cached password no longer works.")
              : tr("Auto-reload failed: the document is password-protected. "
                   "Enable \"cache_password\" in the config to allow "
                   "automatic re-authentication on reload.");
    QMessageBox::warning(this, tr("Auto-reload failed"), msg);
}

void
DocumentView::setPortal(DocumentView *portal) noexcept
{
    m_portal_view = portal;
    portal->set_source(this);
    portal->m_gview->setPortal(true);
}

void
DocumentView::clearPortal() noexcept
{
    if (m_portal_view)
    {
        m_portal_view->clear_source();
        m_portal_view->m_gview->setPortal(false);
        m_portal_view = nullptr;
    }

    // TODO: Maybe notify views that the portal was cleared
}

void
DocumentView::setDPR(float dpr) noexcept
{
    m_model->setDPR(dpr);
    renderPages();
}
#ifdef WITH_LUA

void
DocumentView::dispatchLuaEvent(DispatchType type) noexcept
{
    for (const auto &callback : m_lua_event_dispatcher[type])
        callback.invoker(this);

    // Also forward to the global lektra.event.register/once(...) listener
    // list, not just this view's own view:register(...) listeners. Every
    // event routed through here previously only ever reached the latter —
    // a script with no view handle yet (e.g. init.lua, which runs before
    // any document is open) had no way to observe any of these events.
    // Centralised here rather than at each of the ~12 call sites so a
    // future new event routed through dispatchLuaEvent can't silently
    // reintroduce the same gap.
    if (auto *lektra = qobject_cast<Lektra *>(window()))
        lektra->dispatchLuaEvent(type, this);
}
#endif
#ifdef WITH_LUA

void
DocumentView::removeEventListener(DispatchType type, int handle) noexcept
{
    auto &listeners = m_lua_event_dispatcher[type];
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                   [handle](const LuaCallback<DocumentView> &cb)
    { return cb.ref == handle; }),
                    listeners.end());
}
#endif
#ifdef WITH_LUA

void
DocumentView::removeContextMenuListener(ContextMenuType type,
                                        int handle) noexcept
{
    auto &listeners = m_lua_context_menu_dispatcher[type];
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                   [handle](const MenuCallback &cb)
    { return cb.ref == handle; }),
                    listeners.end());
}
#endif
#ifdef WITH_LUA

void
DocumentView::applyLuaContextMenu(ContextMenuType type, QMenu *menu) noexcept
{
    if (!menu)
        return;

    auto &listeners = m_lua_context_menu_dispatcher[type];
    if (listeners.empty())
        return;

    QPointer<QMenu> guardedMenu(menu);
    std::vector<int> handlesToRemove;

    for (const auto &cb : listeners)
    {
        if (!guardedMenu)
            break;
        cb.invoker(this, guardedMenu.data());
        if (cb.is_once)
            handlesToRemove.push_back(cb.ref);
    }

    if (!handlesToRemove.empty())
    {
        for (const int handle : handlesToRemove)
            removeContextMenuListener(type, handle);
    }
}
#endif
