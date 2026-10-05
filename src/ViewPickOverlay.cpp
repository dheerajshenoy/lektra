#include "ViewPickOverlay.hpp"

#include "DocumentContainer.hpp"
#include "DocumentView.hpp"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <algorithm>

ViewPickOverlay::ViewPickOverlay(
    DocumentContainer *container,
    const QList<QPointer<DocumentView>> &preselected)
    : QWidget(container), m_container(container)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);

    for (const auto &view : preselected)
        if (view)
            m_selected.insert(view);

    container->installEventFilter(this);
    setGeometry(container->rect());
    raise();
    show();
    setFocus();
}

void
ViewPickOverlay::toggle(int number) noexcept
{
    if (!m_container)
        return;

    const auto views = m_container->getAllViews();
    if (number < 1 || number > views.size())
        return;

    DocumentView *view = views.at(number - 1);
    if (!m_selected.remove(view))
        m_selected.insert(view);
    update();
}

QList<DocumentView *>
ViewPickOverlay::selected() const noexcept
{
    QList<DocumentView *> result;
    if (!m_container)
        return result;

    // in the order the views are numbered
    for (DocumentView *view : m_container->getAllViews())
        if (m_selected.contains(view))
            result << view;
    return result;
}

void
ViewPickOverlay::paintEvent(QPaintEvent *)
{
    if (!m_container)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor accent = palette().color(QPalette::Highlight);
    const QColor onAccent = palette().color(QPalette::HighlightedText);

    const auto views = m_container->getAllViews();
    for (int i = 0; i < views.size(); ++i)
    {
        DocumentView *view = views.at(i);
        if (!view->isVisible())
            continue;

        const QRect rect(view->mapTo(m_container, QPoint(0, 0)), view->size());
        const bool selected = m_selected.contains(view);

        QColor tint = selected ? accent : QColor(0, 0, 0);
        tint.setAlpha(selected ? 80 : 110);
        p.fillRect(rect, tint);

        if (selected)
        {
            p.setPen(QPen(accent, 4));
            p.setBrush(Qt::NoBrush);
            p.drawRect(rect.adjusted(2, 2, -2, -2));
        }

        const int radius
            = std::clamp(std::min(rect.width(), rect.height()) / 6, 24, 80);
        const QPoint center = rect.center();

        p.setPen(Qt::NoPen);
        p.setBrush(selected ? accent : QColor(255, 255, 255, 230));
        p.drawEllipse(center, radius, radius);

        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(radius);
        p.setFont(font);
        p.setPen(selected ? onAccent : QColor(30, 30, 30));
        p.drawText(QRect(center.x() - radius, center.y() - radius, radius * 2,
                         radius * 2),
                   Qt::AlignCenter, QString::number(i + 1));
    }

    // what to press
    QFont font = p.font();
    font.setPixelSize(14);
    font.setBold(false);
    p.setFont(font);
    const QString hint
        = tr("Press a number to select a view, Enter to sync, Esc to cancel");
    const QRect textRect = QFontMetrics(font).boundingRect(hint).adjusted(
        -12, -6, 12, 6);
    const QRect box(QPoint((width() - textRect.width()) / 2,
                           height() - textRect.height() - 12),
                    textRect.size());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(box, 6, 6);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, hint);
}

void
ViewPickOverlay::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key >= Qt::Key_1 && key <= Qt::Key_9)
        toggle(key - Qt::Key_0);
    else if (key == Qt::Key_Return || key == Qt::Key_Enter)
        finish(true);
    else if (key == Qt::Key_Escape)
        finish(false);

    // every key is taken: nothing else may react while picking
    event->accept();
}

void
ViewPickOverlay::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    // clicking a view or switching to another window ends the pick
    finish(false);
}

bool
ViewPickOverlay::event(QEvent *event)
{
    // keep key bindings from firing as shortcuts while picking
    if (event->type() == QEvent::ShortcutOverride)
    {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

bool
ViewPickOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_container && event->type() == QEvent::Resize)
        setGeometry(m_container->rect());
    return QWidget::eventFilter(watched, event);
}

void
ViewPickOverlay::finish(bool accept) noexcept
{
    if (m_done)
        return;
    m_done = true;

    const QList<DocumentView *> views = selected();
    if (m_container)
        m_container->removeEventFilter(this);
    hide();

    if (accept)
        emit accepted(views);
    else
        emit cancelled();
    deleteLater();
}
