#include "DocumentView.hpp"

#include <QMovie>

// Annotations
#include "Annotations/HighlightAnnotation.hpp"
#include "Annotations/PopupAnnotation.hpp"
#include "Annotations/RectAnnotation.hpp"
#include "Annotations/ShapeAnnotation.hpp"
#include "BrowseLinkItem.hpp"

// Commands
#include "Commands/AnnotColorCommand.hpp"
#include "Commands/AnnotGeometryCommand.hpp"
#include "Commands/AnnotCommentCommand.hpp"
#include "Commands/DeleteAnnotationsCommand.hpp"
#include "Commands/RectAnnotationCommand.hpp"
#include "Commands/ShapeAnnotationCommand.hpp"
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

void
DocumentView::handleLinkPreviewRequested(QPointF scenePos) noexcept
{
    if (!m_model->supports_links())
        return;

    int pageIndex               = -1;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageIndex, pageItem))
        return;

    const std::vector<BrowseLinkItem *> links_in_page
        = m_page_links_hash[pageIndex];
    if (links_in_page.empty())
        return;

    // Get the link item at the clicked position, if any
    BrowseLinkItem *clicked_link = nullptr;
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
    emit linkPreviewRequested(this, clicked_link);
}

void
DocumentView::handleLinkCtrlClickRequested(QPointF scenePos) noexcept
{
    if (!m_model->supports_links())
        return;

    int pageIndex               = -1;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageIndex, pageItem))
        return;

    const std::vector<BrowseLinkItem *> links_in_page
        = m_page_links_hash[pageIndex];
    if (links_in_page.empty())
        return;

    // Get the link item at the clicked position, if any
    BrowseLinkItem *clicked_link = nullptr;
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
DocumentView::handleLinkMiddleClickRequested(QPointF scenePos) noexcept
{
    if (!m_model->supports_links())
        return;

    int pageIndex               = -1;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageIndex, pageItem))
        return;

    const std::vector<BrowseLinkItem *> links_in_page
        = m_page_links_hash[pageIndex];
    if (links_in_page.empty())
        return;

    BrowseLinkItem *clicked_link = nullptr;
    for (BrowseLinkItem *link : links_in_page)
    {
        if (link->contains(scenePos))
        {
            clicked_link = link;
            break;
        }
    }

    if (!clicked_link || !clicked_link->isInternal())
        return;
    emit linkOpenInNewTabRequested(this, clicked_link);
}

