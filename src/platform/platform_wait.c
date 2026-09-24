#define _POSIX_C_SOURCE 200809L

#include "platform_wait.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>

bool platform_wait_for_fd(int fd, const PlatformClock *clock,
                          bool paced, uint64_t next_sim_tick_ns,
                          bool *ready)
{
    uint64_t deadline = 0;
    struct pollfd item;
    if (fd < 0 || ready == NULL ||
        (paced && !platform_clock_deadline_ns(clock, next_sim_tick_ns,
                                               &deadline))) return false;
    item.fd = fd;
    item.events = POLLIN;
    item.revents = 0;
    for (;;) {
        int timeout_ms = -1;
        int result;
        if (paced) {
            uint64_t now;
            uint64_t remaining;
            uint64_t rounded;
            if (!platform_clock_now_ns(clock, &now)) return false;
            if (now >= deadline) timeout_ms = 0;
            else {
                remaining = deadline - now;
                rounded = remaining / UINT64_C(1000000);
                if (remaining % UINT64_C(1000000) != 0) ++rounded;
                timeout_ms = rounded > INT_MAX ? INT_MAX : (int)rounded;
            }
        }
        result = poll(&item, 1, timeout_ms);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 || (result > 0 && (item.revents & POLLNVAL) != 0))
            return false;
        *ready = result > 0;
        return true;
    }
}
