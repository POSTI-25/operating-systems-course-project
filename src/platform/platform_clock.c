#define _POSIX_C_SOURCE 200809L

#include "platform_clock.h"

#include <limits.h>
#include <time.h>

bool platform_clock_init(PlatformClock *clock, PlatformClockKind kind,
                         uint64_t initial_ns)
{
    if (clock == NULL || (kind != PLATFORM_CLOCK_VIRTUAL && kind != PLATFORM_CLOCK_REAL)) {
        return false;
    }
    clock->kind = kind;
    clock->virtual_now_ns = kind == PLATFORM_CLOCK_VIRTUAL ? initial_ns : 0;
    return true;
}

bool platform_clock_now_ns(const PlatformClock *clock, uint64_t *now_ns)
{
    struct timespec ts;
    if (clock == NULL || now_ns == NULL) {
        return false;
    }
    if (clock->kind == PLATFORM_CLOCK_VIRTUAL) {
        *now_ns = clock->virtual_now_ns;
        return true;
    }
    if (clock->kind != PLATFORM_CLOCK_REAL || clock_gettime(CLOCK_MONOTONIC, &ts) != 0 ||
        ts.tv_sec < 0 || ts.tv_nsec < 0) {
        return false;
    }
    if ((uint64_t)ts.tv_sec > (UINT64_MAX - (uint64_t)ts.tv_nsec) / UINT64_C(1000000000)) {
        return false;
    }
    *now_ns = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
    return true;
}

bool platform_clock_advance_ns(PlatformClock *clock, uint64_t delta_ns)
{
    if (clock == NULL || clock->kind != PLATFORM_CLOCK_VIRTUAL ||
        delta_ns > UINT64_MAX - clock->virtual_now_ns) {
        return false;
    }
    clock->virtual_now_ns += delta_ns;
    return true;
}
