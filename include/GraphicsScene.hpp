#pragma once

#include <QGraphicsScene>
#include <QGraphicsView>

class GraphicsScene : public QGraphicsScene
{
    Q_OBJECT
public:
    GraphicsScene(QObject *parent = nullptr);
};
