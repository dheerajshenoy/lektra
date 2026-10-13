#include "LaserTrail.hpp"

#include "GraphicsView.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

LaserTrail::LaserTrail(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    m_clock.start();
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this]
    {
        const qint64 now = m_clock.elapsed();
        while (!m_samples.isEmpty() && now - m_samples.first().t > m_fade_ms)
            m_samples.pop_front();
        if (m_samples.isEmpty() && !m_down)
            m_timer.stop();
        update();
    });
    hide();
}

void
LaserTrail::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active)
    {
        qApp->installEventFilter(this);
        if (QWidget *p = parentWidget())
            setGeometry(p->rect());
        raise();
        show();
    }
    else
    {
        qApp->removeEventFilter(this);
        m_timer.stop();
        m_samples.clear();
        m_down = false;
        hide();
    }
}

void
LaserTrail::setStyle(const QColor &color, int width, int fade_ms, bool trail)
{
    m_trail   = trail;
    m_color   = color;
    m_width   = qMax(1, width);
    m_fade_ms = qMax(50, fade_ms);
}

bool
LaserTrail::eventFilter(QObject *obj, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove
        && type != QEvent::MouseButtonRelease
        && type != QEvent::MouseButtonDblClick)
        return false;

    // Only the viewport of a document view; menus, tabs etc. stay usable.
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || !qobject_cast<GraphicsView *>(w->parentWidget())
        || !parentWidget() || !parentWidget()->isAncestorOf(w))
        return false;

    auto *me = static_cast<QMouseEvent *>(event);
    bool swallow = false;

    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick)
    {
        if (me->button() == Qt::LeftButton)
        {
            m_down  = true;
            swallow = true;
        }
    }
    else if (type == QEvent::MouseButtonRelease)
    {
        if (me->button() == Qt::LeftButton)
        {
            m_down  = false;
            swallow = true;
        }
    }
    else if (me->buttons() & Qt::LeftButton)
        swallow = true;

    if (m_trail && m_down && swallow && type != QEvent::MouseButtonRelease)
    {
        setGeometry(parentWidget()->rect());
        const QPoint p = mapFromGlobal(me->globalPosition().toPoint());
        m_samples.push_back(
            {p, m_clock.elapsed(), type == QEvent::MouseButtonPress});
        if (!m_timer.isActive())
            m_timer.start();
    }
    return swallow;
}

void
LaserTrail::paintEvent(QPaintEvent *)
{
    if (m_samples.size() < 1)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qint64 now = m_clock.elapsed();

    for (int i = 0; i < m_samples.size(); ++i)
    {
        const Sample &s = m_samples[i];
        const double life
            = 1.0 - double(now - s.t) / double(m_fade_ms); // 1 = fresh
        if (life <= 0)
            continue;
        QColor c = m_color;
        c.setAlphaF(c.alphaF() * life);
        QPen pen(c, m_width * (0.3 + 0.7 * life), Qt::SolidLine, Qt::RoundCap,
                 Qt::RoundJoin);
        p.setPen(pen);
        if (s.start || i == 0)
            p.drawPoint(s.pos);
        else
            p.drawLine(m_samples[i - 1].pos, s.pos);
    }
}
