#include "ImageEncode.hpp"

#include <QBuffer>
#include <QPainter>

namespace
{
QImage
scaledDown(const QImage &image, int maxSide)
{
    if (image.width() <= maxSide && image.height() <= maxSide)
        return image;
    return image.scaled(maxSide, maxSide, Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
}
} // namespace

QString
encodeImageForModel(const QImage &image, int maxSide, int maxBytes)
{
    if (image.isNull())
        return {};

    QImage img = scaledDown(image, maxSide);
    img.setDevicePixelRatio(1.0); // the model sees pixels, not logical sizes

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    QString mime = QStringLiteral("image/png");

    if (bytes.size() > maxBytes)
    {
        // JPEG has no alpha: flatten onto white first.
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, img);
        p.end();

        QByteArray jpeg;
        QBuffer jb(&jpeg);
        jb.open(QIODevice::WriteOnly);
        flat.save(&jb, "JPEG", 85);
        if (!jpeg.isEmpty() && jpeg.size() < bytes.size())
        {
            bytes = jpeg;
            mime  = QStringLiteral("image/jpeg");
        }
    }

    return QStringLiteral("data:%1;base64,%2")
        .arg(mime, QString::fromLatin1(bytes.toBase64()));
}

QImage
thumbnailFor(const QImage &image, int maxSide)
{
    if (image.isNull())
        return {};
    QImage t = image;
    t.setDevicePixelRatio(1.0);
    return scaledDown(t, maxSide);
}
