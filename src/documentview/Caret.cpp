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

        m_caret_pageno        = m_pageno;
        m_caret_anchor_index  = -1;
        m_caret_anchor_pageno = -1;
        m_caret_pref_x        = -1.0;
        ensureCaretCharsLoaded();
        m_caret_index = 0;

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

void
DocumentView::caretLineRange(int index, int &lineStart,
                             int &lineEnd) const noexcept
{
    const int n = static_cast<int>(m_caret_chars.size());
    lineStart   = 0;
    lineEnd     = n;
    if (n == 0)
        return;

    int probe = index;
    if (probe >= n || m_caret_chars[probe].rune == '\n')
        probe = (index > 0) ? index - 1 : 0;
    if (probe >= n)
        probe = n - 1;

    lineStart = probe;
    while (lineStart > 0 && m_caret_chars[lineStart - 1].rune != '\n')
        lineStart--;

    lineEnd = probe;
    while (lineEnd < n && m_caret_chars[lineEnd].rune != '\n')
        lineEnd++;
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
DocumentView::caretSceneRect(int pageno, int index) const noexcept
{
    GraphicsImageItem *pageItem = m_page_items_hash.value(pageno, nullptr);
    if (!pageItem)
        return {};

    // Only valid against m_caret_chars when pageno == m_caret_pageno (the
    // only page whose chars are currently loaded); callers keep those in
    // sync with each other.
    const int n = static_cast<int>(m_caret_chars.size());
    if (n == 0)
        return {};

    const bool useLeftEdge = index < n && m_caret_chars[index].rune != '\n';

    fz_quad q{};
    if (useLeftEdge)
    {
        q = m_caret_chars[index].quad;
    }
    else
    {
        int j = index - 1;
        while (j >= 0 && m_caret_chars[j].rune == '\n')
            j--;
        if (j < 0)
            return {};
        q = m_caret_chars[j].quad;
    }

    const double x
        = useLeftEdge ? std::min(q.ul.x, q.ll.x) : std::max(q.ur.x, q.lr.x);
    const double top    = std::min({q.ul.y, q.ur.y, q.ll.y, q.lr.y});
    const double bottom = std::max({q.ul.y, q.ur.y, q.ll.y, q.lr.y});
    const QRectF pagePts(x - 0.6, top, 1.2, std::max(1.0, bottom - top));

    const float scale = m_model->logicalScale();
    const QRectF itemRect(pagePts.x() * scale, pagePts.y() * scale,
                          pagePts.width() * scale, pagePts.height() * scale);

    return pageItem->mapRectToScene(QRectF(
        itemRect.x() / pageItem->scale(), itemRect.y() / pageItem->scale(),
        itemRect.width() / pageItem->scale(),
        itemRect.height() / pageItem->scale()));
}

void
DocumentView::caretStepLeft() noexcept
{
    if (m_caret_chars.empty())
        return;

    int i = m_caret_index - 1;
    while (i >= 0 && !caretIsValidStop(i))
        i--;

    if (i >= 0)
    {
        m_caret_index = i;
    }
    else if (m_pageno > 0)
    {
        GotoPrevPage();
        m_caret_pageno = m_pageno;
        ensureCaretCharsLoaded();
        m_caret_index = static_cast<int>(m_caret_chars.size());
    }
}

void
DocumentView::caretStepRight() noexcept
{
    if (m_caret_chars.empty())
        return;

    const int n = static_cast<int>(m_caret_chars.size());
    int i       = m_caret_index + 1;
    while (i <= n && !caretIsValidStop(i))
        i++;

    if (i <= n)
    {
        m_caret_index = i;
    }
    else if (m_pageno + 1 < m_model->numPages())
    {
        GotoNextPage();
        m_caret_pageno = m_pageno;
        ensureCaretCharsLoaded();
        m_caret_index = 0;
    }
}

void
DocumentView::caretMoveVertical(bool up) noexcept
{
    if (m_caret_chars.empty())
        return;

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
            if (m_pageno == 0)
                return;
            GotoPrevPage();
            m_caret_pageno = m_pageno;
            ensureCaretCharsLoaded();
            if (m_caret_chars.empty())
            {
                m_caret_index = 0;
                renderCaret();
                return;
            }
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
        if (lineEnd >= static_cast<int>(m_caret_chars.size()))
        {
            if (m_pageno + 1 >= m_model->numPages())
                return;
            GotoNextPage();
            m_caret_pageno = m_pageno;
            ensureCaretCharsLoaded();
            if (m_caret_chars.empty())
            {
                m_caret_index = 0;
                renderCaret();
                return;
            }
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

void
DocumentView::renderCaret() noexcept
{
    if (!m_caret_mode)
        return;

    const QRectF rect = caretSceneRect(m_caret_pageno, m_caret_index);
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
}

void
DocumentView::updateCaretSelection() noexcept
{
    if (m_caret_anchor_index < 0)
        return;

    const QPointF anchorPos
        = caretSceneRect(m_caret_anchor_pageno, m_caret_anchor_index).center();
    const QPointF focusPos
        = caretSceneRect(m_caret_pageno, m_caret_index).center();

    handleTextSelection(anchorPos, focusPos);
}

void
DocumentView::caretMoveLeft() noexcept
{
    if (!m_caret_mode)
        return;
    m_caret_anchor_index = -1;
    ClearTextSelection();
    m_caret_pref_x = -1.0;
    caretStepLeft();
    renderCaret();
}

void
DocumentView::caretMoveRight() noexcept
{
    if (!m_caret_mode)
        return;
    m_caret_anchor_index = -1;
    ClearTextSelection();
    m_caret_pref_x = -1.0;
    caretStepRight();
    renderCaret();
}

void
DocumentView::caretMoveUp() noexcept
{
    if (!m_caret_mode)
        return;
    m_caret_anchor_index = -1;
    ClearTextSelection();
    caretMoveVertical(true);
}

void
DocumentView::caretMoveDown() noexcept
{
    if (!m_caret_mode)
        return;
    m_caret_anchor_index = -1;
    ClearTextSelection();
    caretMoveVertical(false);
}

void
DocumentView::caretMoveLineStart() noexcept
{
    if (!m_caret_mode || m_caret_chars.empty())
        return;
    m_caret_anchor_index = -1;
    ClearTextSelection();
    int lineStart, lineEnd;
    caretLineRange(m_caret_index, lineStart, lineEnd);
    m_caret_index  = lineStart;
    m_caret_pref_x = caretCharCenterX(lineStart);
    renderCaret();
}

void
DocumentView::caretMoveLineEnd() noexcept
{
    if (!m_caret_mode || m_caret_chars.empty())
        return;
    m_caret_anchor_index = -1;
    ClearTextSelection();
    int lineStart, lineEnd;
    caretLineRange(m_caret_index, lineStart, lineEnd);
    m_caret_index  = lineEnd;
    m_caret_pref_x = caretCharCenterX(lineEnd > 0 ? lineEnd - 1 : 0);
    renderCaret();
}

void
DocumentView::caretSelectLeft() noexcept
{
    if (!m_caret_mode)
        return;
    if (m_caret_anchor_index < 0)
    {
        m_caret_anchor_index  = m_caret_index;
        m_caret_anchor_pageno = m_caret_pageno;
    }
    m_caret_pref_x = -1.0;
    caretStepLeft();
    updateCaretSelection();
    renderCaret();
}

void
DocumentView::caretSelectRight() noexcept
{
    if (!m_caret_mode)
        return;
    if (m_caret_anchor_index < 0)
    {
        m_caret_anchor_index  = m_caret_index;
        m_caret_anchor_pageno = m_caret_pageno;
    }
    m_caret_pref_x = -1.0;
    caretStepRight();
    updateCaretSelection();
    renderCaret();
}

void
DocumentView::caretSelectUp() noexcept
{
    if (!m_caret_mode)
        return;
    if (m_caret_anchor_index < 0)
    {
        m_caret_anchor_index  = m_caret_index;
        m_caret_anchor_pageno = m_caret_pageno;
    }
    caretMoveVertical(true);
    updateCaretSelection();
}

void
DocumentView::caretSelectDown() noexcept
{
    if (!m_caret_mode)
        return;
    if (m_caret_anchor_index < 0)
    {
        m_caret_anchor_index  = m_caret_index;
        m_caret_anchor_pageno = m_caret_pageno;
    }
    caretMoveVertical(false);
    updateCaretSelection();
}
