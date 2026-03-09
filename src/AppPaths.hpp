#pragma once

#include "TranslationsDir.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace AppPaths
{

#if defined(__APPLE__) && defined(__MACH__)
QString
bundleResourcesPath() noexcept
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = QDir::cleanPath(appDir.filePath("../Resources"));

    if (QDir(candidate).exists())
        return candidate;

    return {};
}
#endif

static QString
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

static QString
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

} // namespace AppPaths
