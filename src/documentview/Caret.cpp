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
#include <limits>
#include <qdebug.h>
#include <qguiapplication.h>
#include <qicon.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qpolygon.h>
#include <qstyle.h>

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
            // TODO: Implement horizontal movement within the same line if the
            // model provides character-level info.
            break;

        case UP:
        {
            if (m_visual_line_index == 0)
            {
                GotoPrevPage();
                m_visual_line_index = m_visual_lines.size()
                                      - 1; // Move to last line of new page
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
                || m_visual_line_index
                       == static_cast<int>(m_visual_lines.size() - 1))
            {
                GotoNextPage();
            }
            else
            {
                m_visual_line_index++;
            }
        }
        break;
    }

    snapVisualLine();
}

void
DocumentView::snapVisualLine(bool centerView) noexcept
{
    // Ensure we have lines for the current page
    if (!m_visual_line_mode)
        return;

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
        && m_visual_line_index < static_cast<int>(m_visual_lines.size()))
    {
        const Model::VisualLineInfo &info
            = m_visual_lines.at(m_visual_line_index);
        GraphicsImageItem *pageItem
            = m_page_items_hash.value(info.pageno, nullptr);

        if (!pageItem)
            return;

        const float scale = m_model->logicalScale();

        // Map bbox in item-local pixel coords through the full item
        // transform. Do NOT pre-multiply by scale if the item itself
        // carries a scale() factor — mapRectToScene already accounts for
        // it. Instead, work in the item's own coordinate system (pixels at
        // render resolution) and let Qt composite the transform.
        QRectF itemBbox(info.bbox.x() * scale, info.bbox.y() * scale,
                        info.bbox.width() * scale, info.bbox.height() * scale);

        QRectF sceneBbox = pageItem->mapRectToScene(QRectF(
            itemBbox.x() / pageItem->scale(), itemBbox.y() / pageItem->scale(),
            itemBbox.width() / pageItem->scale(),
            itemBbox.height() / pageItem->scale()));

        // QRectF sceneBbox = pageItem->mapRectToScene(scaledBbox);

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
    if (!m_model->supports_text_selection())
    {
        QMessageBox::information(this, tr("Visual Line Mode"),
                                 tr("Document does not support visual "
                                    "line mode."));
        return;
    }

    if (m_visual_line_mode == state)
        return;

    m_visual_line_mode = state;

    if (m_visual_line_mode)
    {
        if (m_caret_mode)
            ToggleCaretMode();
        m_gview->setMode(GraphicsView::Mode::VisualLine);
        snapVisualLine();
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
DocumentView::ToggleCaretMode() noexcept
{
    if (!m_model->supports_text_selection())
    {
        QMessageBox::information(this, tr("Caret Mode"),
                                 tr("Document does not support caret mode."));
        return;
    }

    m_caret_mode = !m_caret_mode;

    if (m_caret_mode)
    {
        if (m_visual_line_mode)
            set_visual_line_mode(false);

        m_caret_visual        = false;
        m_caret_visual_line   = false;
        m_caret_anchor_index  = -1;
        m_caret_anchor_pageno = -1;
        m_caret_pref_x        = -1.0;
        caretPlaceInitial();

        if (!m_caret_blink_timer)
        {
            m_caret_blink_timer = new QTimer(this);
            m_caret_blink_timer->setInterval(530);
            connect(m_caret_blink_timer, &QTimer::timeout, this, [this]()
            {
                if (m_caret_item)
                    m_caret_item->setVisible(!m_caret_item->isVisible());
            });
        }
        m_caret_blink_timer->start();
        renderCaret();
    }
    else
    {
        if (m_caret_blink_timer)
            m_caret_blink_timer->stop();
        m_caret_visual      = false;
        m_caret_visual_line = false;
        if (m_caret_locate_anim)
            m_caret_locate_anim->stop();
        hideCaret();
    }
}

void
DocumentView::ensureCaretCharsLoaded() noexcept
{
    m_caret_chars = m_model->textCharsForPage(m_caret_pageno);
}

bool
DocumentView::caretIsValidStop(int index) const noexcept
{
    const int n = static_cast<int>(m_caret_chars.size());
    if (index <= 0 || index >= n)
        return true;
    return m_caret_chars[index - 1].rune != '\n';
}

namespace
{
// Line containing `index`: [lineStart, lineEnd), lineEnd being the line's
// '\n' entry (or the end of the page).
template <class Chars>
void
lineRangeIn(const Chars &chars, int index, int &lineStart,
            int &lineEnd) noexcept
{
    const int n = static_cast<int>(chars.size());
    lineStart   = 0;
    lineEnd     = n;
    if (n == 0)
        return;

    int probe = index;
    if (probe >= n || chars[probe].rune == '\n')
        probe = (index > 0) ? index - 1 : 0;
    if (probe >= n)
        probe = n - 1;

    lineStart = probe;
    while (lineStart > 0 && chars[lineStart - 1].rune != '\n')
        lineStart--;

    lineEnd = probe;
    while (lineEnd < n && chars[lineEnd].rune != '\n')
        lineEnd++;
}
} // namespace

void
DocumentView::caretLineRange(int index, int &lineStart,
                             int &lineEnd) const noexcept
{
    lineRangeIn(m_caret_chars, index, lineStart, lineEnd);
}

double
DocumentView::caretCharCenterX(int charIndex) const noexcept
{
    if (charIndex < 0 || charIndex >= static_cast<int>(m_caret_chars.size()))
        return 0.0;
    const fz_quad &q = m_caret_chars[charIndex].quad;
    return (q.ul.x + q.ur.x) / 2.0;
}

QRectF
DocumentView::pagePointsToScene(GraphicsImageItem *pageItem,
                                const QRectF &pagePts) const noexcept
{
    const float scale = m_model->logicalScale();
    const QRectF itemRect(pagePts.x() * scale, pagePts.y() * scale,
                          pagePts.width() * scale, pagePts.height() * scale);

    return pageItem->mapRectToScene(QRectF(
        itemRect.x() / pageItem->scale(), itemRect.y() / pageItem->scale(),
        itemRect.width() / pageItem->scale(),
        itemRect.height() / pageItem->scale()));
}

QRectF
DocumentView::caretSceneRect(const std::vector<Model::CachedTextChar> &chars,
                             int pageno, int index) const noexcept
{
    GraphicsImageItem *pageItem = m_page_items_hash.value(pageno, nullptr);
    if (!pageItem)
        return {};

    const int n = static_cast<int>(chars.size());
    if (n == 0)
        return {};

    const bool useLeftEdge = index < n && chars[index].rune != '\n';

    fz_quad q{};
    if (useLeftEdge)
    {
        q = chars[index].quad;
    }
    else
    {
        int j = std::min(index, n) - 1;
        while (j >= 0 && chars[j].rune == '\n')
            j--;
        if (j < 0)
            return {};
        q = chars[j].quad;
    }

    const double x
        = useLeftEdge ? std::min(q.ul.x, q.ll.x) : std::max(q.ur.x, q.lr.x);
    const double top    = std::min({q.ul.y, q.ur.y, q.ll.y, q.lr.y});
    const double bottom = std::max({q.ul.y, q.ur.y, q.ll.y, q.lr.y});

    return pagePointsToScene(
        pageItem, QRectF(x - 0.6, top, 1.2, std::max(1.0, bottom - top)));
}

bool
DocumentView::caretSelectionPoint(
    const std::vector<Model::CachedTextChar> &chars, int pageno, int index,
    QPointF &out) const noexcept
{
    const QRectF r = caretSceneRect(chars, pageno, index);
    if (r.isEmpty())
        return false;
    out = r.center();
    return true;
}

int
DocumentView::caretIndexNear(const std::vector<Model::CachedTextChar> &chars,
                             int pageno, QPointF scenePos) const noexcept
{
    int best     = 0;
    double bestD = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(chars.size()); ++i)
    {
        if (chars[i].rune == '\n')
            continue;
        const QRectF r = caretSceneRect(chars, pageno, i);
        if (r.isEmpty())
            continue;
        // Weigh the vertical distance so a point on a line prefers that
        // line over a nearer-in-x character on the next one.
        const QPointF c = r.center();
        const double d  = std::abs(c.x() - scenePos.x())
                         + 4.0 * std::abs(c.y() - scenePos.y());
        if (d < bestD)
        {
            bestD = d;
            best  = i;
        }
    }
    return best;
}

void
DocumentView::caretEnterPage(int pageno) noexcept
{
    const bool single = m_layout_mode == LayoutMode::SINGLE;
    if (single || !m_page_items_hash.value(pageno, nullptr))
    {
        // GotoPage() clears the text selection in single-page layout, so a
        // caret anchor can no longer be honoured either.
        if (single)
        {
            m_caret_anchor_index  = -1;
            m_caret_anchor_pageno = -1;
        }
        GotoPage(pageno);
    }

    m_caret_pageno = pageno;
    ensureCaretCharsLoaded();
}

bool
DocumentView::caretEnterAdjacentTextPage(bool next) noexcept
{
    const int N    = m_model->numPages();
    const int step = next ? 1 : -1;
    for (int p = m_caret_pageno + step; p >= 0 && p < N; p += step)
    {
        if (!m_model->textCharsForPage(p).empty())
        {
            caretEnterPage(p);
            return true;
        }
    }
    return false;
}

void
DocumentView::caretPlaceInitial() noexcept
{
    m_caret_index = 0;

    // Carry on from a mouse selection, at its end.
    if (hasTextSelection())
    {
        m_caret_pageno = m_selection_end_page;
        ensureCaretCharsLoaded();
        m_caret_index
            = caretIndexNear(m_caret_chars, m_caret_pageno, m_selection_end);
        return;
    }

    // Otherwise the first character visible in the viewport.
    const QRectF visible
        = m_gview->mapToScene(m_gview->viewport()->rect()).boundingRect();

    int page                = m_pageno;
    int probePage           = -1;
    GraphicsImageItem *item = nullptr;
    if (pageAtScenePos(QPointF(visible.center().x(), visible.top() + 1),
                       probePage, item))
        page = probePage;

    m_caret_pageno = page;
    ensureCaretCharsLoaded();

    for (int i = 0; i < static_cast<int>(m_caret_chars.size()); ++i)
    {
        if (m_caret_chars[i].rune == '\n')
            continue;
        const QRectF r = caretSceneRect(m_caret_chars, page, i);
        if (!r.isEmpty() && r.top() >= visible.top() && r.intersects(visible))
        {
            m_caret_index = i;
            return;
        }
    }
}

void
DocumentView::caretStepLeft() noexcept
{
    const int n = static_cast<int>(m_caret_chars.size());
    if (n > 0)
    {
        int i = m_caret_index - 1;
        while (i >= 0 && !caretIsValidStop(i))
            i--;
        if (i >= 0)
        {
            m_caret_index = i;
            return;
        }
    }

    if (caretEnterAdjacentTextPage(false))
        m_caret_index = static_cast<int>(m_caret_chars.size());
}

void
DocumentView::caretStepRight() noexcept
{
    const int n = static_cast<int>(m_caret_chars.size());
    if (n > 0)
    {
        int i = m_caret_index + 1;
        while (i <= n && !caretIsValidStop(i))
            i++;
        if (i <= n)
        {
            m_caret_index = i;
            return;
        }
    }

    if (caretEnterAdjacentTextPage(true))
        m_caret_index = 0;
}

void
DocumentView::caretMoveVertical(bool up) noexcept
{
    if (m_caret_chars.empty())
    {
        if (caretEnterAdjacentTextPage(!up))
            m_caret_index
                = up ? static_cast<int>(m_caret_chars.size()) : 0;
        renderCaret();
        return;
    }

    if (m_caret_pref_x < 0.0)
    {
        const int n = static_cast<int>(m_caret_chars.size());
        const int probe
            = (m_caret_index < n && m_caret_chars[m_caret_index].rune != '\n')
                  ? m_caret_index
                  : std::max(0, m_caret_index - 1);
        m_caret_pref_x = caretCharCenterX(probe);
    }

    int lineStart, lineEnd;
    caretLineRange(m_caret_index, lineStart, lineEnd);

    int targetStart = 0, targetEnd = 0;

    if (up)
    {
        if (lineStart == 0)
        {
            if (!caretEnterAdjacentTextPage(false))
                return;
            caretLineRange(static_cast<int>(m_caret_chars.size()), targetStart,
                           targetEnd);
        }
        else
        {
            caretLineRange(lineStart - 1, targetStart, targetEnd);
        }
    }
    else
    {
        // Every line, the last included, ends in a '\n' entry, so the last
        // line is the one whose end is the final character.
        if (lineEnd + 1 >= static_cast<int>(m_caret_chars.size()))
        {
            if (!caretEnterAdjacentTextPage(true))
                return;
            caretLineRange(0, targetStart, targetEnd);
        }
        else
        {
            caretLineRange(lineEnd + 1, targetStart, targetEnd);
        }
    }

    if (targetStart >= targetEnd)
    {
        m_caret_index = targetStart;
    }
    else
    {
        int best     = targetStart;
        double bestD = std::numeric_limits<double>::max();
        for (int k = targetStart; k < targetEnd; ++k)
        {
            const double d = std::abs(caretCharCenterX(k) - m_caret_pref_x);
            if (d < bestD)
            {
                bestD = d;
                best  = k;
            }
        }
        m_caret_index = best;
    }

    renderCaret();
}

namespace
{
// 0: whitespace (and the synthetic line breaks), 1: word, 2: punctuation
int
caretClassOf(uint32_t rune) noexcept
{
    if (rune == '\n' || QChar::isSpace(rune))
        return 0;
    if (rune == '_' || QChar::isLetterOrNumber(rune))
        return 1;
    return 2;
}
} // namespace

void
DocumentView::caretWordNext() noexcept
{
    const auto cls = [this](int k)
    {
        return (k >= 0 && k < static_cast<int>(m_caret_chars.size()))
                   ? caretClassOf(m_caret_chars[k].rune)
                   : 0;
    };

    int i       = m_caret_index;
    const int n = static_cast<int>(m_caret_chars.size());
    const int c = cls(i);
    if (c != 0)
        while (i < n && cls(i) == c)
            ++i;
    while (i < n && cls(i) == 0)
        ++i;
    m_caret_index = i;

    if (i >= n && caretEnterAdjacentTextPage(true))
    {
        i = 0;
        while (i < static_cast<int>(m_caret_chars.size()) && cls(i) == 0)
            ++i;
        m_caret_index = i;
    }
}

void
DocumentView::caretWordPrev() noexcept
{
    const auto cls = [this](int k)
    {
        return (k >= 0 && k < static_cast<int>(m_caret_chars.size()))
                   ? caretClassOf(m_caret_chars[k].rune)
                   : 0;
    };

    int i = std::min(m_caret_index, static_cast<int>(m_caret_chars.size()));
    for (;;)
    {
        while (i > 0 && cls(i - 1) == 0)
            --i;
        if (i > 0)
            break;
        // Nothing but whitespace before us on this page: continue on the
        // previous one.
        m_caret_index = 0;
        if (!caretEnterAdjacentTextPage(false))
            return;
        i = static_cast<int>(m_caret_chars.size());
    }

    const int c = cls(i - 1);
    while (i > 0 && cls(i - 1) == c)
        --i;
    m_caret_index = i;
}

void
DocumentView::caretWordEnd() noexcept
{
    const auto cls = [this](int k)
    {
        return (k >= 0 && k < static_cast<int>(m_caret_chars.size()))
                   ? caretClassOf(m_caret_chars[k].rune)
                   : 0;
    };

    int i = m_caret_index;
    for (;;)
    {
        const int n = static_cast<int>(m_caret_chars.size());
        while (i < n && cls(i) == 0)
            ++i;
        if (i < n)
            break;
        if (!caretEnterAdjacentTextPage(true))
        {
            m_caret_index = n;
            return;
        }
        i = 0;
    }

    const int n = static_cast<int>(m_caret_chars.size());
    const int c = cls(i);
    while (i < n && cls(i) == c)
        ++i;
    m_caret_index = i;
}

void
DocumentView::caretFollowSearchHit(int pageno, const fz_quad &quad) noexcept
{
    if (!m_caret_mode)
        return;

    // Single-page layout drops the selection when the page changes.
    if (pageno != m_caret_pageno && m_layout_mode == LayoutMode::SINGLE)
        caretExitVisual();

    m_caret_pageno = pageno;
    ensureCaretCharsLoaded();
    if (m_caret_chars.empty())
        return;

    int best     = 0;
    double bestD = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(m_caret_chars.size()); ++i)
    {
        if (m_caret_chars[i].rune == '\n')
            continue;
        const fz_quad &q = m_caret_chars[i].quad;
        const double d   = std::abs(q.ul.x - quad.ul.x)
                         + 4.0 * std::abs(q.ul.y - quad.ul.y);
        if (d < bestD)
        {
            bestD = d;
            best  = i;
        }
    }

    m_caret_index  = best;
    m_caret_pref_x = -1.0;
    if (m_caret_anchor_index >= 0)
        updateCaretSelection();
    renderCaret();
}

