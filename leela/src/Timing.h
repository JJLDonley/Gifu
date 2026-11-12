#ifndef TIMING_H_INCLUDED
#define TIMING_H_INCLUDED

#include <chrono>

class Time {
public:
    // The current time.
    Time(void);
    // Time difference in centiseconds.
    static int timediff(Time start, Time end);

private:
    std::chrono::steady_clock::time_point m_time;
};

#endif
