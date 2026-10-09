#include "TabBar.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QStylePainter>
#include <algorithm>
#include <cmath>

TabBar::TabBar(QWidget *parent) : QTabBar(parent)
{
    setElideMode(Qt::TextElideMode::ElideRight);
    setDrawBase(false);
    setMovable(false); // We handle reordering manually
    setAcceptDrops(true);
    setTabsClosable(true); // matches the CloseButtonMode::All default
    setMouseTracking(true); // for the scrolling title of the hovered tab

    m_scroll_timer = new QTimer(this);
    m_scroll_timer->setInterval(33);
    connect(m_scroll_timer, &QTimer::timeout, this, [this]()
    {
        const int tab = scrollingTab();
        if (tab < 0)
        {
            m_scroll_timer->stop();
            update();
            return;
        }
        update(tabRect(tab));
    });

    connect(this, &QTabBar::currentChanged, this, &TabBar::refreshCloseButtons);
}

void
TabBar::setCloseButtonMode(CloseButtonMode mode) noexcept
{
    if (m_close_button_mode == mode)
        return;
    m_close_button_mode = mode;

    // Clear any custom Current-mode buttons before switching representation
    // — setTabsClosable(true) below would otherwise leave them in place
    // alongside (or instead of) Qt's own buttons.
    for (int i = 0; i < count(); ++i)
        setTabButton(i, QTabBar::RightSide, nullptr);

    switch (mode)
    {
        case CloseButtonMode::All:
            setTabsClosable(true);
            break;
        case CloseButtonMode::Hidden:
            setTabsClosable(false);
            break;
        case CloseButtonMode::Current:
            setTabsClosable(false); // managed manually from here on
            refreshCloseButtons();
            break;
    }
}

void
TabBar::refreshCloseButtons() noexcept
{
    if (m_close_button_mode != CloseButtonMode::Current)
        return;

    for (int i = 0; i < count(); ++i)
    {
        if (i == currentIndex())
        {
            if (tabButton(i, QTabBar::RightSide))
                continue; // already has one

            auto *button = new QToolButton(this);
            button->setText(QStringLiteral("✕"));
            button->setAutoRaise(true);
            button->setCursor(Qt::ArrowCursor);
            // The button only ever lives on whichever tab is current, so
            // closing "the tab this button is on" is always currentIndex().
            connect(button, &QToolButton::clicked, this,
                    [this]() { emit tabCloseRequested(currentIndex()); });
            setTabButton(i, QTabBar::RightSide, button);
        }
        else
        {
            setTabButton(i, QTabBar::RightSide, nullptr);
        }
    }
}

void
TabBar::setTabFailed(int index, bool failed) noexcept
{
    if (index < 0 || index >= count())
        return;
    if (failed)
        m_failed_tabs.insert(index);
    else
        m_failed_tabs.remove(index);
    update(tabRect(index));
}

bool
TabBar::isTabFailed(int index) const noexcept
{
    return m_failed_tabs.contains(index);
}

void
TabBar::set_split_count(int index, int count) noexcept
{
    if (index < 0 || index >= this->count())
        return;

    const int clampedCount = qMax(1, count);
    if (m_split_counts.value(index, 1) == clampedCount)
        return;

    if (m_split_counts.size() <= index)
        m_split_counts.resize(index + 1);
    m_split_counts[index] = clampedCount;
    update(tabRect(index));
}

void
TabBar::setTabText(int index, const QString &text)
{
    if (hasCustomTitle(index))
        return;
    QTabBar::setTabText(index, text);
}

void
TabBar::setCustomTitle(int index, const QString &title)
{
    if (index < 0 || index >= count())
        return;
    m_custom_titles.insert(tabData(index).toInt(), title);
    QTabBar::setTabText(index, title);
}

void
TabBar::clearCustomTitle(int index)
{
    if (index < 0 || index >= count())
        return;
    m_custom_titles.remove(tabData(index).toInt());
}