void
DocumentView::caretToDocEdge(bool end) noexcept
{
    const int N = m_model->numPages();
    for (int k = 0; k < N; ++k)
    {
        const int p = end ? N - 1 - k : k;
        if (m_model->textCharsForPage(p).empty())
            continue;
        caretEnterPage(p);
        m_caret_index = end ? static_cast<int>(m_caret_chars.size()) : 0;
        return;
    }
}

void
DocumentView::renderCaret() noexcept
{
    if (!m_caret_mode)
        return;

    const QRectF rect
        = caretSceneRect(m_caret_chars, m_caret_pageno, m_caret_index);
    m_caret_render_pending = rect.isEmpty();
    if (rect.isEmpty())
    {
        hideCaret();
        return;
    }

    QPainterPath path;
    path.addRect(rect);

    if (!m_caret_item)
    {
        m_caret_item = m_gscene->addPath(path);
        m_caret_item->setBrush(QBrush(Qt::red));
        m_caret_item->setPen(Qt::NoPen);
        m_caret_item->setZValue(ZVALUE_TEXT_SELECTION + 1);
    }
    else
    {
        m_caret_item->setPath(path);
    }
    m_caret_item->setVisible(true);

    if (m_caret_blink_timer)
        m_caret_blink_timer
            ->start(); // restart so it's solid right after a move

    m_gview->ensureVisible(rect, 40, 40);
}

