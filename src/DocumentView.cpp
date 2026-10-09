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
static DocumentView::Id nextId = 0;

static DocumentView::Id
g_newId() noexcept
{
    return nextId++;
}

DocumentView::DocumentView(const Config &config, float dpr, QWidget *parent,
                           bool thumbnailMode,
                           const Config *inheritFrom) noexcept
    : QWidget(parent), m_global(config),
      m_local_config(
          std::make_unique<Config>(inheritFrom ? *inheritFrom : config)),
      m_config(*m_local_config), m_id(g_newId()),
      m_thumbnail_mode(thumbnailMode)
{
#ifndef NDEBUG
    qDebug() << "DocumentView::DocumentView(): Initializing DocumentView";
#endif

    m_model = new Model(m_config, this);
    m_model->setDPR(dpr);

    connectModelFailureSignals();

    initGui();
#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnReady);
#endif
}

DocumentView::~DocumentView() noexcept
{
    stopGifPlayback();

    // The hover preview worker uses the model.
    ++m_hover_generation;
    m_hover_watcher.waitForFinished();

    // Stop and WAIT for all renders to finish before touching anything
    stopPendingRenders();
    resetConnections();

#ifdef WITH_SYNCTEX
    synctex_scanner_free(m_synctex_scanner);
#endif

    // m_gscene->removeItem(m_jump_marker);
    // m_gscene->removeItem(m_selection_path_item);
    // m_gscene->removeItem(m_current_search_hit_item);
    // m_gscene->removeItem(m_visual_line_item);

    clearDocumentItems();

    if (m_visual_line_item)
        delete m_visual_line_item;

    if (m_jump_marker)
        delete m_jump_marker;

    if (m_selection_path_item)
        delete m_selection_path_item;

    if (m_current_search_hit_item)
        delete m_current_search_hit_item;

    // Model and GraphicsView hold references to this view's local config,
    // which is a member and is destroyed before QWidget's destructor deletes
    // child objects. Delete them here, while the config is still alive.
    delete m_gview;
    m_gview = nullptr;
    delete m_model;
    m_model = nullptr;
}

void
DocumentView::initGui() noexcept
{
    m_gview  = new GraphicsView(m_config, this);
    m_gscene = new GraphicsScene(m_gview);
    m_gview->setScene(m_gscene);

    if (!m_thumbnail_mode)
    {
        m_selection_path_item = m_gscene->addPath(QPainterPath());
        m_selection_path_item->setBrush(
            QBrush(rgbaToQColor(m_config.selection.color)));
        m_selection_path_item->setPen(Qt::NoPen);
        m_selection_path_item->setZValue(ZVALUE_TEXT_SELECTION);

        m_current_search_hit_item = m_gscene->addPath(QPainterPath());
        m_current_search_hit_item->setBrush(
            rgbaToQColor(m_config.search.index_color));
        m_current_search_hit_item->setPen(Qt::NoPen);
        m_current_search_hit_item->setZValue(ZVALUE_SEARCH_HITS + 1);

        m_jump_marker
            = new JumpMarker(rgbaToQColor(m_config.jump_marker.color));
        m_jump_marker->setFadeDuration(m_config.jump_marker.fade_duration);
        m_jump_marker->setZValue(ZVALUE_JUMP_MARKER);
        m_gscene->addItem(m_jump_marker);
    }

    m_spinner = new WaitingSpinnerWidget(this);
    m_spinner->setInnerRadius(5.0);
    m_spinner->setColor(palette().color(QPalette::Text));

    m_spacing      = m_config.layout.spacing;
    m_grid_columns = std::clamp(m_config.layout.grid_columns, 1, 32);

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

    m_gview->setAlignment(Qt::AlignCenter);
    m_gview->setDefaultMode(m_config.behavior.initial_mode);
    m_gview->setMode(m_config.behavior.initial_mode);
    m_model->setAnnotRectColor(
        rgbaToQColor(m_config.annotations.rect.color).toRgb());
    m_model->setSelectionColor(rgbaToQColor(m_config.selection.color));
    m_model->setHighlightColor(
        rgbaToQColor(m_config.annotations.highlight.color));
    // m_model->setAntialiasingBits(m_config.rendering.antialiasing_bits);
    m_model->undoStack()->setUndoLimit(m_config.behavior.undo_limit);

    m_model->setInvertColor(m_config.behavior.invert_mode);
    m_model->setDetectUrlLinks(m_config.links.detect_urls);
    m_model->setUrlLinkRegex(m_config.links.url_regex);
    // if (m_config.rendering.icc_color_profile)
    //     m_model->enableICC();
    m_model->setCacheCapacity(m_config.behavior.cache_pages);
    m_model->setBackgroundColor(m_config.page.bg);
    m_model->setForegroundColor(m_config.page.fg);

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

    m_gview->setImageDragProvider([this](QPointF scenePos)
    { return imageAt(scenePos); });

    m_auto_resize       = m_config.layout.auto_resize;
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(0, 0, 0, 0);
    this->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_gview);
}
