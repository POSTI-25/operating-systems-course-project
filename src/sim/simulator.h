#ifndef SAGE_SIMULATOR_H
#define SAGE_SIMULATOR_H

#include "platform/platform_clock.h"
#include "sim/scenario.h"

typedef enum {
    SIMULATOR_FRAME = 0,
    SIMULATOR_DONE = 1,
    SIMULATOR_ERROR = 2
} SimulatorResult;

typedef struct {
    const Scenario *scenario;
    PlatformClock *clock;
    TelemetryFrame current;
    uint64_t last_time_ns;
    bool started;
    bool finished;
} Simulator;

bool simulator_init(Simulator *sim, const Scenario *scenario, PlatformClock *clock);
SimulatorResult simulator_next(Simulator *sim, TelemetryFrame *frame,
                               uint64_t *frame_time_ns);

#endif
