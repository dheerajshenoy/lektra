#pragma once

#include <QByteArray>
#include <chrono>
#include <cstdarg>
#include <cstdio>

// Render timing log, for measuring the render pipeline (see
// scripts/bench/README.md). Off unless LEKTRA_RENDER_TRACE=1 is set; when off
// every call is one branch on a static bool.
//
// Lines go to stderr as:   RTRACE <ms-since-start> <event> key=value ...
namespace rtrace
{
inline bool
enabled() noexcept
{
    static const bool on = qEnvironmentVariableIntValue("LEKTRA_RENDER_TRACE") != 0;
    return on;
}

// Milliseconds on a steady clock (fractional).
inline double
nowMs() noexcept
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch())
        .count();
}

inline double
sinceStartMs() noexcept
{
    static const double start = nowMs();
    return nowMs() - start;
}

// One call, one fprintf: lines from different threads do not interleave.
inline void
log(const char *event, const char *fmt, ...) noexcept
{
    if (!enabled())
        return;
    char body[384];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "RTRACE %.3f %s %s\n", sinceStartMs(), event, body);
}
} // namespace rtrace