// Get the link KB for the current document
QMap<int, Model::LinkInfo>
DocumentView::LinkKB() noexcept
{
    QMap<int, Model::LinkInfo> hintMap;

    if (!m_gscene)
        return hintMap;

    if (!m_model->supports_links())
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

    const QColor bg = rgbaToQColor(m_config.link_hints.bg);
    const QColor fg = rgbaToQColor(m_config.link_hints.fg);

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
                emit externalLinkRequested(info.uri);
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
                PageLocation target = {info.target_page, 0, 0};
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

#ifdef WITH_LUA
    dispatchLuaEvent(DispatchType::OnLinkClicked);
#endif
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

void
DocumentView::ensureHoverSetup() noexcept
{
    if (!m_hover_timer)
    {
        m_hover_timer = new QTimer(this);
        m_hover_timer->setSingleShot(true);
        connect(m_hover_timer, &QTimer::timeout, this,
                &DocumentView::startHoverRender);
        connect(&m_hover_watcher, &QFutureWatcher<QImage>::finished, this,
                [this]
        {
            QImage image            = m_hover_watcher.result();
            const HoverRequest done = m_hover_inflight;
            if (done.generation == m_hover_generation && !image.isNull())
            {
                if (!m_hover_preview)
                    m_hover_preview = new LinkHoverPreview(this);
                m_hover_preview->applyStyle(m_global.preview.border_radius,
                                            m_global.preview.opacity);
                m_hover_preview->showPreview(QPixmap::fromImage(image),
                                             tr("Page %1").arg(done.page + 1),
                                             done.globalPos);
            }
            // A newer hover may have been waiting for the worker.
            if (m_has_hover_pending && !m_hover_timer->isActive())
                startHoverRender();
        });
    }
}

void
DocumentView::showLinkHoverPreview(const BrowseLinkItem *link,
                                   const QPoint &globalPos) noexcept
{
    hideLinkHoverPreview();

    if (!m_config.links.hover_preview || !link || !link->isInternal()
        || m_thumbnail_mode)
        return;

    const int page = link->gotoPageNo();
    if (page < 0 || page >= m_model->numPages())
        return;

    // The link item may be deleted before the delay is over (page
    // re-rendered): keep a copy of what is needed.
    m_hover_pending.page       = page;
    m_hover_pending.x          = link->location().x;
    m_hover_pending.y          = link->location().y;
    m_hover_pending.globalPos  = globalPos;
    m_hover_pending.generation = m_hover_generation;
    m_has_hover_pending        = true;

    ensureHoverSetup();
    m_hover_timer->start(std::max(0, m_config.links.hover_preview_delay));
}

void
DocumentView::hideLinkHoverPreview() noexcept
{
    ++m_hover_generation; // a render still running is now stale
    m_has_hover_pending = false;
    if (m_hover_timer)
        m_hover_timer->stop();
    if (m_hover_preview)
        m_hover_preview->hide();
}

void
DocumentView::startHoverRender() noexcept
{
    if (!m_has_hover_pending || m_hover_watcher.isRunning())
        return;

    m_hover_inflight    = m_hover_pending;
    m_has_hover_pending = false;

    const HoverRequest req = m_hover_inflight;
    const QSize size(std::clamp(m_config.links.hover_preview_width, 120, 1600),
                     std::clamp(m_config.links.hover_preview_height, 60, 1200));
    const qreal dpr = devicePixelRatioF();
    Model *model    = m_model;

    m_hover_watcher.setFuture(QtConcurrent::run([=]() -> QImage
    {
        // The part of the target page to show: as wide as is readable, with
        // the preview's proportions, starting just above the destination.
        const QSizeF page = model->pageSizePts(req.page, true);
        if (page.isEmpty())
            return {};
        const double regionW = std::min<double>(page.width(), 420.0);
        const double regionH = std::min<double>(
            page.height(), regionW * size.height() / size.width());
        const double x0 = std::isnan(req.x)
                              ? 0.0
                              : std::clamp<double>(req.x - 12.0, 0.0,
                                                   page.width() - regionW);
        const double y0 = std::isnan(req.y)
                              ? 0.0
                              : std::clamp<double>(req.y - 24.0, 0.0,
                                                   page.height() - regionH);

        // Pixels needed: the region's width at the preview's device size.
        const float dpi
            = static_cast<float>(size.width() * dpr / regionW * 72.0);
        QImage image = model->renderPtsRegion(
            req.page, QRectF(x0, y0, regionW, regionH), dpi);
        if (!image.isNull())
            image.setDevicePixelRatio(dpr);
        return image;
    }));
}

void
DocumentView::renderLinks(int pageno,
                          const std::vector<Model::RenderLink> &links,
                          bool append) noexcept
{
    if (!m_model->supports_links())
        return;

    if (!append && m_page_links_hash.contains(pageno))
        return;

    GraphicsImageItem *pageItem = m_page_items_hash.value(pageno, nullptr);
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

        // Internal jump links show a raw internal destination string (e.g.
        // "#page=5") in link.uri, which isn't meaningful to users — show the
        // target page number instead. External links keep showing the real
        // URL, same as a browser.
        switch (link.type)
        {
            case BrowseLinkItem::LinkType::Page:
            case BrowseLinkItem::LinkType::Section:
            case BrowseLinkItem::LinkType::Location:
            case BrowseLinkItem::LinkType::FitV:
            case BrowseLinkItem::LinkType::FitH:
                if (m_config.links.hover_preview)
                    // The preview replaces the tooltip.
                    item->setToolTip(QString());
                else if (link.target_page >= 0)
                    item->setToolTip(
                        tr("Go to page %1").arg(link.target_page + 1));
                break;
            case BrowseLinkItem::LinkType::External:
                break;
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
                    PageLocation target = {pageno, loc.x, loc.y};
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
                    PageLocation target = {pageno, loc.x, loc.y};
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
                    const PageLocation targetLocation = {targetPageno, 0, 0};
                    const PageLocation sourceLocation = {
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
                    const PageLocation targetLocation{targetPageno,
                                                      targetLocationOfLink.x,
                                                      targetLocationOfLink.y};

                    const PageLocation sourceLocation{
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

        if (m_config.links.hover_preview && item->isInternal())
        {
            connect(item, &BrowseLinkItem::hoverPreviewRequested, this,
                    [this](const BrowseLinkItem *l, const QPoint &pos)
            { showLinkHoverPreview(l, pos); });
            connect(item, &BrowseLinkItem::hoverPreviewCancelled, this,
                    &DocumentView::hideLinkHoverPreview);
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

        connect(item, &BrowseLinkItem::linkOpenInNewTabRequested, this,
                [this](const BrowseLinkItem *link)
        { emit linkOpenInNewTabRequested(this, link); });

        connect(item, &BrowseLinkItem::linkOpenPortalRequested, this,
                [this](const BrowseLinkItem *link)
        { emit ctrlLinkClickRequested(this, link); });

        connect(item, &BrowseLinkItem::linkOpenPreviewRequested, this,
                [this](const BrowseLinkItem *link)
        { emit linkPreviewRequested(this, link); });

        connect(item, &BrowseLinkItem::linkOpenVSplitRequested, this,
                [this](const BrowseLinkItem *link)
        { emit linkOpenVSplitRequested(this, link); });

        connect(item, &BrowseLinkItem::linkOpenHSplitRequested, this,
                [this](const BrowseLinkItem *link)
        { emit linkOpenHSplitRequested(this, link); });

        connect(item, &BrowseLinkItem::externalLinkRequested, this,
                &DocumentView::externalLinkRequested);

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
    if (!m_model->supports_annotations())
        return;

    clearAnnotationsForPage(pageno);
    // if (m_page_annotations_hash.contains(pageno))
    //     return;

    const GraphicsImageItem *pageItem
        = m_page_items_hash.value(pageno, nullptr);
    if (!pageItem)
        return;

    for (const auto &annot : annotations)
    {
        Annotation *annot_item = nullptr;
        switch (annot.type)
        {
            case PDF_ANNOT_HIGHLIGHT:
            {
                annot_item = new HighlightAnnotation(
                    m_config.annotations.highlight, annot.rect, annot.index,
                    annot.text, annot.rects);
            }
            break;

            case PDF_ANNOT_UNDERLINE:
            {
                // The underline itself is in the page's appearance; this item
                // only gives it hover, comment and selection, as a highlight
                // has.
                Config::Annotations::Highlight options;
                static_cast<Config::Annotations::Base &>(options)
                    = m_config.annotations.underline;
                options.comment_marker
                    = m_config.annotations.underline.comment_marker;
                annot_item
                    = new HighlightAnnotation(options, annot.rect, annot.index,
                                              annot.text, annot.rects);
            }
            break;

            case PDF_ANNOT_SQUARE:
            {
                annot_item
                    = new RectAnnotation(m_config.annotations.rect, annot.rect,
                                         annot.index, annot.text, annot.color);
            }
            break;

            case PDF_ANNOT_CIRCLE:
            case PDF_ANNOT_POLYGON:
            case PDF_ANNOT_FREE_TEXT:
            {
                const auto type
                    = annot.type == PDF_ANNOT_CIRCLE ? Annotation::Type::Ellipse
                      : annot.type == PDF_ANNOT_POLYGON
                          ? Annotation::Type::Polygon
                          : Annotation::Type::Note;
                annot_item = new ShapeAnnotation(
                    m_config.annotations.rect, type, annot.rect, annot.index,
                    annot.text, annot.color);
            }
            break;

            case PDF_ANNOT_TEXT:
            {
                annot_item = new PopupAnnotation(m_config.annotations.popup,
                                                 annot.rect, annot.index,
                                                 annot.color, annot.text);
            }
            break;

            default:
                break;
        }

        if (!annot_item)
            continue;

        annot_item->setZValue(ZVALUE_ANNOTATION);
        annot_item->setPos(pageItem->pos());

        connect(annot_item, &Annotation::annotCommentRequested, this,
                [this, annot_item, pageno]()
        {
            const QString oldComment = annot_item->comment();
            bool ok;
            const QString newComment = InputDialog::getText(
                tr("Add Comment"), tr("Enter annotation comment:"), "",
                oldComment, ok, this);

            if (!ok)
                return;

            m_model->undoStack()->push(new AnnotCommentCommand(
                m_model, pageno, annot_item->index(), oldComment, newComment));
        });

        connect(annot_item, &Annotation::annotCopyTextRequested, this,
                [this, annot_item, pageno]()
        {
            const QString text
                = m_model->getHighlightText(pageno, annot_item->index());
            if (!text.isEmpty())
                QGuiApplication::clipboard()->setText(text);
        });

        connect(annot_item, &Annotation::annotDeleteRequested, this,
                [this, annot_item, pageno]()
        {
            m_model->undoStack()->push(new DeleteAnnotationsCommand(
                m_model, pageno, {annot_item->index()}));
        });

        connect(annot_item, &Annotation::annotColorChangeRequested, this,
                [this, annot_item, pageno]()
        {
            QColor oldColor
                = m_model->getAnnotColor(pageno, annot_item->index());
            ColorDialog colorDialog(m_global.misc.color_dialog_colors,
                                    QColor::fromRgba(oldColor.rgba()), this);
            colorDialog.setWindowTitle(tr("Select Annotation Color"));

            if (colorDialog.exec() == QDialog::Accepted)
            {
                QColor newColor = colorDialog.selectedColor();
                if (newColor.isValid())
                {
                    m_model->undoStack()->push(new AnnotColorCommand(
                        m_model, pageno, annot_item->index(), oldColor,
                        newColor));
                }
            }
        });

        m_gscene->addItem(annot_item);
        m_page_annotations_hash[pageno].push_back(annot_item);

        if (pageno == m_reselect_pageno
            && annot_item->index() == m_reselect_objnum)
        {
            annot_item->setSelected(true);
            m_reselect_pageno = m_reselect_objnum = -1;
        }
    }
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

// The selected annotation under the mouse, and what of it: a handle or its body.
bool
DocumentView::grabAnnotation(QPointF scenePos) noexcept
{
    const qreal scale = m_gview->transform().m11();

    for (auto it = m_page_annotations_hash.begin();
         it != m_page_annotations_hash.end(); ++it)
    {
        for (Annotation *annot : it.value())
        {
            if (!annot || !annot->isSelected() || !annot->canTransform())
                continue;

            const auto handle
                = annot->handleAt(annot->mapFromScene(scenePos), scale);
            if (handle == Annotation::Handle::None)
                continue;

            m_annot_drag.annot     = annot;
            m_annot_drag.pageno    = it.key();
            m_annot_drag.handle    = handle;
            m_annot_drag.startPos  = annot->mapFromScene(scenePos);
            m_annot_drag.startRect = annot->geometryRect();
            m_annot_drag.rect      = m_annot_drag.startRect;
            return true;
        }
    }
    return false;
}

Qt::CursorShape
DocumentView::annotationCursor(QPointF scenePos) noexcept
{
    const qreal scale = m_gview->transform().m11();

    for (auto it = m_page_annotations_hash.begin();
         it != m_page_annotations_hash.end(); ++it)
    {
        for (Annotation *annot : it.value())
        {
            if (!annot || !annot->isSelected() || !annot->canTransform())
                continue;

            switch (annot->handleAt(annot->mapFromScene(scenePos), scale))
            {
                case Annotation::Handle::TopLeft:
                case Annotation::Handle::BottomRight:
                    return Qt::SizeFDiagCursor;
                case Annotation::Handle::TopRight:
                case Annotation::Handle::BottomLeft:
                    return Qt::SizeBDiagCursor;
                case Annotation::Handle::Top:
                case Annotation::Handle::Bottom:
                    return Qt::SizeVerCursor;
                case Annotation::Handle::Left:
                case Annotation::Handle::Right:
                    return Qt::SizeHorCursor;
                case Annotation::Handle::Body:
                    return Qt::SizeAllCursor;
                case Annotation::Handle::None:
                    break;
            }
        }
    }
    return Qt::ArrowCursor;
}

void
DocumentView::dragAnnotation(QPointF scenePos, bool keepAspect) noexcept
{
    Annotation *annot = m_annot_drag.annot;
    if (!annot)
        return;

    const GraphicsImageItem *pageItem
        = m_page_items_hash.value(m_annot_drag.pageno, nullptr);
    const QRectF bounds = pageItem ? pageItem->boundingRect() : QRectF();

    using H = Annotation::Handle;
    constexpr qreal minSize = 8.0;

    const QPointF delta = annot->mapFromScene(scenePos) - m_annot_drag.startPos;
    const QRectF &start = m_annot_drag.startRect;
    QRectF r            = start;

    if (m_annot_drag.handle == H::Body)
    {
        r.translate(delta);
        if (bounds.isValid())
        {
            // keep it on the page
            r.translate(std::max(bounds.left() - r.left(), 0.0), 0);
            r.translate(std::min(bounds.right() - r.right(), 0.0), 0);
            r.translate(0, std::max(bounds.top() - r.top(), 0.0));
            r.translate(0, std::min(bounds.bottom() - r.bottom(), 0.0));
        }
    }
    else
    {
        const H h = m_annot_drag.handle;
        const bool left
            = h == H::Left || h == H::TopLeft || h == H::BottomLeft;
        const bool right
            = h == H::Right || h == H::TopRight || h == H::BottomRight;
        const bool top = h == H::Top || h == H::TopLeft || h == H::TopRight;
        const bool bottom
            = h == H::Bottom || h == H::BottomLeft || h == H::BottomRight;

        qreal l = start.left(), t = start.top(), rr = start.right(),
              b = start.bottom();
        if (left)
            l = std::min(start.left() + delta.x(), rr - minSize);
        if (right)
            rr = std::max(start.right() + delta.x(), l + minSize);
        if (top)
            t = std::min(start.top() + delta.y(), b - minSize);
        if (bottom)
            b = std::max(start.bottom() + delta.y(), t + minSize);

        if (bounds.isValid())
        {
            l  = std::max(l, bounds.left());
            t  = std::max(t, bounds.top());
            rr = std::min(rr, bounds.right());
            b  = std::min(b, bounds.bottom());
        }

        // Shift on a corner: keep the proportions, the opposite corner stays.
        if (keepAspect && (left || right) && (top || bottom)
            && start.height() > 0)
        {
            const qreal ratio = start.width() / start.height();
            qreal w           = rr - l;
            qreal hgt         = b - t;
            if (w / std::max(hgt, 1.0) > ratio)
                w = hgt * ratio;
            else
                hgt = w / ratio;
            if (left)
                l = rr - w;
            else
                rr = l + w;
            if (top)
                t = b - hgt;
            else
                b = t + hgt;
        }

        r = QRectF(QPointF(l, t), QPointF(rr, b));
    }

    m_annot_drag.rect = r;
    annot->setPreviewRect(r);
}

void
DocumentView::dropAnnotation(QPointF) noexcept
{
    Annotation *annot = m_annot_drag.annot;
    const AnnotDrag drag = m_annot_drag;
    m_annot_drag         = {};

    if (!annot)
        return;

    annot->clearPreviewRect();

    if (drag.rect == drag.startRect || !m_model)
        return;

    const int pageno = drag.pageno;
    const int objNum = annot->index();

    auto toPdf = [&](const QRectF &r)
    {
        const fz_point a = m_model->toPDFSpace(pageno, r.topLeft());
        const fz_point b = m_model->toPDFSpace(pageno, r.bottomRight());
        return fz_rect{std::min(a.x, b.x), std::min(a.y, b.y),
                       std::max(a.x, b.x), std::max(a.y, b.y)};
    };

    Model::AnnotGeometry before, after;
    if (!m_model->annotGeometry(pageno, objNum, before))
        return;

    const fz_rect from = toPdf(drag.startRect);
    const fz_rect to   = toPdf(drag.rect);

    // What is on the page differs a little from what is shown (the border), so
    // the old geometry is carried over to the new one instead of replaced.
    const float sx = (from.x1 - from.x0) > 0
                         ? (to.x1 - to.x0) / (from.x1 - from.x0)
                         : 1.0f;
    const float sy = (from.y1 - from.y0) > 0
                         ? (to.y1 - to.y0) / (from.y1 - from.y0)
                         : 1.0f;
    auto carry = [&](fz_point p)
    {
        return fz_point{to.x0 + (p.x - from.x0) * sx,
                        to.y0 + (p.y - from.y0) * sy};
    };

    if (annot->atype() == Annotation::Type::Polygon)
    {
        for (const fz_point &p : before.vertices)
            after.vertices.push_back(carry(p));
    }
    else
    {
        const fz_point a = carry({before.rect.x0, before.rect.y0});
        const fz_point b = carry({before.rect.x1, before.rect.y1});
        after.rect       = fz_rect{a.x, a.y, b.x, b.y};
    }

    m_reselect_pageno = pageno;
    m_reselect_objnum = objNum;
    m_model->undoStack()->push(new AnnotGeometryCommand(
        m_model, pageno, objNum, std::move(before), std::move(after)));
}

void
DocumentView::cancelAnnotationDrag() noexcept
{
    if (m_annot_drag.annot)
        m_annot_drag.annot->clearPreviewRect();
    m_annot_drag = {};
}

void
DocumentView::handleAnnotSelectRequested(QRectF sceneRect) noexcept
{
    if (!m_model || !m_model->supports_annotations())
        return;

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
        annot->setSelected(true);
}

void
DocumentView::handleAnnotSelectRequested(QPointF scenePos) noexcept
{
    if (!m_model || !m_model->supports_annotations())
        return;

    int pageno;
    GraphicsImageItem *pageItem;
    if (!pageAtScenePos(scenePos, pageno, pageItem))
        return;

    const QPointF searchPos = pageItem->mapFromScene(scenePos);
    const auto annotAtPoint = annotationAtPoint(pageno, searchPos);

    if (!annotAtPoint)
        return;

    annotAtPoint->setSelected(true);
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
    Annotation *foundAnnot = nullptr;
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

// Handle annotation rectangle requested
void
DocumentView::handleAnnotRectRequested(QRectF area) noexcept
{
    if (!m_model || !m_model->supports_annotations())
        return;

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
    // setModified(true);
}

// Handle annotation ellipse requested
void
DocumentView::handleAnnotEllipseRequested(QRectF area) noexcept
{
    if (!m_model || !m_model->supports_annotations())
        return;

    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    const QRectF pageLocalRect = pageItem->mapFromScene(area).boundingRect();
    const fz_point topLeft
        = m_model->toPDFSpace(pageno, pageLocalRect.topLeft());
    const fz_point bottomRight
        = m_model->toPDFSpace(pageno, pageLocalRect.bottomRight());

    const fz_rect rect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};

    m_model->undoStack()->push(new ShapeAnnotationCommand(
        m_model, pageno, PDF_ANNOT_CIRCLE, rect));
}

// Handle annotation polygon requested (the corners, in scene space)
void
DocumentView::handleAnnotPolygonRequested(QVector<QPointF> points) noexcept
{
    if (!m_model || !m_model->supports_annotations() || points.size() < 3)
        return;

    QPointF sum;
    for (const QPointF &p : points)
        sum += p;

    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(sum / points.size(), pageno, pageItem))
        return;

    std::vector<fz_point> vertices;
    vertices.reserve(points.size());
    for (const QPointF &p : points)
        vertices.push_back(
            m_model->toPDFSpace(pageno, pageItem->mapFromScene(p)));

    m_model->undoStack()->push(new ShapeAnnotationCommand(
        m_model, pageno, PDF_ANNOT_POLYGON, fz_empty_rect,
        std::move(vertices)));
}

// Handle inline note requested: text written on the page, in `area`
void
DocumentView::handleAnnotNoteRequested(QRectF area) noexcept
{
    if (!m_model || !m_model->supports_annotations())
        return;

    int pageno;
    GraphicsImageItem *pageItem;

    if (!pageAtScenePos(area.center(), pageno, pageItem))
        return;

    bool ok;
    const QString text
        = InputDialog::getText(tr("Add Inline Note"), tr("Enter note text:"),
                               tr("Enter text here"), "", ok, this);

    if (!ok || text.isEmpty())
        return;

    const QRectF pageLocalRect = pageItem->mapFromScene(area).boundingRect();
    const fz_point topLeft
        = m_model->toPDFSpace(pageno, pageLocalRect.topLeft());
    const fz_point bottomRight
        = m_model->toPDFSpace(pageno, pageLocalRect.bottomRight());

    const fz_rect rect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};

    m_model->undoStack()->push(new ShapeAnnotationCommand(
        m_model, pageno, PDF_ANNOT_FREE_TEXT, rect, {}, text));
}

// Handle annotation popup (text/sticky note) requested
void
DocumentView::handleAnnotPopupRequested(QPointF scenePos) noexcept
{
    if (!m_model || !m_model->supports_annotations())
        return;

    int pageno;
    GraphicsImageItem *pageItem = nullptr;

    if (!pageAtScenePos(scenePos, pageno, pageItem))
        return;

    // Show input dialog for annotation text
    bool ok;
    QString text
        = InputDialog::getText(tr("Add Note"), tr("Enter annotation text:"),
                               tr("Enter text here"), "", ok, this);

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
    // setModified(true);
}

void
DocumentView::ToggleCommentMarkers() noexcept
{
    if (!m_model->supports_annotations())
        return;

    if (m_page_annotations_hash.isEmpty())
        return;

    for (auto it = m_page_annotations_hash.begin();
         it != m_page_annotations_hash.end(); ++it)
    {
        auto &annotations = it.value();
        for (Annotation *annot : annotations)
        {
            if (!annot)
                continue;

            switch (annot->atype())
            {
                case Annotation::Type::Highlight:
                {
                    annot->setCommentMarkerVisible(
                        m_config.annotations.highlight.comment_marker);
                }
                break;
                case Annotation::Type::Rect:
                case Annotation::Type::Ellipse:
                case Annotation::Type::Polygon:
                {
                    annot->setCommentMarkerVisible(
                        m_config.annotations.rect.comment_marker);
                }
                break;

                default:
                    break;
            }

            annot->updateCommentMarker();
        }
    }
}

struct DBG { QString s; QDebug d{&s}; ~DBG(){ d.nospace(); fprintf(stderr, "%s\n", qPrintable(s)); } template<class T> DBG &operator<<(const T &v){ d << v << ' '; return *this; } };
void
DocumentView::debugSelfTest() noexcept
{
    struct T { int obj; QString name; };
    auto *st = new QList<T>;
    auto add = [&](const QString &n, QUndoCommand *c, int obj) { m_model->undoStack()->push(c); st->push_back({obj, n}); };
    DBG() << "SELFTEST start";
    auto *c1 = new ShapeAnnotationCommand(m_model, 0, PDF_ANNOT_CIRCLE, fz_rect{100, 100, 200, 200});
    DBG() << "SELFTEST adding ellipse"; add("ellipse", c1, 0);
    st->back().obj = c1->objNum();
    auto *c2 = new ShapeAnnotationCommand(m_model, 0, PDF_ANNOT_POLYGON, fz_empty_rect, std::vector<fz_point>{{300, 100}, {400, 100}, {350, 200}});
    DBG() << "SELFTEST adding polygon"; add("polygon", c2, 0); st->back().obj = c2->objNum();
    auto *c3 = new ShapeAnnotationCommand(m_model, 0, PDF_ANNOT_FREE_TEXT, fz_rect{100, 300, 250, 350}, {}, "hello");
    DBG() << "SELFTEST adding note"; add("note", c3, 0); st->back().obj = c3->objNum();
    auto *c4 = new RectAnnotationCommand(m_model, 0, fz_rect{300, 300, 400, 400});
    DBG() << "SELFTEST adding rect"; add("rect", c4, 0); st->back().obj = c4->objNum();
    auto dump = [=](const char *tag, const T &t)
    {
        Model::AnnotGeometry g;
        DBG() << "SELFTEST dumping" << t.name << t.obj;
        const bool ok = m_model->annotGeometry(0, t.obj, g);
        DBG() << "SELFTEST" << tag << t.name << t.obj << ok << g.rect.x0 << g.rect.y0 << g.rect.x1 << g.rect.y1 << "verts" << (int)g.vertices.size() << (g.vertices.empty() ? 0.f : g.vertices[0].x) << (g.vertices.empty() ? 0.f : g.vertices[0].y);
    };
    for (auto &t : *st) dump("added", t);
    QTimer::singleShot(1500, this, [=]
    {
        m_gview->setMode(GraphicsView::Mode::AnnotSelect);
        for (auto &t : *st)
        {
            Annotation *a = nullptr;
            for (auto *x : m_page_annotations_hash.value(0)) if (x->index() == t.obj) a = x;
            if (!a) { DBG() << "SELFTEST no item" << t.name; continue; }
            a->setSelected(true);
            const QPointF c = a->mapToScene(a->geometryRect().center());
            const QPoint p0 = m_gview->mapFromScene(c);
            auto send = [&](QEvent::Type ty, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs)
            {
                QMouseEvent ev(ty, QPointF(pos), QPointF(m_gview->viewport()->mapToGlobal(pos)), b, bs, Qt::NoModifier);
                QApplication::sendEvent(m_gview->viewport(), &ev);
            };
            send(QEvent::MouseButtonPress, p0, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, p0 + QPoint(20, 10), Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseMove, p0 + QPoint(40, 30), Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, p0 + QPoint(40, 30), Qt::LeftButton, Qt::NoButton);
            a->setSelected(false);
            dump("moved", t);
        }
        DBG() << "SELFTEST stack index" << m_model->undoStack()->index() << m_model->undoStack()->count();
        QTimer::singleShot(1500, this, [=]
        {
            for (int k = 0; k < st->size(); ++k)
            {
                m_model->undoStack()->undo();
                for (auto &t : *st) dump("after-undo", t);
            }
        });
    });
}
