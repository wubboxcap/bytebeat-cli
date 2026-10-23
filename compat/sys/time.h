#ifndef BYTEBEAT_COMPAT_SYS_TIME_H
#define BYTEBEAT_COMPAT_SYS_TIME_H

#include <winsock.h>

static inline int gettimeofday(struct timeval *value, void *timezone)
{
    FILETIME file_time;
    ULARGE_INTEGER ticks;
    unsigned long long microseconds;

    (void)timezone;
    GetSystemTimeAsFileTime(&file_time);
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    microseconds = ticks.QuadPart / 10;
    microseconds -= 11644473600000000ULL;
    value->tv_sec = (long)(microseconds / 1000000ULL);
    value->tv_usec = (long)(microseconds % 1000000ULL);
    return 0;
}

#endif