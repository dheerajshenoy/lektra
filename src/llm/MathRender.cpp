#include "MathRender.hpp"

#include "MicroTeXQt.hpp"
#include "latex.h"

#include <QCache>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QPainter>
#include <QStandardPaths>
#include <memory>

namespace
{
// Bump when the embedded MicroTeX resources change, so an old unpacked copy
// is replaced.
constexpr const char *kResourceVersion = "1";

// MicroTeX reads its fonts and symbol tables from a folder, so the copy that
// is embedded in the binary is unpacked into the cache folder once.
bool
unpackResources(const QString &root)
{
    const QString stamp = root + QStringLiteral("/.lektra-version");
    QFile stampFile(stamp);
    if (stampFile.open(QIODevice::ReadOnly)
        && stampFile.readAll().trimmed() == kResourceVersion
        && QFile::exists(root + QStringLiteral("/res/.clatexmath-res_root")))
        return true;
    stampFile.close();

    QDir(root).removeRecursively();
    const QString from = QStringLiteral(":/microtex/");
    QDirIterator it(from + QStringLiteral("res"), QDir::Files | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString source = it.next();
        const QString target
            = root + QLatin1Char('/') + source.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(source, target))
            return false;
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    }
    if (!stampFile.open(QIODevice::WriteOnly))
        return false;
    stampFile.write(kResourceVersion);
    return true;
}

bool
ensureInitialised()
{
    static bool tried = false;
    static bool ok    = false;
    if (tried)
        return ok;
    tried = true;

    const QString root
        = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
          + QStringLiteral("/microtex");
    if (!unpackResources(root))
        return false;
    try
    {
        tex::LaTeX::init((root + QStringLiteral("/res")).toStdString());
        ok = true;
    }
    catch (...)
    {
    }
    return ok;
}
} // namespace

QImage
renderMath(const QString &latex, int pixelSize, const QColor &color,
           int maxWidth, qreal dpr)
{
    static QCache<QString, QImage> cache(300);
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(latex)
                            .arg(pixelSize)
                            .arg(color.rgba(), 0, 16)
                            .arg(maxWidth)
                            .arg(dpr);
    if (const QImage *hit = cache.object(key))
        return *hit;
    if (latex.trimmed().isEmpty() || !ensureInitialised())
        return {};

    QImage image;
    try
    {
        std::unique_ptr<tex::TeXRender> render(tex::LaTeX::parse(
            latex.toStdWString(), maxWidth, static_cast<float>(pixelSize),
            pixelSize / 3.f, color.rgba()));
        constexpr int pad = 2;
        const int width   = render->getWidth() + 2 * pad;
        const int height  = render->getHeight() + render->getDepth() + 2 * pad;
        if (width > 2 * pad && height > 2 * pad && width < 4000
            && height < 4000)
        {
            image = QImage(QSize(width, height) * dpr,
                           QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            tex::Graphics2D_qt g2(&painter);
            render->draw(g2, pad, pad);
        }
    }
    catch (...)
    {
        image = QImage(); // not valid LaTeX: the caller shows the source
    }

    cache.insert(key, new QImage(image));
    return image;
}
