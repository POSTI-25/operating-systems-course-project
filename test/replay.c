#include "replay.h"
#include "sim/simulator.h"
#include <inttypes.h>

static bool replay_cycle(Governor *governor, const TelemetryFrame *frame,
    uint64_t now_ns, uint32_t fault_mask, FILE *output,
    ReplayCycleHook on_cycle, void *context)
{
    GovernorCycleResult result = governor_cycle_with_fault_mask(governor,
                                                               now_ns, fault_mask);
    size_t i;
    if (result == GOVERNOR_CYCLE_ERROR) return false;
    if (output != NULL) {
        if (result == GOVERNOR_CYCLE_OVERRUN) {
            if (fprintf(output, "OVERRUN sim_ns=%" PRIu64 " trigger=%s\n",
                now_ns, fault_mask != 0 ? "FAULT" : "TICK") < 0) return false;
        } else {
            for (i = 0; i < governor->last_record_count; ++i)
                if (!logger_write(output, governor_log_record_at(governor, i)))
                    return false;
        }
    }
    return on_cycle == NULL || on_cycle(governor, frame, now_ns, result,
                                       context);
}

bool replay_run(const Scenario *scenario, Governor *governor,
                FILE *decision_output, ReplayCycleHook on_cycle, void *context)
{
    PlatformClock clock;
    Simulator simulator;
    SimulatorResult result;
    TelemetryFrame frame;
    GovernorVariantPolicy policy;
    uint64_t frame_time;
    uint64_t next_tick = 0;
    bool ticks_done = false;
    if (scenario == NULL || governor == NULL ||
        !governor_variant_policy(governor->config.variant, &policy) ||
        !platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, 0) ||
        !simulator_init(&simulator, scenario, &clock)) return false;
    while ((result = simulator_next(&simulator, &frame, &frame_time)) == SIMULATOR_FRAME) {
        size_t i;
        if (!governor_observe_at(governor, &frame, frame_time)) return false;
        while (!ticks_done && next_tick <= frame_time && next_tick <= scenario->duration_ns) {
            if (!replay_cycle(governor, &frame, next_tick, 0,
                              decision_output, on_cycle, context)) return false;
            if (next_tick > UINT64_MAX - scenario->step_ns) ticks_done = true;
            else next_tick += scenario->step_ns;
        }
        if (!policy.immediate_fault_cycle) continue;
        for (i = 0; i < scenario->fault_count; ++i)
            if (scenario->faults[i].time_ns == frame_time &&
                scenario->faults[i].set_mask != 0U &&
                !replay_cycle(governor, &frame, frame_time,
                    scenario->faults[i].set_mask, decision_output, on_cycle, context))
                return false;
    }
    return result == SIMULATOR_DONE &&
        governor_settle_execution_at(governor, scenario->duration_ns);
}