bool
TabBar::hasCustomTitle(int index) const noexcept
{
    return index >= 0 && index < count()
           && m_custom_titles.contains(tabData(index).toInt());
}

int
TabBar::splitCount(int index) const noexcept
{
    if (index < 0 || index >= this->count())
        return 1;

    return m_split_counts.value(index, 1);
}

void
TabBar::tabInserted(int index)
{
    QTabBar::tabInserted(index);
    if (index < 0)
        return;
    if (index > m_split_counts.size())
        m_split_counts.resize(index);
    m_split_counts.insert(index, 1);
    refreshCloseButtons();
}

void
TabBar::tabRemoved(int index)
{
    QTabBar::tabRemoved(index);
    if (index < 0 || index >= m_split_counts.size())
        return;
    m_split_counts.removeAt(index);
    refreshCloseButtons();

    if (!m_selected_tabs.isEmpty())
    {
        QSet<int> shifted;
        for (int i : std::as_const(m_selected_tabs))
            if (i < index)
                shifted.insert(i);
            else if (i > index)
                shifted.insert(i - 1);
        m_selected_tabs    = shifted;
        m_selection_anchor = -1;
    }

    // Shift m_failed_tabs indices down past the removed tab.
    if (!m_failed_tabs.isEmpty())
    {
        QSet<int> shifted;
        for (int i : std::as_const(m_failed_tabs))
        {
            if (i < index)
                shifted.insert(i);
            else if (i > index)
                shifted.insert(i - 1);
            // i == index: the failed tab itself was removed, drop it.
        }
        m_failed_tabs = shifted;
    }
}

void
TabBar::tabMoved(int from, int to)
{
    QTabBar::tabMoved(from, to);
    if (from < 0 || from >= m_split_counts.size())
        return;
    if (to < 0 || to >= m_split_counts.size())
        return;
    m_split_counts.move(from, to);

    if (!m_selected_tabs.isEmpty())
    {
        QSet<int> moved;
        for (int i : std::as_const(m_selected_tabs))
        {
            if (i == from)
                moved.insert(to);
            else if (from < to && i > from && i <= to)
                moved.insert(i - 1);
            else if (from > to && i >= to && i < from)
                moved.insert(i + 1);
            else
                moved.insert(i);
        }
        m_selected_tabs    = moved;
        m_selection_anchor = -1;
    }

    if (m_failed_tabs.contains(from))
    {
        m_failed_tabs.remove(from);
        m_failed_tabs.insert(to);
    }
}

void
TabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        const int index  = tabAt(event->pos());
        const bool ctrl  = event->modifiers() & Qt::ControlModifier;
        const bool shift = event->modifiers() & Qt::ShiftModifier;

        if (index >= 0 && (ctrl || shift))
        {
            if (shift && m_selection_anchor >= 0)
            {
                m_selected_tabs.clear();
                for (int i = qMin(m_selection_anchor, index);
                     i <= qMax(m_selection_anchor, index); ++i)
                    m_selected_tabs.insert(i);
            }
            else if (ctrl && m_selected_tabs.contains(index))
            {
                m_selected_tabs.remove(index);
            }
            else
            {
                if (!ctrl)
                    m_selected_tabs.clear();
                m_selected_tabs.insert(index);
                m_selection_anchor = index;
            }
            update();
            return;
        }

        clearTabSelection();
        m_drag_start_pos = event->pos();
        m_drag_tab_index = index;
    }
    QTabBar::mousePressEvent(event);
}

QList<int>
TabBar::selectedTabs() const
{
    QList<int> result(m_selected_tabs.begin(), m_selected_tabs.end());
    std::sort(result.begin(), result.end());
    return result;
}

void
TabBar::setTabSelected(int index, bool selected) noexcept
{
    if (index < 0 || index >= count())
        return;
    if (selected)
        m_selected_tabs.insert(index);
    else
        m_selected_tabs.remove(index);
    m_selection_anchor = index;
    update();
}

