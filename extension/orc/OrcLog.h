#pragma once
//
// One line of ORC's log (/tmp/orc.log, set up in main.cpp) with the wall-clock
// time in front: "[19:05:14.505] ORC: ...". Without it the log said THAT the
// UF1 fell off the bus and was reopened thirteen times, but not WHEN, and the
// recording dropouts of 01.10.2026 could not be laid next to it. Rea-Sixty
// writes its side of the handover into the same file in the same format
// (orcLogLine_ in main.cpp), so one file tells who held the UF1 and TotalMix.
// Formatted first and written in one go, so the input thread's lines and the
// surface loop's cannot interleave inside a line.
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

namespace orc {

inline void logLine(const char* fmt, ...)
{
    timeval tv{};
    gettimeofday(&tv, nullptr);
    std::tm lt{};
    localtime_r(&tv.tv_sec, &lt);
    char buf[1024];
    int n = static_cast<int>(std::strftime(buf, sizeof(buf), "[%H:%M:%S", &lt));
    n += std::snprintf(buf + n, sizeof(buf) - n, ".%03d] ", static_cast<int>(tv.tv_usec / 1000));
    va_list ap;
    va_start(ap, fmt);
    if (n < static_cast<int>(sizeof(buf))) std::vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    std::fflush(stdout);
}

} // namespace orc
