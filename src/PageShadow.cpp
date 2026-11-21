#include "PageShadow.hpp"

#include <QImage>
#include <QPixmap>
#include <cmath>

namespace
{
// How strong the shadow is `k` pixels in from its outer edge, for a fall-off
// `r` pixels wide: a blurred edge that is 0 at the outer edge and 1 at the
// page. The whole fall-off is kept, so the corner pieces are exact.
float
fallOff(float k, int r) noexcept
{
    const float sigma = std::max(0.75f, r / 4.0f);
    return 0.5f * (1.0f + std::erf((k - r / 2.0f) / (sigma * 1.41421356f)));
}

// The shadow of a page corner: `r` pixels of fall-off on each side of a 2 pixel
// strip that stands for the straight part (r + 2 + r pixels wide and high).
QPixmap
makeTexture(int r, const QColor &color)
{
    const int size = 2 * r + 2;

    // Along one axis: the strength at each pixel. The strip in the middle is
    // fully shadowed.
    std::vector<float> profile(size);
    for (int i = 0; i < size; ++i)
    {
        if (i < r)
            profile[i] = fallOff(i + 0.5f, r);
        else if (i < r + 2)
            profile[i] = 1.0f;
        else
            profile[i] = fallOff(size - i - 0.5f, r);
    }

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    const float strength = static_cast<float>(color.alphaF());
    for (int y = 0; y < size; ++y)
    {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size; ++x)
        {
            const float a = profile[x] * profile[y] * strength;
            line[x]       = qRgba(static_cast<int>(color.red() * a + 0.5f),
                                  static_cast<int>(color.green() * a + 0.5f),
                                  static_cast<int>(color.blue() * a + 0.5f),
                                  static_cast<int>(255.0f * a + 0.5f));
        }
    }
    return QPixmap::fromImage(image);
}
} // namespace

void
pageshadow::draw(QPainter *painter, const QRectF &page, const QRectF &visible,
                 int radius, int offsetY, const QColor &color, qreal dpr)
{
    if (radius <= 0 || color.alpha() == 0 || page.width() <= 0
        || page.height() <= 0)
        return;

    // One picture is kept; it is made again only when its look changes.
    static QPixmap texture;
    static int textureRadius = -1;
    static QRgb textureColor = 0;

    // The picture is in device pixels so it lands on the screen 1:1.
    const int rd = std::max(1, static_cast<int>(std::lround(radius * dpr)));
    if (texture.isNull() || textureRadius != rd
        || textureColor != color.rgba())
    {
        texture       = makeTexture(rd, color);
        textureRadius = rd;
        textureColor  = color.rgba();
    }

    const qreal r  = radius;
    const qreal x  = page.x();
    const qreal y  = page.y() + offsetY;
    const qreal w  = page.width();
    const qreal h  = page.height();
    const qreal xs[3] = {x - r, x, x + w};
    const qreal ws[3] = {r, w, r};
    const qreal ys[3] = {y - r, y, y + h};
    const qreal hs[3] = {r, h, r};
    const qreal us[3] = {0, qreal(rd), qreal(rd + 2)};
    const qreal uw[3] = {qreal(rd), 2, qreal(rd)};

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            const QRectF src(us[i], us[j], uw[i], uw[j]);
            QRectF dst(xs[i], ys[j], ws[i], hs[j]);

            // The middle is behind the page, except the strip that sticks out
            // above or below it when the shadow is dropped.
            if (i == 1 && j == 1)
            {
                if (offsetY > 0)
                    dst = QRectF(x, page.y() + h, w, offsetY);
                else if (offsetY < 0)
                    dst = QRectF(x, y, w, -offsetY);
                else
                    continue;
            }

            // The stretched pieces are the same all along, so cutting them to
            // what is visible changes nothing.
            if (i == 1 || j == 1)
            {
                const QRectF vis = dst.intersected(visible);
                if (vis.isEmpty())
                    continue;
                if (i == 1)
                {
                    dst.setX(vis.x());
                    dst.setWidth(vis.width());
                }
                if (j == 1)
                {
                    dst.setY(vis.y());
                    dst.setHeight(vis.height());
                }
            }
            else if (!dst.intersects(visible))
                continue;

            painter->drawPixmap(dst, texture, src);
        }
    }
    painter->restore();
}