void
TabBar::selectAllTabs() noexcept
{
    for (int i = 0; i < count(); ++i)
        m_selected_tabs.insert(i);
    update();
}

void
TabBar::clearTabSelection() noexcept
{
    if (m_selected_tabs.isEmpty())
        return;
    m_selected_tabs.clear();
    m_selection_anchor = -1;
    update();
}

void
TabBar::setScrollTextOnHover(bool enabled) noexcept
{
    m_scroll_text_on_hover = enabled;
    if (!enabled)
    {
        m_hover_tab = -1;
        m_scroll_timer->stop();
        update();
    }
}

bool
TabBar::isHorizontal() const noexcept
{
    const QTabBar::Shape s = shape();
    return !(s == QTabBar::RoundedWest || s == QTabBar::RoundedEast
             || s == QTabBar::TriangularWest || s == QTabBar::TriangularEast);
}

QRect
TabBar::tabTextRect(int index) const noexcept
{
    QStyleOptionTab opt;
    initStyleOption(&opt, index);
    return style()->subElementRect(QStyle::SE_TabBarTabText, &opt, this);
}

// The hovered tab, if its title is wider than the room it has.
int
TabBar::scrollingTab() const noexcept
{
    if (!m_scroll_text_on_hover || !isHorizontal() || m_hover_tab < 0
        || m_hover_tab >= count())
        return -1;
    const QRect textRect = tabTextRect(m_hover_tab);
    if (!textRect.isValid())
        return -1;
    return fontMetrics().horizontalAdvance(tabText(m_hover_tab))
                   > textRect.width()
               ? m_hover_tab
               : -1;
}

void
TabBar::updateHoveredTab(const QPoint &pos) noexcept
{
    if (!m_scroll_text_on_hover)
        return;
    const int tab = tabAt(pos);
    if (tab == m_hover_tab)
        return;

    const int before = m_hover_tab;
    m_hover_tab      = tab;
    m_scroll_clock.restart();
    if (scrollingTab() >= 0)
        m_scroll_timer->start();
    else
        m_scroll_timer->stop();
    if (before >= 0 && before < count())
        update(tabRect(before)); // back to the normal, shortened title
    if (tab >= 0)
        update(tabRect(tab));
}

void
TabBar::leaveEvent(QEvent *event)
{
    if (m_hover_tab >= 0)
    {
        const int before = m_hover_tab;
        m_hover_tab      = -1;
        m_scroll_timer->stop();
        if (before < count())
            update(tabRect(before));
    }
    QTabBar::leaveEvent(event);
}

