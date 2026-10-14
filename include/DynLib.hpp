#pragma once

#include <QDebug>
#include <QLibrary>
#include <QString>
#include <initializer_list>

namespace DynLib
{
// Loads an optional shared library at runtime without hardcoding one soname.
//
// Order tried:
//   1. The path in environment variable `env` (if set), for unusual setups.
//   2. `base` with each of `versions`, newest first (libfoo.so.<ver> on Linux,
//      libfoo.<ver>.dylib on macOS; the version is ignored on Windows).
//   3. `base` alone, which finds the unversioned dev symlink if present.
//
// Returns true if the library is loaded, and logs once which file was used.
inline bool
load(QLibrary &lib, const char *env, const QString &base,
     std::initializer_list<const char *> versions = {}) noexcept
{
    if (lib.isLoaded())
        return true;

    const QString override_path = qEnvironmentVariable(env);
    if (!override_path.isEmpty())
    {
        lib.setFileName(override_path);
        if (lib.load())
        {
            qInfo() << "Loaded" << lib.fileName() << "from" << env;
            return true;
        }
        qWarning() << env << "is set but" << override_path
                   << "could not be loaded:" << lib.errorString();
    }

    for (const char *ver : versions)
    {
        lib.setFileNameAndVersion(base, QString::fromLatin1(ver));
        if (lib.load())
        {
            qInfo() << "Loaded" << lib.fileName();
            return true;
        }
    }

    lib.setFileName(base);
    if (lib.load())
    {
        qInfo() << "Loaded" << lib.fileName();
        return true;
    }
    return false;
}
} // namespace DynLib
