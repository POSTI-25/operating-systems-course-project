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
    clock->real_origin_ns = 0;
    clock->simulated_step_ns = 0;
    clock->real_step_ns = 0;
    if (kind == PLATFORM_CLOCK_REAL &&
        !platform_clock_now_ns(clock, &clock->real_origin_ns)) return false;
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

bool platform_clock_set_time_scale(PlatformClock *clock,
                                   uint64_t simulated_step_ns,
                                   uint64_t real_step_ns)
{
    if (clock == NULL || clock->kind != PLATFORM_CLOCK_REAL ||
        simulated_step_ns == 0 || real_step_ns == 0) return false;
    clock->simulated_step_ns = simulated_step_ns;
    clock->real_step_ns = real_step_ns;
    return true;
}

bool platform_clock_deadline_ns(const PlatformClock *clock,
                                uint64_t simulated_time_ns,
                                uint64_t *deadline_ns)
{
    uint64_t whole;
    uint64_t remainder;
    uint64_t elapsed;
    uint64_t fractional;
    if (clock == NULL || deadline_ns == NULL ||
        clock->kind != PLATFORM_CLOCK_REAL || clock->simulated_step_ns == 0)
        return false;
    whole = simulated_time_ns / clock->simulated_step_ns;
    remainder = simulated_time_ns % clock->simulated_step_ns;
    if (whole > UINT64_MAX / clock->real_step_ns ||
        remainder > UINT64_MAX / clock->real_step_ns) return false;
    elapsed = whole * clock->real_step_ns;
    fractional = remainder * clock->real_step_ns /
        clock->simulated_step_ns;
    if (fractional > UINT64_MAX - elapsed) return false;
    elapsed += fractional;
    if (elapsed > UINT64_MAX - clock->real_origin_ns) return false;
    *deadline_ns = clock->real_origin_ns + elapsed;
    return true;
}
