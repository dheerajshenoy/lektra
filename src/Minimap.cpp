#include "Minimap.hpp"

#include "GraphicsImageItem.hpp"
#include "utils.hpp"

#include <QAbstractScrollArea>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

Minimap::Minimap(const Config &config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_NoMousePropagation);
    hide();
}

void
Minimap::rebuildThumbnail(const GraphicsImageItem *page,
                          const QImage &whole) noexcept
{
    const bool use_whole = !whole.isNull();
    const QImage &src    = use_whole ? whole : page->image();
    const bool partial   = !use_whole && page->isPartial();
    const QRectF page_rect = page->boundingRect();
    const int longest      = qMax(32, m_config.minimap.size);
    const qreal scale
        = longest / qMax(page_rect.width(), page_rect.height());
    const QSize logical(qMax(1, qRound(page_rect.width() * scale)),
                        qMax(1, qRound(page_rect.height() * scale)));
    const qreal dpr = devicePixelRatioF();

    QImage thumb(logical * dpr, QImage::Format_ARGB32_Premultiplied);
    thumb.setDevicePixelRatio(dpr);
    if (!m_config.minimap.render_page)
    {
        thumb.fill(palette().window().color());
        m_thumb = std::move(thumb);
        setFixedSize(logical);
        return;
    }
    thumb.fill(partial && !src.isNull() ? QColor(src.pixel(0, 0))
                                                  : QColor(Qt::white));

    // A smooth filter over a zoomed-in page (tens of megapixels) is slow, and
    // this runs on the UI thread at every re-render or animation frame. Shrink
    // it with the cheap nearest filter to twice the final size first; the
    // smooth pass then only has a small image to work on.
    auto shrunk = [dpr](const QImage &img, const QSizeF &logicalSize)
    {
        const QSize want = (logicalSize * dpr * 2).toSize().expandedTo({1, 1});
        if (img.width() <= want.width() && img.height() <= want.height())
            return img;
        QImage out = img.scaled(want, Qt::IgnoreAspectRatio,
                                Qt::FastTransformation);
        out.setDevicePixelRatio(img.devicePixelRatio());
        return out;
    };

    QPainter p(&thumb);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF target(QPointF(0, 0), QSizeF(logical));
    if (partial)
    {
        const QRectF region = page->imageRect();
        const QRectF dest(region.x() * scale, region.y() * scale,
                          region.width() * scale, region.height() * scale);
        p.drawImage(dest, shrunk(src, dest.size()));
    }
    else
        p.drawImage(target, shrunk(src, target.size()));
    p.end();

    m_thumb = std::move(thumb);
    setFixedSize(logical);
}

int
Minimap::optionsKey() const noexcept
{
    return m_config.minimap.size * 2 + (m_config.minimap.render_page ? 1 : 0);
}

void
Minimap::setPage(const GraphicsImageItem *page, int pageno,
                 const QRectF &visible, const QImage &whole,
                 qint64 refreshKey) noexcept
{
    const qint64 image_key = whole.isNull() ? refreshKey : whole.cacheKey();
    if (pageno != m_page_key || image_key != m_image_key
        || optionsKey() != m_size_key || m_thumb.isNull())
    {
        m_page_key  = pageno;
        m_image_key = image_key;
        m_size_key  = optionsKey();
        rebuildThumbnail(page, whole);
        reposition();
    }
    m_visible = visible;
    update();
}

void
Minimap::setVisibleRect(const QRectF &visible) noexcept
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    update();
}

void
Minimap::reposition() noexcept
{
    // The parent is the view, not its viewport: Qt scrolls the children of
    // the viewport along with the content, and this has to stay put.
    QWidget *p = parentWidget();
    if (!p)
        return;
    const QRect area = qobject_cast<QAbstractScrollArea *>(p)
                           ? static_cast<QAbstractScrollArea *>(p)
                                 ->viewport()
                                 ->geometry()
                           : p->rect();
    using MP         = Config::Minimap::Position;
    const int margin = qMax(0, m_config.minimap.margin);
    const bool left  = m_config.minimap.position == MP::TopLeft
                       || m_config.minimap.position == MP::BottomLeft;
    const bool top = m_config.minimap.position == MP::TopLeft
                     || m_config.minimap.position == MP::TopRight;
    move(left ? area.left() + margin : area.right() + 1 - width() - margin,
         top ? area.top() + margin : area.bottom() + 1 - height() - margin);
}

void
Minimap::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setOpacity(qBound(0.0, double(m_config.minimap.opacity), 1.0));
    p.drawImage(rect(), m_thumb);

    const QRectF v(m_visible.x() * width(), m_visible.y() * height(),
                   m_visible.width() * width(), m_visible.height() * height());

    // Dim what is out of view, then outline what is in it.
    QPainterPath outside;
    outside.addRect(rect());
    QPainterPath inside;
    inside.addRect(v);
    p.fillPath(outside.subtracted(inside),
               rgbaToQColor(m_config.minimap.viewport.dim_color));
    p.fillRect(v, rgbaToQColor(m_config.minimap.viewport.fill));
    const qreal lw = m_config.minimap.viewport.width;
    if (lw > 0)
    {
        p.setPen(QPen(rgbaToQColor(m_config.minimap.viewport.color), lw));
        p.drawRect(v.adjusted(lw / 2, lw / 2, -lw / 2, -lw / 2));
    }

    const int bw = m_config.minimap.border_width;
    if (bw > 0)
    {
        p.setOpacity(1.0);
        p.setPen(QPen(rgbaToQColor(m_config.minimap.border_color), bw,
                      Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        const qreal half = bw / 2.0;
        p.drawRect(QRectF(rect()).adjusted(half, half, -half, -half));
    }
}

void
Minimap::requestMove(const QPoint &pos) noexcept
{
    const QPointF norm(qBound(0.0, double(pos.x()) / width(), 1.0),
                       qBound(0.0, double(pos.y()) / height(), 1.0));

    // Move the marker right away, centered on the pointer, instead of waiting
    // for the view to scroll and report back (which may lag behind the mouse).
    // That report then settles it where the view really ended up.
    if (m_visible.isValid() && !m_visible.isEmpty())
    {
        m_visible.moveCenter(norm);
        m_visible.moveLeft(qBound(0.0, m_visible.left(), 1.0 - m_visible.width()));
        m_visible.moveTop(qBound(0.0, m_visible.top(), 1.0 - m_visible.height()));
        update();
    }

    emit moveRequested(norm);
}

void
Minimap::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit dragStarted();
        requestMove(event->position().toPoint());
    }
    event->accept();
}

void
Minimap::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        requestMove(event->position().toPoint());
    event->accept();
}

void
Minimap::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit dragFinished();
    event->accept();
}
