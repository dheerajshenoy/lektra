#pragma once

#include <QColor>
#include <QList>
#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>
#include <QWidget>

// Transparent overlay that draws a fading trail while the left mouse button
// is held, like Excalidraw's laser pointer. The oldest part of the stroke
// fades first. It never takes mouse events itself: it watches the
// application's events instead.
class LaserTrail : public QWidget
{
public:
    explicit LaserTrail(QWidget *parent);

    void setActive(bool active);
    void setStyle(const QColor &color, int width, int fade_ms);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample
    {
        QPoint pos;
        qint64 t;
        bool start; // first point of a stroke (no segment to the previous one)
    };

    QList<Sample> m_samples;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QColor m_color{Qt::red};
    int m_width   = 6;
    int m_fade_ms = 1000;
    bool m_down   = false;
    bool m_active = false;
};
