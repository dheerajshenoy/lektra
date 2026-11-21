#include "PageShadow.hpp"

#include <QImage>
#include <QPixmap>
#include <cmath>

namespace
{
// How much of the shadow is left at `d` pixels from the page edge (negative
// outside the page): a blurred straight edge, 0.5 at the edge itself.
float
edgeProfile(float d, float sigma) noexcept
{
    return 0.5f * (1.0f + std::erf(d / (sigma * 1.41421356f)));
}

// The shadow of a page corner: `r` pixels of fall-off on each side of a 2 pixel
// strip that stands for the straight part (r + 2 + r pixels wide and high).
QPixmap
makeTexture(int r, const QColor &color)
{
    const int size    = 2 * r + 2;
    const float sigma = std::max(1.0f, r / 3.0f);

    // Along one axis: the profile of each pixel. The strip in the middle is
    // inside the page on both sides, so it is fully shadowed.
    std::vector<float> profile(size);
    for (int i = 0; i < size; ++i)
    {
        if (i < r)
            profile[i] = edgeProfile(i + 0.5f - r, sigma);
        else if (i < r + 2)
            profile[i] = 1.0f;
        else
            profile[i] = edgeProfile(r + 2 - (i + 0.5f), sigma);
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
            if (i == 1 && j == 1)
                continue; // the page covers it

            const QRectF src(us[i], us[j], uw[i], uw[j]);
            QRectF dst(xs[i], ys[j], ws[i], hs[j]);

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
