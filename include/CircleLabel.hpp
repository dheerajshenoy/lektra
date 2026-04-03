#pragma once

#include <QLabel>
#include <QPainter>

// A QLabel that displays a colored circle and emits a clicked signal when
// pressed
class CircleLabel : public QLabel
{
    Q_OBJECT
public:
    CircleLabel(QWidget *parent = nullptr) noexcept : QLabel(parent)
    {
        setFixedSize(24, 24); // or any square size
        setContentsMargins(0, 0, 0, 0);
    }

    void setColor(const QColor &color) noexcept
    {
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(m_color);
        p.drawEllipse(rect().adjusted(1, 1, -1, -1)); // Avoid clipping
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        emit clicked();
        QLabel::mousePressEvent(event);
    }

signals:
    void clicked();

private:
    QColor m_color;
};