void
DocumentView::hideCaret() noexcept
{
    if (m_caret_item)
        m_caret_item->hide();
    if (m_caret_locate_item)
        m_caret_locate_item->hide();
}

void
DocumentView::caretLocate() noexcept
{
    if (!m_caret_mode)
        return;

    // The caret may be hidden or on a page that was left (a jump from
    // search, outline, goto...): bring its page back first.
    if (m_caret_chars.empty() || !m_page_items_hash.value(m_caret_pageno))
    {
        caretEnterPage(m_caret_pageno);
        m_caret_index = std::min(m_caret_index,
                                 static_cast<int>(m_caret_chars.size()));
    }

    renderCaret();
    const QRectF rect
        = caretSceneRect(m_caret_chars, m_caret_pageno, m_caret_index);
    if (rect.isEmpty())
        return;

    m_gview->centerOn(rect.center());

    if (!m_caret_locate_item)
    {
        m_caret_locate_item = m_gscene->addEllipse(QRectF());
        QPen pen(Qt::red, 3);
        pen.setCosmetic(true);
        m_caret_locate_item->setPen(pen);
        m_caret_locate_item->setBrush(Qt::NoBrush);
        m_caret_locate_item->setZValue(ZVALUE_TEXT_SELECTION + 2);
    }

    if (!m_caret_locate_anim)
    {
        m_caret_locate_anim = new QVariantAnimation(this);
        m_caret_locate_anim->setDuration(600);
        m_caret_locate_anim->setLoopCount(2);
        m_caret_locate_anim->setStartValue(0.0);
        m_caret_locate_anim->setEndValue(1.0);
        connect(m_caret_locate_anim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v)
        {
            if (!m_caret_mode || !m_caret_locate_item)
                return;
            const QRectF caret
                = caretSceneRect(m_caret_chars, m_caret_pageno, m_caret_index);
            if (caret.isEmpty())
                return;

            // The ring shrinks from 90 px to 12 px around the caret,
            // whatever the zoom.
            const double t     = v.toDouble();
            const double zoom  = std::max(0.01, m_gview->transform().m11());
            const double r     = (90.0 - 78.0 * t) / zoom;
            const QPointF c    = caret.center();
            m_caret_locate_item->setRect(c.x() - r, c.y() - r, 2 * r, 2 * r);
            m_caret_locate_item->setOpacity(0.35 + 0.65 * t);
            m_caret_locate_item->show();
        });
        connect(m_caret_locate_anim, &QVariantAnimation::finished, this,
                [this]()
        {
            if (m_caret_locate_item)
                m_caret_locate_item->hide();
        });
    }

    m_caret_locate_anim->stop();
    m_caret_locate_anim->start();
}

