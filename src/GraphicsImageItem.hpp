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
        // setCacheMode(QGraphicsItem::NoCache);
        // setCacheMode(QGraphicsItem::ItemCoordinateCache);
    }

    // Set image (copy)
    void setImage(const QImage &image)
    {
        prepareGeometryChange();
        m_image = image;
        updateBoundingRect();
        update();
    }

    // Set image (move - more efficient)
    void setImage(QImage &&image)
    {
        prepareGeometryChange();
        m_image = std::move(image);
        updateBoundingRect();
        update();
    }

    // API compatibility with QGraphicsPixmapItem
    inline const QImage &image() const noexcept
    {
        return m_image;
    }

    inline bool isNull() const noexcept
    {
        return m_image.isNull();
    }

    inline qreal devicePixelRatio() const noexcept
    {
        return m_image.isNull() ? 1.0 : m_image.devicePixelRatio();
    }

    // Returns pixel width (not logical width)
    inline int width() const noexcept
    {
        return m_image.isNull() ? 0 : m_image.width();
    }

    // Returns pixel height (not logical height)
    inline int height() const { return m_image.isNull() ? 0 : m_image.height(); }

    QRectF boundingRect() const override
    {
        return m_bounding_rect;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        if (m_image.isNull())
            return;

        // Draw the image scaled to its logical size (accounting for DPR)
        // painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        // painter->setRenderHint(QPainter::Antialiasing, true);
        painter->drawImage(m_bounding_rect, m_image);
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

    QImage m_image;
    QRectF m_bounding_rect;
};
