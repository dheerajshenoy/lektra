#pragma once

#include "Config.hpp"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

class GraphicsImageItem;

// A small overview of one page, drawn over a corner of the view, with the part
// of the page that is visible marked. Click or drag in it to move there.
class Minimap : public QWidget
{
    Q_OBJECT

public:
    explicit Minimap(const Config &config, QWidget *parent = nullptr);

    // Show `page` (its thumbnail is only rebuilt when the page or its pixels
    // changed) with `visible` marked, in 0..1 of the page.
    // `whole` (optional) is the entire page, for when `page` only holds the part
    // of it near the view.
    // The thumbnail is built once per (`pageno`, `whole`, `refreshKey`, size)
    // and otherwise left alone; `refreshKey` changes when `page`'s pixels
    // should be taken again (an animation frame).
    void setPage(const GraphicsImageItem *page, int pageno,
                 const QRectF &visible, const QImage &whole = {},
                 qint64 refreshKey = 0) noexcept;

    // Move the marker only; the thumbnail is left as it is.
    void setVisibleRect(const QRectF &visible) noexcept;

    // Put it in the configured corner of the parent.
    void reposition() noexcept;

signals:
    // A position of the page (0..1 in both axes) to move the view to.
    void moveRequested(QPointF normalizedPos);
    // The mouse button went down / up in the minimap.
    void dragStarted();
    void dragFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void rebuildThumbnail(const GraphicsImageItem *page,
                          const QImage &whole) noexcept;
    void requestMove(const QPoint &pos) noexcept;

    const Config &m_config;
    QImage m_thumb;
    QRectF m_visible;
    int m_page_key         = -1;
    qint64 m_image_key     = 0;
    int m_size_key         = 0;
};
