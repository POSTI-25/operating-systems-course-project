#define _POSIX_C_SOURCE 200809L

#include "platform_ipc.h"
#include "sim/simulator.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <sys/wait.h>
#include <unistd.h>

_Static_assert(sizeof(PlatformIpcMessage) <= PIPE_BUF,
               "IPC message exceeds atomic pipe write size");

static bool write_message(int fd, const PlatformIpcMessage *message)
{
    const unsigned char *bytes = (const unsigned char *)(const void *)message;
    size_t sent = 0;
    while (sent < sizeof(*message)) {
        ssize_t written = write(fd, bytes + sent, sizeof(*message) - sent);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        sent += (size_t)written;
    }
    return true;
}

static bool wait_until(const PlatformClock *clock, uint64_t sim_time_ns)
{
    uint64_t deadline;
    if (!platform_clock_deadline_ns(clock, sim_time_ns, &deadline)) return false;
    for (;;) {
        uint64_t now;
        uint64_t remaining;
        uint64_t timeout;
        int result;
        if (!platform_clock_now_ns(clock, &now)) return false;
        if (now >= deadline) return true;
        remaining = deadline - now;
        timeout = remaining / UINT64_C(1000000);
        if (remaining % UINT64_C(1000000) != 0) ++timeout;
        if (timeout > INT_MAX) timeout = INT_MAX;
        result = poll(NULL, 0, (int)timeout);
        if (result < 0 && errno != EINTR) return false;
    }
}

static void child_run(int write_fd, const Scenario *scenario,
                      const PlatformClock *real_clock, bool paced)
{
    PlatformClock virtual_clock;
    Simulator simulator;
    TelemetryFrame frame;
    uint64_t time_ns;
    SimulatorResult result;
    if (!platform_clock_init(&virtual_clock, PLATFORM_CLOCK_VIRTUAL, 0) ||
        !simulator_init(&simulator, scenario, &virtual_clock)) _exit(2);
    while ((result = simulator_next(&simulator, &frame, &time_ns)) == SIMULATOR_FRAME) {
        PlatformIpcMessage message = {0};
        size_t i;
        if (paced && !wait_until(real_clock, time_ns)) _exit(2);
        message.type = PLATFORM_IPC_FRAME;
        message.sim_time_ns = time_ns;
        message.frame = frame;
        if (!write_message(write_fd, &message)) _exit(2);
        for (i = 0; i < scenario->fault_count; ++i) {
            if (scenario->faults[i].time_ns == time_ns &&
                scenario->faults[i].set_mask != 0U) {
                message = (PlatformIpcMessage){0};
                message.type = PLATFORM_IPC_FAULT_ESCALATION;
                message.sim_time_ns = time_ns;
                message.fault_set_mask = scenario->faults[i].set_mask;
                if (!write_message(write_fd, &message)) _exit(2);
            }
        }
    }
    if (result != SIMULATOR_DONE) _exit(2);
    {
        PlatformIpcMessage done = {0};
        done.type = PLATFORM_IPC_DONE;
        if (!write_message(write_fd, &done)) _exit(2);
    }
    (void)close(write_fd);
    _exit(0);
}

bool platform_ipc_spawn(SimulatorPipe *stream, const Scenario *scenario,
                        uint64_t real_tick_interval_ns)
{
    int fds[2];
    pid_t child;
    if (stream == NULL || scenario == NULL || scenario->step_ns == 0 ||
        !platform_clock_init(&stream->real_clock, PLATFORM_CLOCK_REAL, 0))
        return false;
    if (real_tick_interval_ns != 0 &&
        !platform_clock_set_time_scale(&stream->real_clock,
            scenario->step_ns, real_tick_interval_ns)) return false;
    if (pipe(fds) != 0) return false;
    child = fork();
    if (child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return false;
    }
    if (child == 0) {
        (void)close(fds[0]);
        child_run(fds[1], scenario, &stream->real_clock,
                  real_tick_interval_ns != 0);
    }
    (void)close(fds[1]);
    stream->read_fd = fds[0];
    stream->child_pid = child;
    stream->paced = real_tick_interval_ns != 0;
    return true;
}

bool platform_ipc_read_message(SimulatorPipe *stream, PlatformIpcMessage *message)
{
    unsigned char *bytes = (unsigned char *)(void *)message;
    size_t received = 0;
    if (stream == NULL || message == NULL || stream->read_fd < 0) return false;
    while (received < sizeof(*message)) {
        ssize_t amount = read(stream->read_fd, bytes + received,
                              sizeof(*message) - received);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) return false;
        received += (size_t)amount;
    }
    return true;
}

bool platform_ipc_close(SimulatorPipe *stream)
{
    int status;
    pid_t child;
    if (stream == NULL || stream->read_fd < 0 || stream->child_pid <= 0)
        return false;
    (void)close(stream->read_fd);
    stream->read_fd = -1;
    do {
        child = waitpid(stream->child_pid, &status, 0);
    } while (child < 0 && errno == EINTR);
    stream->child_pid = -1;
    return child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