void
DocumentView::updateCaretSelection() noexcept
{
    if (m_caret_anchor_index < 0)
        return;

    const bool samePage = m_caret_anchor_pageno == m_caret_pageno;
    if (!m_caret_visual_line && samePage
        && m_caret_anchor_index == m_caret_index)
    {
        ClearTextSelection();
        return;
    }

    std::vector<Model::CachedTextChar> anchorChars;
    const std::vector<Model::CachedTextChar> *anchorSrc = &m_caret_chars;
    if (!samePage)
    {
        anchorChars = m_model->textCharsForPage(m_caret_anchor_pageno);
        anchorSrc   = &anchorChars;
    }

    int anchorIdx = m_caret_anchor_index;
    int focusIdx  = m_caret_index;

    if (m_caret_visual_line)
    {
        // Whole lines: from the start of the first line to the end of the
        // last, whichever of anchor and focus comes first.
        const bool anchorFirst
            = m_caret_anchor_pageno < m_caret_pageno
              || (samePage && m_caret_anchor_index <= m_caret_index);

        int aStart, aEnd, fStart, fEnd;
        lineRangeIn(*anchorSrc, anchorIdx, aStart, aEnd);
        lineRangeIn(m_caret_chars, focusIdx, fStart, fEnd);

        anchorIdx = anchorFirst ? aStart : aEnd;
        focusIdx  = anchorFirst ? fEnd : fStart;
    }

    // The selection engine takes the text between the character boundaries
    // nearest to the two points, so use the caret edges.
    QPointF anchorPos, focusPos;
    if (!caretSelectionPoint(*anchorSrc, m_caret_anchor_pageno, anchorIdx,
                             anchorPos)
        || !caretSelectionPoint(m_caret_chars, m_caret_pageno, focusIdx,
                                focusPos))
        return;

    handleTextSelection(anchorPos, focusPos);
}

