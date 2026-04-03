#pragma once

#include <QGraphicsPixmapItem>
#include <QImage>
#include <QPixmap>

class GraphicsPixmapItem : public QGraphicsPixmapItem
{
public:
    GraphicsPixmapItem(QGraphicsItem *parent = nullptr)
        : QGraphicsPixmapItem(parent)
    {
        // NoCache is appropriate for OpenGL backend as the pixmap
        // is already a GPU texture
        setCacheMode(QGraphicsItem::NoCache);
    }
};
