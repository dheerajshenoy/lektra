#pragma once

#include <QColor>
#include <QGraphicsItem>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

// A memory-efficient QGraphicsItem that renders QImage directly,
// avoiding the QImage -> QPixmap conversion overhead.
//
// QPixmap::fromImage() is expensive because it:
// 1. Allocates new memory for the pixmap
// 2. Copies and potentially converts pixel data
// 3. May upload to GPU memory (platform-dependent)
//
// By using QImage directly, we save memory and CPU cycles.
// This class provides API compatibility with QGraphicsPixmapItem
// for easy migration.
class GraphicsImageItem : public QGraphicsItem
{
public:
    GraphicsImageItem(QGraphicsItem *parent = nullptr) : QGraphicsItem(parent)
    {
        // Note: When using QOpenGLWidget viewport, DeviceCoordinateCache can
        // cause rendering issues. Use NoCache for OpenGL compatibility.
        // If not using OpenGL, DeviceCoordinateCache provides better scroll
        // performance.
        setCacheMode(QGraphicsItem::NoCache);
        // setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    }

    // Set image (copy)
    void setImage(const QImage &image) noexcept
    {
        prepareGeometryChange();
        m_image = image;
        m_full_px = {};
        m_region_px = {};
        updateBoundingRect();
        update();
    }

    // Set image (move - more efficient)
    void setImage(QImage &&image) noexcept
    {
        prepareGeometryChange();
        m_image = std::move(image);
        m_full_px = {};
        m_region_px = {};
        updateBoundingRect();
        update();
    }

    // Set an image that only covers `regionPx` (device pixels) of a page whose
    // full rendered size would be `fullPx`. The item keeps the geometry of the
    // whole page, so layout, links and selection behave as for a full render,
    // but only the region's pixels exist in memory.
    void setPartialImage(QImage &&image, const QSize &fullPx,
                         const QRect &regionPx) noexcept
    {
        prepareGeometryChange();
        m_image     = std::move(image);
        m_full_px   = fullPx;
        m_region_px = regionPx;
        m_fill      = m_image.isNull() ? QColor(Qt::white)
                                       : QColor(m_image.pixel(0, 0));
        updateBoundingRect();
        update();
    }

    [[nodiscard]] inline bool isPartial() const noexcept
    {
        return m_full_px.isValid();
    }

    // Area (item coordinates) that actually has pixels.
    [[nodiscard]] QRectF imageRect() const noexcept
    {
        if (!isPartial())
            return m_bounding_rect;
        const qreal dpr = m_image.devicePixelRatio();
        return QRectF(m_region_px.x() / dpr, m_region_px.y() / dpr,
                      m_region_px.width() / dpr, m_region_px.height() / dpr);
    }

    // Copy of `fullPx` (device pixels of the whole page); parts that are not
    // resident come out blank.
    [[nodiscard]] QImage imageRegion(const QRect &fullPx) const
    {
        if (!isPartial())
            return m_image.copy(fullPx);
        QImage out(fullPx.size(), m_image.format());
        out.setDevicePixelRatio(m_image.devicePixelRatio());
        out.fill(m_fill);
        const QRect common = fullPx.intersected(m_region_px);
        if (!common.isEmpty())
        {
            QPainter p(&out);
            p.drawImage(common.topLeft() - fullPx.topLeft(), m_image,
                        common.translated(-m_region_px.topLeft()));
        }
        return out;
    }

    // API compatibility with QGraphicsPixmapItem
    [[nodiscard]] inline const QImage &image() const noexcept
    {
        return m_image;
    }

    [[nodiscard]] inline bool isNull() const noexcept
    {
        return m_image.isNull();
    }

    [[nodiscard]] inline qreal devicePixelRatio() const noexcept
    {
        return m_image.isNull() ? 1.0 : m_image.devicePixelRatioF();
    }

    // Returns pixel width (not logical width) of the whole page, even if only
    // part of it is resident.
    [[nodiscard]] inline int width() const noexcept
    {
        return m_image.isNull() ? 0
                                : (isPartial() ? m_full_px.width()
                                               : m_image.width());
    }

    // Returns pixel height (not logical height) of the whole page.
    [[nodiscard]] inline int height() const noexcept
    {
        return m_image.isNull() ? 0
                                : (isPartial() ? m_full_px.height()
                                               : m_image.height());
    }

    [[nodiscard]] QRectF boundingRect() const override
    {
        return m_bounding_rect;
    }

    void setHighlighted(bool highlighted) noexcept
    {
        if (m_highlighted == highlighted)
            return;
        m_highlighted = highlighted;
        update();
    }

    [[nodiscard]] bool isHighlighted() const noexcept { return m_highlighted; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override
    {
        Q_UNUSED(widget);

        if (m_image.isNull())
            return;

        const QRectF exposed
            = option ? option->exposedRect.intersected(m_bounding_rect)
                     : m_bounding_rect;
        if (exposed.isEmpty())
            return;

        painter->save();
        painter->setClipRect(exposed);
        if (isPartial())
        {
            painter->fillRect(exposed, m_fill);
            painter->drawImage(imageRect(), m_image);
        }
        else
            painter->drawImage(m_bounding_rect, m_image);
        painter->restore();

        if (m_highlighted)
        {
            painter->save();
            QPen pen(QColor(70, 130, 255, 220), 3.0);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(m_bounding_rect.adjusted(1.5, 1.5, -1.5, -1.5));
            painter->restore();
        }
    }

    // Inside GraphicsImageItem or a subclass
    void setPageNumber(int pageno, int fontSize = 10)
    {
        if (!m_label)
            m_label = new QGraphicsSimpleTextItem(this);

        QFont f = m_label->font();
        f.setPointSize(fontSize);
        m_label->setFont(f);
        m_label->setText(QString::number(pageno + 1));

        // Position it once. Because it's a child, it stays here
        // even if the parent GraphicsImageItem is moved via setPos()
        updateLabelPosition();
    }

    void updateLabelPosition()
    {
        if (m_label)
        {
            const QRectF br = this->boundingRect();
            const QRectF lr = m_label->boundingRect();
            // Center horizontally, 5px below the image
            m_label->setPos((br.width() - lr.width()) / 2.0, br.height() + 5.0);
        }
    }

private:
    void updateBoundingRect()
    {
        if (m_image.isNull())
        {
            m_bounding_rect = QRectF();
        }
        else
        {
            // Logical size = pixel size / device pixel ratio
            const qreal dpr = m_image.devicePixelRatio();
            const QSize px  = isPartial() ? m_full_px : m_image.size();
            m_bounding_rect = QRectF(0, 0, px.width() / dpr, px.height() / dpr);
        }
    }

    QGraphicsSimpleTextItem *m_label = nullptr;
    QImage m_image;
    QSize m_full_px;   // valid only for partial images
    QRect m_region_px; // where m_image sits inside the full page, in pixels
    QColor m_fill = Qt::white;
    QRectF m_bounding_rect;
    bool m_highlighted = false;
};
