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
} PlatformClock;

bool platform_clock_init(PlatformClock *clock, PlatformClockKind kind,
                         uint64_t initial_ns);
bool platform_clock_now_ns(const PlatformClock *clock, uint64_t *now_ns);
bool platform_clock_advance_ns(PlatformClock *clock, uint64_t delta_ns);

#endif
