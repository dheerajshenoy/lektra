#pragma once

class CrashHandler
{
public:
    // Call once, after QCoreApplication is constructed.
    static void install() noexcept;
};
