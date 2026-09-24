#include "simulator.h"

#include <limits.h>
#include <stddef.h>

static void consider(uint64_t candidate, uint64_t after, uint64_t end,
                     uint64_t *best, bool *found)
{
    if (candidate > after && candidate <= end && (!*found || candidate < *best)) {
        *best = candidate;
        *found = true;
    }
}

static void orbit_candidates(const Scenario *scenario, uint64_t after,
                             uint64_t *best, bool *found)
{
    uint64_t cycle;
    uint64_t base;
    uint64_t k;
    if (!scenario->has_orbit || scenario->orbit_epoch_ns > scenario->duration_ns) return;
    consider(scenario->orbit_epoch_ns, after, scenario->duration_ns, best, found);
    cycle = after < scenario->orbit_epoch_ns ? 0 :
        (after - scenario->orbit_epoch_ns) / scenario->orbit_period_ns;
    for (k = cycle; k <= cycle + 1 && k >= cycle; ++k) {
        uint64_t entry;
        uint64_t exit;
        if (k > (UINT64_MAX - scenario->orbit_epoch_ns) / scenario->orbit_period_ns) break;
        base = scenario->orbit_epoch_ns + k * scenario->orbit_period_ns;
        if (scenario->eclipse_offset_ns > UINT64_MAX - base) break;
        entry = base + scenario->eclipse_offset_ns;
        if (scenario->eclipse_duration_ns > UINT64_MAX - entry) break;
        exit = entry + scenario->eclipse_duration_ns;
        consider(entry, after, scenario->duration_ns, best, found);
        consider(exit, after, scenario->duration_ns, best, found);
        if (k == UINT64_MAX) break;
    }
}

static bool eclipse_at(const Scenario *scenario, uint64_t time)
{
    uint64_t phase;
    if (time < scenario->orbit_epoch_ns) return scenario->initial.eclipse.value != 0;
    phase = (time - scenario->orbit_epoch_ns) % scenario->orbit_period_ns;
    return phase >= scenario->eclipse_offset_ns &&
        phase < scenario->eclipse_offset_ns + scenario->eclipse_duration_ns;
}

bool simulator_init(Simulator *sim, const Scenario *scenario, PlatformClock *clock)
{
    if (sim == NULL || scenario == NULL || clock == NULL ||
        clock->kind != PLATFORM_CLOCK_VIRTUAL || scenario->step_ns == 0) return false;
    sim->scenario = scenario;
    sim->clock = clock;
    sim->current = scenario->initial;
    sim->last_time_ns = 0;
    sim->started = false;
    sim->finished = false;
    return platform_clock_init(clock, PLATFORM_CLOCK_VIRTUAL, 0);
}

SimulatorResult simulator_next(Simulator *sim, TelemetryFrame *frame,
                               uint64_t *frame_time_ns)
{
    uint64_t time = 0;
    bool found = false;
    size_t i;
    if (sim == NULL || frame == NULL || frame_time_ns == NULL ||
        sim->scenario == NULL || sim->clock == NULL) return SIMULATOR_ERROR;
    if (sim->finished) return SIMULATOR_DONE;
    if (!sim->started) {
        time = 0;
        sim->started = true;
    } else {
        uint64_t after = sim->last_time_ns;
        uint64_t step = sim->scenario->step_ns;
        uint64_t quotient = after / step;
        if (quotient < UINT64_MAX / step) {
            consider((quotient + 1) * step, after, sim->scenario->duration_ns,
                     &time, &found);
        }
        consider(sim->scenario->duration_ns, after, sim->scenario->duration_ns,
                 &time, &found);
        for (i = 0; i < sim->scenario->update_count; ++i)
            consider(sim->scenario->updates[i].time_ns, after,
                     sim->scenario->duration_ns, &time, &found);
        for (i = 0; i < sim->scenario->fault_count; ++i)
            consider(sim->scenario->faults[i].time_ns, after,
                     sim->scenario->duration_ns, &time, &found);
        orbit_candidates(sim->scenario, after, &time, &found);
        if (!found) {
            sim->finished = true;
            return SIMULATOR_DONE;
        }
        if (!platform_clock_advance_ns(sim->clock, time - after)) return SIMULATOR_ERROR;
    }
    if (sim->scenario->has_orbit) {
        TelemetryChannel eclipse = sim->current.eclipse;
        int64_t wanted = eclipse_at(sim->scenario, time) ? 1 : 0;
        if (eclipse.value != wanted) {
            eclipse.value = wanted;
            eclipse.timestamp_ns = time;
            eclipse.quality = TELEMETRY_QUALITY_OK;
            if (!telemetry_frame_set(&sim->current, TELEMETRY_ECLIPSE, 0, eclipse))
                return SIMULATOR_ERROR;
        }
    }
    for (i = 0; i < sim->scenario->update_count; ++i) {
        const ScenarioUpdate *update = &sim->scenario->updates[i];
        if (update->time_ns == time &&
            !telemetry_frame_set(&sim->current, update->id, update->index, update->channel))
            return SIMULATOR_ERROR;
    }
    for (i = 0; i < sim->scenario->fault_count; ++i) {
        const ScenarioFault *fault = &sim->scenario->faults[i];
        if (fault->time_ns == time) {
            TelemetryChannel channel = sim->current.fault_flags;
            uint32_t mask = (uint32_t)channel.value;
            mask = (mask | fault->set_mask) & ~fault->clear_mask;
            channel.value = (int64_t)mask;
            channel.timestamp_ns = time;
            channel.quality = TELEMETRY_QUALITY_OK;
            if (!telemetry_frame_set(&sim->current, TELEMETRY_FAULT_FLAGS, 0, channel))
                return SIMULATOR_ERROR;
        }
    }
    sim->last_time_ns = time;
    *frame_time_ns = time;
    *frame = sim->current;
    return SIMULATOR_FRAME;
}