bool
DocumentView::caretBeginMotion(bool extend) noexcept
{
    const bool ext = extend || m_caret_visual;
    if (ext)
    {
        if (m_caret_anchor_index < 0)
        {
            m_caret_anchor_index  = m_caret_index;
            m_caret_anchor_pageno = m_caret_pageno;
        }
    }
    else
    {
        m_caret_anchor_index = -1;
        ClearTextSelection();
    }
    return ext;
}

void
DocumentView::caretRunMotion(const std::function<void()> &motion,
                             bool extend) noexcept
{
    if (!m_caret_mode)
        return;

    const bool ext = caretBeginMotion(extend);
    m_caret_pref_x = -1.0;
    motion();

    if (ext)
        updateCaretSelection();
    renderCaret();
}

void
DocumentView::caretVerticalMotion(bool up, bool extend) noexcept
{
    if (!m_caret_mode)
        return;

    const bool ext = caretBeginMotion(extend);
    caretMoveVertical(up);
    if (ext)
        updateCaretSelection();
}

void
DocumentView::caretMoveLeft() noexcept
{
    caretRunMotion([this] { caretStepLeft(); }, false);
}

void
DocumentView::caretMoveRight() noexcept
{
    caretRunMotion([this] { caretStepRight(); }, false);
}

void
DocumentView::caretMoveUp() noexcept
{
    caretVerticalMotion(true, false);
}

