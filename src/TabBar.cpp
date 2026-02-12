#include "TabBar.hpp"

TabBar::TabBar(QWidget *parent) : QTabBar(parent)
{
    setAcceptDrops(true);
    setElideMode(Qt::TextElideMode::ElideRight);
    setDrawBase(false);
    setMovable(true);
}

void
TabBar::setSplitCount(int index, int count) noexcept
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

    // Request tab data from the parent widget
    TabData tabData;
    emit tabDataRequested(m_drag_tab_index, &tabData);
    if (tabData.filePath.isEmpty())
        return;

    m_drop_received  = false;
    int draggedIndex = m_drag_tab_index;

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
    drag->setHotSpot(event->pos() - tabRect.topLeft());

    Qt::DropAction result = drag->exec(Qt::MoveAction | Qt::CopyAction);
    m_drag_tab_index      = -1;

    // If the drag was accepted by another window, close this tab
    if (result == Qt::MoveAction && !m_drop_received)
    {
        emit tabDetached(draggedIndex, QCursor::pos());
    }
    else if (result == Qt::IgnoreAction)
    {
        // Tab was dropped outside any accepting window - detach to new
        // window
        emit tabDetachedToNewWindow(draggedIndex, tabData);
    }
}

void
TabBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_drag_tab_index = -1;
    QTabBar::mouseReleaseEvent(event);
}

void
TabBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(MIME_TYPE))
    {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    else if (event->mimeData()->hasUrls())
    {
        // Accept file drops
        for (const QUrl &url : event->mimeData()->urls())
        {
            if (url.isLocalFile()
                && url.toLocalFile().endsWith(".pdf", Qt::CaseInsensitive))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void
TabBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(MIME_TYPE))
    {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    else if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void
TabBar::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat(MIME_TYPE))
    {
        m_drop_received = true;

        // Handle Internal Move
        if (event->source() == this)
        {
            int toIndex   = tabAt(event->position().toPoint());
            int fromIndex = m_drag_tab_index; // This is valid because we
                                              // haven't cleared it yet

            if (toIndex != -1 && fromIndex != -1 && fromIndex != toIndex)
            {
                moveTab(fromIndex, toIndex);
                setCurrentIndex(toIndex); // Optional: keep the moved tab active
                event->setDropAction(Qt::MoveAction);
                event->accept();
                return;
            }

            // If it was dropped on itself, still accept to prevent detachment
            event->acceptProposedAction();
            return;
        }

        TabData tabData
            = TabData::deserialize(event->mimeData()->data(MIME_TYPE));

        if (!tabData.filePath.isEmpty())
        {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            emit tabDropReceived(tabData);
        }
    }
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
    const int paddingX = 6;
    const int paddingY = 2;
    const int margin   = 6;

    for (int i = 0; i < count(); ++i)
    {
        const int badgeCount = splitCount(i);
        if (badgeCount <= 1)
            continue;

        const QString text = QString::number(badgeCount);
        const int textW    = fm.horizontalAdvance(text);
        const int textH    = fm.height();
        const int badgeW   = textW + paddingX * 2;
        const int badgeH   = textH + paddingY * 2;
        const int radius   = badgeH / 2;

        QRect tabRect = this->tabRect(i);
        if (!tabRect.isValid())
            continue;

        int badgeLeft = tabRect.right() - badgeW - margin;
        int badgeTop  = tabRect.top() + margin;

        if (QWidget *closeButton = tabButton(i, QTabBar::RightSide))
        {
            const QRect closeRect = closeButton->geometry();
            if (closeRect.isValid())
            {
                badgeLeft = closeRect.left() - badgeW - margin;
                badgeTop  = closeRect.center().y() - badgeH / 2;
            }
        }

        QRect badgeRect(badgeLeft, badgeTop, badgeW, badgeH);

        QColor bg = palette().color(QPalette::Highlight);
        QColor fg = palette().color(QPalette::HighlightedText);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(badgeRect, radius, radius);

        painter.setPen(fg);
        painter.drawText(badgeRect, Qt::AlignCenter, text);
    }
}
