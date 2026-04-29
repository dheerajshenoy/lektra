#include "TabBar.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

TabBar::TabBar(QWidget *parent) : QTabBar(parent)
{
    setElideMode(Qt::TextElideMode::ElideRight);
    setDrawBase(false);
    setMovable(false); // We handle reordering manually
    setAcceptDrops(true);
}

void
TabBar::set_split_count(int index, int count) noexcept
{
    if (index < 0 || index >= this->count())
        return;

    const int clampedCount = qMax(1, count);
    if (tabData(index).toInt() == clampedCount)
        return;

    setTabData(index, clampedCount);
    update(tabRect(index));
}

int
TabBar::splitCount(int index) const noexcept
{
    if (index < 0 || index >= this->count())
        return 1;

    const QVariant data = tabData(index);
    return data.isValid() ? data.toInt() : 1;
}

void
TabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_drag_start_pos = event->pos();
        m_drag_tab_index = tabAt(event->pos());
    }
    QTabBar::mousePressEvent(event);
}

void
TabBar::mouseMoveEvent(QMouseEvent *event)
{
    // Early exit if not dragging with left button
    if (!(event->buttons() & Qt::LeftButton) || m_drag_tab_index < 0)
    {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    // Check if we've moved enough to start a drag
    if ((event->pos() - m_drag_start_pos).manhattanLength()
        < QApplication::startDragDistance())
    {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    QPoint globalPos = mapToGlobal(event->pos());
    QWidget *win     = window();

    // If cursor is outside the window, initiate detach
    if (!win->geometry().contains(globalPos))
    {
        // Request tab data from the parent widget
        TabData tabData;
        emit tabDataRequested(m_drag_tab_index, &tabData);

        int draggedIndex = m_drag_tab_index;
        if (tabData.filePath.isEmpty())
        {
            m_drag_tab_index = -1;
            return;
        }

        // Create drag object
        QDrag *drag     = new QDrag(this);
        QMimeData *mime = new QMimeData();
        mime->setData(MIME_TYPE, tabData.serialize());
        mime->setUrls({QUrl::fromLocalFile(tabData.filePath)});
        drag->setMimeData(mime);

        // Create a pixmap of the tab for visual feedback
        QRect tabRect = this->tabRect(draggedIndex);
        QPixmap tabPixmap(tabRect.size());
        tabPixmap.fill(Qt::transparent);
        QPainter painter(&tabPixmap);
        painter.setOpacity(0.8);

        // Render the tab
        QStyleOptionTab opt;
        initStyleOption(&opt, draggedIndex);
        opt.rect = QRect(QPoint(0, 0), tabRect.size());
        style()->drawControl(QStyle::CE_TabBarTab, &opt, &painter, this);
        painter.end();

        drag->setPixmap(tabPixmap);
        drag->setHotSpot(m_drag_start_pos);

        drag->exec(Qt::MoveAction | Qt::IgnoreAction);

        m_drag_tab_index = -1;

        // Process events to let the target window become active
        QApplication::processEvents();

        QWidget *sourceWindow = window();
        QWidget *activeWindow = QApplication::activeWindow();

        // If the active window is different from source, tab was dropped on it
        // (close original tab). Otherwise create new window
        if (activeWindow && activeWindow != sourceWindow
            && activeWindow->isVisible())
        {
            emit tabDetached(draggedIndex, QCursor::pos());
        }
        else
        {
            emit tabDetachedToNewWindow(draggedIndex, tabData);
        }
        return;
    }

    // Inside window - handle tab reordering
    int targetIndex = tabAt(event->pos());
    if (targetIndex != -1 && targetIndex != m_drag_tab_index)
    {
        // Move tab to new position
        moveTab(m_drag_tab_index, targetIndex);
        m_drag_tab_index = targetIndex;
    }
}

void
TabBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_drag_tab_index = -1;
    QTabBar::mouseReleaseEvent(event);
}

void
TabBar::paintEvent(QPaintEvent *event)
{
    QTabBar::paintEvent(event);
    if (count() == 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    QFontMetrics fm(painter.font());

    const int paddingX = 7;
    const int paddingY = 2;

    const QTabBar::Shape s = shape();
    const bool isVertical
        = (s == QTabBar::RoundedWest || s == QTabBar::RoundedEast
           || s == QTabBar::TriangularWest || s == QTabBar::TriangularEast);

    for (int i = 0; i < count(); ++i)
    {
        const int badgeCount = splitCount(i);
        if (badgeCount <= 1)
            continue;

        const QString text  = QString::number(badgeCount);
        const int textW     = fm.horizontalAdvance(text);
        const int textH     = fm.height();
        const int badgeW    = textW + paddingX * 2;
        const int badgeH    = textH + paddingY * 2;
        const int radius    = badgeH / 2;
        const QRect tabRect = this->tabRect(i);
        if (!tabRect.isValid())
            continue;

        int badgeLeft = 0, badgeTop = 0;

        if (!isVertical)
        {
            if (QWidget *closeButton = tabButton(i, QTabBar::RightSide))
            {
                const QRect closeRect = closeButton->geometry();
                if (closeRect.isValid())
                {
                    badgeLeft = badgeW / 2;
                    badgeTop  = closeRect.center().y() - badgeH / 2;
                }
            }
        }
        else
        {
            // We want the badge near the top of the tab rect, centered
            // horizontally — this places it visually before the close button.
            badgeLeft = tabRect.left() + (tabRect.width() - badgeW) / 2;
            badgeTop  = tabRect.bottom() - badgeH - paddingX;
        }

        const QRect badgeRect(badgeLeft, badgeTop, badgeW, badgeH);

        const QColor bg = palette().color(QPalette::Highlight);
        const QColor fg = palette().color(QPalette::HighlightedText);

        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(badgeRect, radius, radius);
        painter.setPen(fg);
        painter.drawText(badgeRect, Qt::AlignCenter, text);
    }
}

QSize
TabBar::tabSizeHint(int index) const
{
    QSize s = QTabBar::tabSizeHint(index);

    const QTabBar::Shape sh = shape();
    const bool isVertical
        = (sh == QTabBar::RoundedWest || sh == QTabBar::RoundedEast
           || sh == QTabBar::TriangularWest || sh == QTabBar::TriangularEast);
    if (!isVertical)
        return s;

    s.setHeight(s.height() + 50);
    return s;
}

void
TabBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(MIME_TYPE))
    {
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}

void
TabBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(MIME_TYPE))
    {
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}

void
TabBar::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasFormat(MIME_TYPE))
    {
        event->ignore();
        return;
    }

    TabData data = TabData::deserialize(mime->data(MIME_TYPE));
    if (data.filePath.isEmpty())
    {
        event->ignore();
        return;
    }

    emit tabDropReceived(data);
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void
TabBar::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(tabAt(event->pos()), event->globalPos());
}
