#include "AppPaths.hpp"

#include "TranslationsDir.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace
{

QString
bundleResourcesPath() noexcept
{
#if defined(__APPLE__) && defined(__MACH__)
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = QDir::cleanPath(appDir.filePath("../Resources"));

    if (QDir(candidate).exists())
        return candidate;
#endif

    return {};
}

}

QString
appTranslationsPath() noexcept
{
#if defined(__APPLE__) && defined(__MACH__)
    const QString resourcesPath = bundleResourcesPath();
    if (!resourcesPath.isEmpty())
    {
        const QString bundledTranslations
            = QDir(resourcesPath).filePath("locale");
        if (QDir(bundledTranslations).exists())
            return bundledTranslations;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString buildTranslations
        = QDir(appDir.filePath("../../..")).filePath("translations");
    if (QDir(buildTranslations).exists())
        return buildTranslations;
#endif

    return QStringLiteral(TRANSLATIONS_DIR);
}

QString
appTutorialPath() noexcept
{
#if defined(__APPLE__) && defined(__MACH__)
    const QString resourcesPath = bundleResourcesPath();
    if (!resourcesPath.isEmpty())
    {
        const QString bundledTutorial
            = QDir(resourcesPath).filePath("tutorial.pdf");
        if (QFileInfo::exists(bundledTutorial))
            return bundledTutorial;
    }
#endif

#if defined(__linux__)
    return QString("%1/share/doc/%2/tutorial.pdf")
        .arg(APP_INSTALL_PREFIX)
        .arg(APP_NAME);
#else
    return {};
#endif
}