void
DocumentView::caretMoveDown() noexcept
{
    caretVerticalMotion(false, false);
}

void
DocumentView::caretMoveLineStart() noexcept
{
    if (!m_caret_mode || m_caret_chars.empty())
        return;
    const bool ext = caretBeginMotion(false);
    int lineStart, lineEnd;
    caretLineRange(m_caret_index, lineStart, lineEnd);
    m_caret_index  = lineStart;
    m_caret_pref_x = caretCharCenterX(lineStart);
    if (ext)
        updateCaretSelection();
    renderCaret();
}

void
DocumentView::caretMoveLineEnd() noexcept
{
    if (!m_caret_mode || m_caret_chars.empty())
        return;
    const bool ext = caretBeginMotion(false);
    int lineStart, lineEnd;
    caretLineRange(m_caret_index, lineStart, lineEnd);
    m_caret_index  = lineEnd;
    m_caret_pref_x = caretCharCenterX(lineEnd > 0 ? lineEnd - 1 : 0);
    if (ext)
        updateCaretSelection();
    renderCaret();
}

void
DocumentView::caretSelectLeft() noexcept
{
    caretRunMotion([this] { caretStepLeft(); }, true);
}

void
DocumentView::caretSelectRight() noexcept
{
    caretRunMotion([this] { caretStepRight(); }, true);
}

