#ifndef SAGE_PLATFORM_CLOCK_H
#define SAGE_PLATFORM_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PLATFORM_CLOCK_VIRTUAL = 0,
    PLATFORM_CLOCK_REAL = 1
} PlatformClockKind;

typedef struct {
    PlatformClockKind kind;
    uint64_t virtual_now_ns;
    uint64_t real_origin_ns;
    uint64_t simulated_step_ns;
    uint64_t real_step_ns;
} PlatformClock;

bool platform_clock_init(PlatformClock *clock, PlatformClockKind kind,
                         uint64_t initial_ns);
bool platform_clock_now_ns(const PlatformClock *clock, uint64_t *now_ns);
bool platform_clock_advance_ns(PlatformClock *clock, uint64_t delta_ns);
bool platform_clock_set_time_scale(PlatformClock *clock,
                                   uint64_t simulated_step_ns,
                                   uint64_t real_step_ns);
bool platform_clock_deadline_ns(const PlatformClock *clock,
                                uint64_t simulated_time_ns,
                                uint64_t *deadline_ns);

#endif
