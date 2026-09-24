#ifndef SAGE_PLATFORM_IPC_H
#define SAGE_PLATFORM_IPC_H

#include "platform_clock.h"
#include "sim/scenario.h"

#include <sys/types.h>

typedef enum {
    PLATFORM_IPC_FRAME = 1,
    PLATFORM_IPC_FAULT_ESCALATION = 2,
    PLATFORM_IPC_DONE = 3
} PlatformIpcType;

typedef struct {
    PlatformIpcType type;
    uint64_t sim_time_ns;
    TelemetryFrame frame;
    uint32_t fault_set_mask;
} PlatformIpcMessage;

typedef struct {
    int read_fd;
    pid_t child_pid;
    PlatformClock real_clock;
    bool paced;
} SimulatorPipe;

bool platform_ipc_spawn(SimulatorPipe *stream, const Scenario *scenario,
                        uint64_t real_tick_interval_ns);
bool platform_ipc_read_message(SimulatorPipe *stream, PlatformIpcMessage *message);
bool platform_ipc_close(SimulatorPipe *stream);

#endif
