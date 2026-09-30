#ifndef SAGE_REPLAY_H
#define SAGE_REPLAY_H
#include "core/governor.h"

typedef bool (*ReplayCycleHook)(const Governor *, const TelemetryFrame *,
                                uint64_t now_ns, GovernorCycleResult result,
                                void *context);
bool replay_run(const Scenario *scenario, Governor *governor,
                FILE *decision_output, ReplayCycleHook on_cycle, void *context);
#endif
