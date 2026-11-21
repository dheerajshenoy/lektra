#pragma once

#include <QColor>
#include <QPainter>
#include <QRectF>

// The soft shadow under a page. It is drawn from one small, already blurred
// picture of the shadow's corner and edge, cut in nine pieces: the corners are
// drawn as they are, the sides stretch a thin strip, and the middle is left
// out because the page covers it. So the cost does not depend on how big the
// page is, however far it is zoomed in.
namespace pageshadow
{
// Draws the shadow of `page` with `painter`, which must be set up in viewport
// coordinates (no scale). `visible` is the part of the viewport that can be
// seen; the stretched pieces are cut to it so a huge page never becomes a huge
// draw. `radius` is how far the shadow reaches, `offsetY` how far it is dropped
// below the page (both in viewport pixels), `color` the shadow's color; its
// alpha is the strength at the edge of the page. `dpr` is the device pixel
// ratio of the viewport.
void draw(QPainter *painter, const QRectF &page, const QRectF &visible,
          int radius, int offsetY, const QColor &color, qreal dpr);
} // namespace pageshadow
