#include "Timing.h"

#include <cstdlib>

int Time::timediff(Time start, Time end) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end.m_time - start.m_time).count();
    return std::abs(static_cast<int>((ms + 5) / 10));
}

Time::Time(void) : m_time(std::chrono::steady_clock::now()) {}
