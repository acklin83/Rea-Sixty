#pragma once
//
// One line of ORC's log (/tmp/orc.log or %TEMP%\\orc.log, set up in main.cpp) with the wall-clock
// time in front: "[19:05:14.505] ORC: ...". Without it the log said THAT the
// UF1 fell off the bus and was reopened thirteen times, but not WHEN, and the
// recording dropouts of 01.10.2026 could not be laid next to it. Rea-Sixty
// writes its side of the handover into the same file in the same format
// (orcLogLine_ in main.cpp), so one file tells who held the UF1 and TotalMix.
// Formatted first and written in one go, so the input thread's lines and the
// surface loop's cannot interleave inside a line.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace orc {

inline void logLine(const char* fmt, ...)
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t secs = system_clock::to_time_t(now);
    const int millis = static_cast<int>(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &secs);
#else
    localtime_r(&secs, &lt);
#endif
    char buf[1024];
    int n = static_cast<int>(std::strftime(buf, sizeof(buf), "[%H:%M:%S", &lt));
    // ⛔ snprintf unqualified and its result unused: WDL makes it a void macro under
    // Windows (memory uf1-sidecar-rme-plan).
    snprintf(buf + n, sizeof(buf) - n, ".%03d] ", millis);
    n += 6;
    va_list ap;
    va_start(ap, fmt);
    if (n < static_cast<int>(sizeof(buf))) vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    std::fflush(stdout);
}

} // namespace orc
