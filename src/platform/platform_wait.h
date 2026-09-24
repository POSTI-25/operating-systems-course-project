#ifndef SAGE_PLATFORM_WAIT_H
#define SAGE_PLATFORM_WAIT_H

#include "platform_clock.h"

bool platform_wait_for_fd(int fd, const PlatformClock *clock,
                          bool paced, uint64_t next_sim_tick_ns,
                          bool *ready);

#endif
