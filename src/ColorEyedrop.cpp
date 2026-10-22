#include "ColorEyedrop.hpp"

#include "GraphicsView.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

static constexpr int DIAMETER = 84;
static constexpr int OFFSET   = 18;

ColorEyedrop::ColorEyedrop(QWidget *parent)
    : QWidget(parent)
{
    // A child widget, not a window: Wayland ignores move() on top-level
    // windows, which left the circle away from the pointer.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(DIAMETER, DIAMETER);
    hide();
}

void
ColorEyedrop::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active)
    {
        qApp->installEventFilter(this);
        QGuiApplication::setOverrideCursor(Qt::CrossCursor);
        m_color = QColor();
        // Show once the pointer is over a view and the color is known.
    }
    else
    {
        qApp->removeEventFilter(this);
        QGuiApplication::restoreOverrideCursor();
        hide();
    }
}

QColor
ColorEyedrop::sample(QWidget *viewport, const QPoint &globalPos) const
{
    // Grab from the view, not the screen: the result is what the page looks
    // like (zoom, filters) and never contains this window.
    const QPoint p = viewport->mapFromGlobal(globalPos);
    if (!viewport->rect().contains(p))
        return {};
    return viewport->grab(QRect(p, QSize(1, 1))).toImage().pixelColor(0, 0);
}

void
ColorEyedrop::follow(const QPoint &globalPos)
{
    const QWidget *host = parentWidget();
    if (!host)
        return;

    const QPoint local = host->mapFromGlobal(globalPos);
    QPoint pos         = local + QPoint(OFFSET, OFFSET);
    const QRect r      = host->rect();
    if (pos.x() + width() > r.right())
        pos.setX(local.x() - OFFSET - width());
    if (pos.y() + height() > r.bottom())
        pos.setY(local.y() - OFFSET - height());
    move(pos);
    raise();
    if (!isVisible())
        show();
    update();
}

bool
ColorEyedrop::eventFilter(QObject *obj, QEvent *event)
{
    const QEvent::Type type = event->type();

    // Escape is swallowed on press and release, and cancels on release, so
    // nothing else reacts to the same key.
    if (type == QEvent::KeyPress || type == QEvent::KeyRelease)
    {
        if (static_cast<QKeyEvent *>(event)->key() != Qt::Key_Escape)
            return false;
        if (type == QEvent::KeyRelease)
            setActive(false);
        return true;
    }

    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove
        && type != QEvent::MouseButtonRelease
        && type != QEvent::MouseButtonDblClick)
        return false;

    // Only the viewport of a document view; menus, tabs etc. stay usable.
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || !qobject_cast<GraphicsView *>(w->parentWidget())
        || !parentWidget() || !parentWidget()->isAncestorOf(w))
        return false;

    auto *me               = static_cast<QMouseEvent *>(event);
    const QPoint globalPos = me->globalPosition().toPoint();

    if (type == QEvent::MouseMove)
    {
        const QColor c = sample(w, globalPos);
        if (c.isValid())
        {
            m_color = c;
            follow(globalPos);
        }
        return me->buttons() & Qt::LeftButton;
    }

    if (me->button() != Qt::LeftButton)
        return false;

    if (type == QEvent::MouseButtonRelease)
    {
        const QColor c = sample(w, globalPos);
        setActive(false);
        if (c.isValid())
            emit picked(c);
    }
    return true;
}

void
ColorEyedrop::paintEvent(QPaintEvent *)
{
    if (!m_color.isValid())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);

    p.setPen(QPen(QColor(0, 0, 0, 160), 2));
    p.setBrush(m_color);
    p.drawEllipse(r);

    // Text readable on any color.
    p.setPen(m_color.lightness() > 128 ? Qt::black : Qt::white);
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    f.setBold(true);
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter, m_color.name().toUpper());
}
