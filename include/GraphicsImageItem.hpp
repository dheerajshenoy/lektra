#pragma once

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
        updateBoundingRect();
        update();
    }

    // Set image (move - more efficient)
    void setImage(QImage &&image) noexcept
    {
        prepareGeometryChange();
        m_image = std::move(image);
        updateBoundingRect();
        update();
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

    // Returns pixel width (not logical width)
    [[nodiscard]] inline int width() const noexcept
    {
        return m_image.isNull() ? 0 : m_image.width();
    }

    // Returns pixel height (not logical height)
    [[nodiscard]] inline int height() const noexcept
    {
        return m_image.isNull() ? 0 : m_image.height();
    }

    [[nodiscard]] QRectF boundingRect() const override
    {
        return m_bounding_rect;
    }

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

        // Draw only the exposed region (scissored by clip rect), while still
        // using the full image mapping for correct DPR and transforms.
        painter->drawImage(m_bounding_rect, m_image);

        painter->restore();
    }

    // Inside GraphicsImageItem or a subclass
    void setPageNumber(int pageno)
    {
        if (!m_label)
        {
            // 'this' is the parent. Child positions are relative to (0,0) of
            // the image.
            m_label = new QGraphicsSimpleTextItem(this);
        }
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
            m_bounding_rect
                = QRectF(0, 0, m_image.width() / dpr, m_image.height() / dpr);
        }
    }

    QGraphicsSimpleTextItem *m_label = nullptr; // For page number labels
    QImage m_image;
    QRectF m_bounding_rect;
};