// Draws the tabs as QTabBar::paintEvent does, except that tab `scrolling` is
// drawn without its title, and the whole title is drawn in its place, shifted
// along. (Drawing over the normal tab would leave its elided title showing
// through any style whose tabs are not opaque, so the tab has to be drawn
// without it in the first place.)
void
TabBar::paintTabsWithScrollingText(int scrolling) noexcept
{
    QStylePainter painter(this);

    auto drawTab = [&](int i)
    {
        QStyleOptionTab opt;
        initStyleOption(&opt, i);
        if (!(opt.state & QStyle::State_Enabled))
            opt.palette.setCurrentColorGroup(QPalette::Disabled);
        if (i == scrolling)
            opt.text.clear();
        painter.drawControl(QStyle::CE_TabBarTab, opt);
    };

    for (int i = 0; i < count(); ++i)
        if (i != currentIndex())
            drawTab(i);
    if (currentIndex() >= 0)
        drawTab(currentIndex());

    // The title: still for a moment, then slid to its end, still again, and
    // back.
    QStyleOptionTab opt;
    initStyleOption(&opt, scrolling);
    const QRect textRect = tabTextRect(scrolling);
    const QString text   = tabText(scrolling);
    const int textWidth  = fontMetrics().horizontalAdvance(text);
    const int overflow   = textWidth - textRect.width();

    constexpr double pixelsPerSecond = 45.0;
    constexpr double pauseSeconds    = 0.7;
    const double travel              = overflow / pixelsPerSecond;
    const double cycle               = 2.0 * (pauseSeconds + travel);
    double t = std::fmod(m_scroll_clock.elapsed() / 1000.0, cycle);
    double offset;
    if (t < pauseSeconds)
        offset = 0;
    else if (t < pauseSeconds + travel)
        offset = (t - pauseSeconds) * pixelsPerSecond;
    else if (t < 2 * pauseSeconds + travel)
        offset = overflow;
    else
        offset = overflow - (t - 2 * pauseSeconds - travel) * pixelsPerSecond;
    offset = std::clamp(offset, 0.0, static_cast<double>(overflow));

    painter.save();
    painter.setClipRect(textRect);
    painter.setPen(isTabFailed(scrolling)
                       ? QColor(Qt::red)
                       : opt.palette.color(opt.state & QStyle::State_Enabled
                                               ? QPalette::Normal
                                               : QPalette::Disabled,
                                           QPalette::WindowText));
    painter.setFont(font());
    painter.drawText(QRect(textRect.left() - qRound(offset), textRect.top(),
                           textWidth, textRect.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, text);
    painter.restore();
}

void
TabBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        updateHoveredTab(event->position().toPoint());

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

        // The target TabBar's dropEvent() calls
        // event->setDropAction(Qt::MoveAction) whenever it actually
        // receives and accepts the drop, and that's exactly what exec()
        // returns here — Qt already tells us whether some window took
        // it. Don't guess via QApplication::activeWindow(): window
        // managers don't reliably hand OS focus to the drop target
        // synchronously (or sometimes at all), so that heuristic could
        // decide "no target" even when another Lektra window's
        // dropEvent had already fired tabDropReceived and opened the
        // tab there — causing both that window AND a spurious new
        // process (spawned by tabDetachedToNewWindow) to end up with
        // the document.
        const Qt::DropAction result
            = drag->exec(Qt::MoveAction | Qt::IgnoreAction);

        m_drag_tab_index = -1;

        if (result == Qt::MoveAction)
            emit tabDetached(draggedIndex, QCursor::pos());
        else
            emit tabDetachedToNewWindow(draggedIndex, tabData);
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
    const int scrolling = scrollingTab();
    if (scrolling >= 0)
        paintTabsWithScrollingText(scrolling);
    else
        QTabBar::paintEvent(event);
    if (count() == 0)
        return;

    if (!m_selected_tabs.isEmpty())
    {
        QPainter selPainter(this);
        for (int i : std::as_const(m_selected_tabs))
            if (i >= 0 && i < count())
                selPainter.fillRect(tabRect(i), QColor(64, 128, 255, 90));
    }

    if (!m_failed_tabs.isEmpty())
    {
        // Overdraw the title in red for failed tabs, manually — some Qt
        // platform styles/themes ignore per-tab setTabTextColor() and
        // always paint tab text in the theme's own fixed color, so that
        // API alone can silently have no visible effect.
        QPainter textPainter(this);
        textPainter.setRenderHint(QPainter::Antialiasing, true);
        for (int i : std::as_const(m_failed_tabs))
        {
            if (i < 0 || i >= count() || i == scrolling)
                continue; // (a scrolling title is drawn red already)

            QStyleOptionTab opt;
            initStyleOption(&opt, i);
            const QRect textRect
                = style()->subElementRect(QStyle::SE_TabBarTabText, &opt, this);
            if (!textRect.isValid())
                continue;

            const QString elided = fontMetrics().elidedText(
                tabText(i), elideMode(), textRect.width());
            textPainter.setPen(QColor(Qt::red));
            textPainter.setFont(font());
            textPainter.drawText(textRect, Qt::AlignCenter, elided);
        }
    }

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
