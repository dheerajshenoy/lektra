#pragma once
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <vector>

class ScrollBar : public QScrollBar
{
    Q_OBJECT
public:
    explicit ScrollBar(Qt::Orientation o, QWidget *parent = nullptr) noexcept;
    void setSize(int size) noexcept;

    void setSearchMarkers(std::vector<double> markers) noexcept;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_size = 12;
    std::vector<double> m_markers;
};