void
DocumentView::caretSelectUp() noexcept
{
    caretVerticalMotion(true, true);
}

void
DocumentView::caretSelectDown() noexcept
{
    caretVerticalMotion(false, true);
}

void
DocumentView::caretMoveWordNext() noexcept
{
    caretRunMotion([this] { caretWordNext(); }, false);
}

void
DocumentView::caretMoveWordPrev() noexcept
{
    caretRunMotion([this] { caretWordPrev(); }, false);
}

void
DocumentView::caretMoveDocStart() noexcept
{
    caretRunMotion([this] { caretToDocEdge(false); }, false);
}

void
DocumentView::caretMoveDocEnd() noexcept
{
    caretRunMotion([this] { caretToDocEdge(true); }, false);
}

void
DocumentView::caretSelectWordNext() noexcept
{
    caretRunMotion([this] { caretWordNext(); }, true);
}

void
DocumentView::caretSelectWordPrev() noexcept
{
    caretRunMotion([this] { caretWordPrev(); }, true);
}

void
DocumentView::caretSelectDocStart() noexcept
{
    caretRunMotion([this] { caretToDocEdge(false); }, true);
}

void
DocumentView::caretSelectDocEnd() noexcept
{
    caretRunMotion([this] { caretToDocEdge(true); }, true);
}

void
DocumentView::caretHighlightSelection() noexcept
{
    if (!m_caret_mode || m_caret_anchor_index < 0 || !hasTextSelection())
        return;

    handleTextHighlightRequested(); // clears the selection when done
    m_caret_visual        = false;
    m_caret_visual_line   = false;
    m_caret_anchor_index  = -1;
    m_caret_anchor_pageno = -1;
    renderCaret();
}

void
DocumentView::caretMoveWordEnd() noexcept
{
    caretRunMotion([this] { caretWordEnd(); }, false);
}

void
DocumentView::caretToggleVisual() noexcept
{
    if (!m_caret_mode)
        return;

    if (m_caret_visual && !m_caret_visual_line)
    {
        caretExitVisual();
        return;
    }

    // From linewise, v goes to charwise visual, as in vim.
    const bool wasLinewise = m_caret_visual_line;
    m_caret_visual         = true;
    m_caret_visual_line    = false;
    if (m_caret_anchor_index < 0)
    {
        m_caret_anchor_index  = m_caret_index;
        m_caret_anchor_pageno = m_caret_pageno;
    }
    if (wasLinewise)
        updateCaretSelection();
}

void
DocumentView::caretToggleVisualLine() noexcept
{
    if (!m_caret_mode)
        return;

    if (m_caret_visual_line)
    {
        caretExitVisual();
        return;
    }

    m_caret_visual      = true;
    m_caret_visual_line = true;
    if (m_caret_anchor_index < 0)
    {
        m_caret_anchor_index  = m_caret_index;
        m_caret_anchor_pageno = m_caret_pageno;
    }
    updateCaretSelection();
}

void
DocumentView::caretExitVisual() noexcept
{
    if (!m_caret_visual)
        return;

    m_caret_visual        = false;
    m_caret_visual_line   = false;
    m_caret_anchor_index  = -1;
    m_caret_anchor_pageno = -1;
    ClearTextSelection();
}
