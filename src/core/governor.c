#define _POSIX_C_SOURCE 200809L

#include "governor.h"
#include "platform/platform_wait.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

static bool process_cpu_time_ns(uint64_t *value)
{
    struct timespec stamp;
    if (value == NULL || clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stamp) != 0 ||
        stamp.tv_sec < 0 || stamp.tv_nsec < 0 ||
        (uint64_t)stamp.tv_sec >
        (UINT64_MAX - (uint64_t)stamp.tv_nsec) / UINT64_C(1000000000))
        return false;
    *value = (uint64_t)stamp.tv_sec * UINT64_C(1000000000) +
        (uint64_t)stamp.tv_nsec;
    return true;
}

static bool monotonic_time_ns(uint64_t *value)
{
    struct timespec stamp;
    if (value == NULL || clock_gettime(CLOCK_MONOTONIC, &stamp) != 0 ||
        stamp.tv_sec < 0 || stamp.tv_nsec < 0 ||
        (uint64_t)stamp.tv_sec >
        (UINT64_MAX - (uint64_t)stamp.tv_nsec) / UINT64_C(1000000000))
        return false;
    *value = (uint64_t)stamp.tv_sec * UINT64_C(1000000000) +
        (uint64_t)stamp.tv_nsec;
    return true;
}

bool governor_init(Governor *governor, const Scenario *scenario,
                   const GovernorConfig *config)
{
    LedgerConfig ledger_config = {0};
    size_t i;
    if (governor == NULL || scenario == NULL || config == NULL ||
        scenario->tasks.count > SAGE_MAX_TASKS || scenario->step_ns == 0)
        return false;
    memset(governor, 0, sizeof(*governor));
    governor->scenario = scenario;
    governor->config = *config;
    ledger_config.capacity[LEDGER_CPU] = scenario->initial.cpu_avail_pct.value;
    ledger_config.capacity[LEDGER_MEMORY] = scenario->initial.mem_avail_KB.value;
    ledger_config.capacity[LEDGER_STORAGE] = scenario->initial.storage_avail_KB.value;
    ledger_config.capacity[LEDGER_SENSOR] = scenario->initial.sensor_avail_mask.value;
    ledger_config.capacity[LEDGER_COMM] =
        scenario->initial.comm_window_open.value != 0 ? 100 : 0;
    for (i = 0; i < LEDGER_DIMENSION_COUNT; ++i)
        ledger_config.protected_reserve[i] = config->protected_reserve[i];
    ledger_config.battery_capacity_mJ = scenario->battery_capacity_mJ;
    ledger_config.soc_floor_permille = scenario->soc_floor_permille;
    if (!ledger_init(&governor->plan.ledger, &ledger_config) ||
        !prediction_engine_init(&governor->prediction, config->alpha,
            config->beta, scenario->step_ns, config->lookahead_ns)) return false;
    for (i = 0; i < scenario->tasks.count; ++i)
        governor->plan.pending[i] = true;
    return true;
}

bool governor_observe(Governor *governor, const TelemetryFrame *frame)
{
    if (frame == NULL) return false;
    return governor_observe_at(governor, frame,
                               frame->battery_soc_mV_pct.timestamp_ns);
}

bool governor_observe_at(Governor *governor, const TelemetryFrame *frame,
                         uint64_t now_ns)
{
    uint64_t start_cpu;
    uint64_t end_cpu;
    uint64_t start_wall;
    uint64_t end_wall;
    uint64_t added;
    if (!monotonic_time_ns(&start_wall) ||
        !process_cpu_time_ns(&start_cpu)) return false;
    if (governor == NULL || frame == NULL ||
        !prediction_tracker_observe(&governor->accuracy_tracker, now_ns, frame,
            governor->accuracy_events, PREDICTION_MAX_PENDING,
            &governor->accuracy_count) ||
        !prediction_engine_update(&governor->prediction, frame)) return false;
    if (!process_cpu_time_ns(&end_cpu) || end_cpu < start_cpu ||
        !monotonic_time_ns(&end_wall) || end_wall < start_wall)
        return false;
    if (end_wall - start_wall > UINT64_MAX -
        governor->pending_prediction_wall_ns) return false;
    governor->pending_prediction_wall_ns += end_wall - start_wall;
    added = end_cpu - start_cpu;
    if (governor->config.test_injected_prediction_cpu_ns > UINT64_MAX - added)
        return false;
    added += governor->config.test_injected_prediction_cpu_ns;
    if (added > UINT64_MAX - governor->pending_prediction_cpu_ns)
        return false;
    governor->pending_prediction_cpu_ns += added;
    governor->latest = *frame;
    governor->has_frame = true;
    return true;
}

static bool predicted_for_task(const Governor *governor, const Task *task,
                               uint64_t now_ns, TelemetryFrame *forecast,
                               uint64_t *horizon_end_ns)
{
    if (!prediction_engine_forecast(&governor->prediction, task->nominal_exec_ns,
                                    now_ns, forecast, horizon_end_ns)) return false;
    if (!governor->config.prediction_enabled) *forecast = governor->latest;
    return true;
}

static bool select_task(Governor *governor, GovernorPlan *shadow,
                        PredictionTracker *tracker, size_t index,
                        uint64_t now_ns)
{
    const Task *task = &governor->scenario->tasks.items[index];
    TelemetryFrame forecast;
    uint64_t horizon_end;
    Candidate candidates[SAGE_MAX_CANDIDATES];
    size_t count;
    GateContext gate = {0};
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    UtilityContext utility = {0};
    SelectionResult selected = {0};
    LedgerDimension dimension;
    int i;
    if (!predicted_for_task(governor, task, now_ns, &forecast, &horizon_end) ||
        forecast.battery_soc_mV_pct.value < 0 ||
        forecast.battery_soc_mV_pct.value > 1000) return false;
    if (!prediction_tracker_add(tracker, task->task_id,
                                 now_ns, horizon_end, &forecast)) return false;
    count = mode_generator_generate(task, &forecast, candidates, SAGE_MAX_CANDIDATES);
    if (count == 0) return false;
    gate.telemetry = &forecast;
    gate.ledger = &shadow->ledger;
    gate.safety_margin_permille = governor->config.safety_margin_permille;
    gate.absolute_deadline_ns = task->deadline_ns;
    if (task->nominal_exec_ns > UINT64_MAX - now_ns) return false;
    gate.predicted_completion_time_ns = now_ns + task->nominal_exec_ns;
    for (i = 0; i < N_SUBSYSTEMS; ++i) {
        int64_t temp = forecast.temp_mC[i].value;
        int64_t limit = governor->config.thermal_limit_mC[i];
        gate.remaining_thermal_margin_mC[i] =
            temp >= limit ? -1 : limit - temp;
        utility.remaining_thermal_margin_mC[i] = gate.remaining_thermal_margin_mC[i];
    }
    survivors = gate_filter(candidates, count, task, &gate, &filtered);
    if (survivors == NULL) return false;
    utility.time_remaining_to_deadline_ns = now_ns < task->deadline_ns ?
        task->deadline_ns - now_ns : 0;
    utility.time_until_next_tick_ns = governor->scenario->step_ns;
    utility.fault_flags = (uint32_t)governor->latest.fault_flags.value;
    if (!ledger_query(&shadow->ledger, LEDGER_CPU, &dimension)) return false;
    utility.free_cpu_pct = dimension.free;
    if (!ledger_query(&shadow->ledger, LEDGER_ENERGY, &dimension)) return false;
    utility.energy_capacity_mJ = dimension.capacity;
    if (!ledger_query(&shadow->ledger, LEDGER_MEMORY, &dimension)) return false;
    utility.free_mem_KB = dimension.free;
    if (!ledger_query(&shadow->ledger, LEDGER_STORAGE, &dimension)) return false;
    utility.free_storage_KB = dimension.free;
    if (!ledger_query(&shadow->ledger, LEDGER_COMM, &dimension)) return false;
    utility.free_comm_pct = dimension.free;
    if (!utility_select(survivors, task, &utility, &selected)) return false;
    if (selected.outcome == SELECTION_EXECUTING) {
        Commitment *entry;
        if (shadow->commitment_count >= SAGE_MAX_TASKS ||
            !ledger_commit(&shadow->ledger, task->task_id, task->criticality_class,
                selected.selected.predicted_cost.resource_demand,
                selected.selected.predicted_cost.energy_cost_mJ)) return false;
        entry = &shadow->commitments[shadow->commitment_count++];
        entry->task_id = task->task_id;
        entry->mode = selected.selected.mode;
        entry->assumption_snapshot.soc_permille = forecast.battery_soc_mV_pct.value;
        for (i = 0; i < N_SUBSYSTEMS; ++i)
            entry->assumption_snapshot.temp_mC[i] = forecast.temp_mC[i].value;
        entry->assumption_snapshot.current_draw_mA = forecast.current_draw_mA.value;
        entry->assumption_snapshot.forecast_horizon_end_ns = horizon_end;
        entry->commit_time_ns = now_ns;
        shadow->pending[index] = false;
    } else if (selected.outcome == SELECTION_REJECTED) {
        shadow->pending[index] = false;
    }
    return true;
}

GovernorCycleResult governor_cycle(Governor *governor, uint64_t now_ns,
                                    bool fault_escalation)
{
    GovernorPlan shadow;
    PredictionTracker shadow_tracker;
    size_t i;
    size_t order[SAGE_MAX_TASKS];
    size_t processed = 0;
    uint64_t start_cpu;
    uint64_t end_cpu;
    uint16_t budget_soc;
    uint64_t prediction_cpu;
    (void)fault_escalation;
    if (governor == NULL || !governor->has_frame) return GOVERNOR_CYCLE_ERROR;
    if (!process_cpu_time_ns(&start_cpu)) return GOVERNOR_CYCLE_ERROR;
    prediction_cpu = governor->pending_prediction_cpu_ns;
    governor->pending_prediction_cpu_ns = 0;
    governor->last_prediction_wall_ns = governor->pending_prediction_wall_ns;
    governor->pending_prediction_wall_ns = 0;
    shadow = governor->plan;
    shadow_tracker = governor->accuracy_tracker;
    if (!ledger_set_rate_capacities(&shadow.ledger,
            (uint16_t)governor->latest.cpu_avail_pct.value,
            governor->latest.comm_window_open.value != 0 ? 100U : 0U))
        return GOVERNOR_CYCLE_ERROR;
    if (governor->latest.battery_soc_mV_pct.value < 0 ||
        governor->latest.battery_soc_mV_pct.value > 1000)
        return GOVERNOR_CYCLE_ERROR;
    budget_soc = governor->latest.battery_soc_mV_pct.quality ==
        TELEMETRY_QUALITY_OK ?
        (uint16_t)governor->latest.battery_soc_mV_pct.value : 0;
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        if (shadow.pending[i]) {
            TelemetryFrame forecast;
            uint64_t horizon_end;
            if (!predicted_for_task(governor,
                &governor->scenario->tasks.items[i], now_ns,
                &forecast, &horizon_end) ||
                forecast.battery_soc_mV_pct.value < 0 ||
                forecast.battery_soc_mV_pct.value > 1000)
                return GOVERNOR_CYCLE_ERROR;
            if ((uint16_t)forecast.battery_soc_mV_pct.value < budget_soc)
                budget_soc = (uint16_t)forecast.battery_soc_mV_pct.value;
        }
    }
    if (!ledger_update_energy(&shadow.ledger, budget_soc, 0, 0))
        return GOVERNOR_CYCLE_ERROR;
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        size_t at = i;
        order[i] = i;
        while (at > 0 && governor->scenario->tasks.items[order[at - 1]].criticality_class >
                         governor->scenario->tasks.items[order[at]].criticality_class) {
            size_t previous = order[at - 1];
            order[at - 1] = order[at];
            order[at] = previous;
            --at;
        }
    }
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        size_t index = order[i];
        if (shadow.pending[index]) {
            uint64_t injected;
            if (!select_task(governor, &shadow, &shadow_tracker, index, now_ns) ||
                !process_cpu_time_ns(&end_cpu) || end_cpu < start_cpu)
                return GOVERNOR_CYCLE_ERROR;
            ++processed;
            if (prediction_cpu > UINT64_MAX - (end_cpu - start_cpu))
                return GOVERNOR_CYCLE_OVERRUN;
            governor->last_cycle_cpu_ns = end_cpu - start_cpu + prediction_cpu;
            if (processed > 0 && governor->config.test_injected_cpu_ns_per_task >
                UINT64_MAX / processed) return GOVERNOR_CYCLE_OVERRUN;
            injected = governor->config.test_injected_cpu_ns_per_task * processed;
            if (injected > UINT64_MAX - governor->last_cycle_cpu_ns ||
                governor->last_cycle_cpu_ns + injected >
                governor->config.self_cost_cap_ns)
                return GOVERNOR_CYCLE_OVERRUN;
        }
    }
    if (!process_cpu_time_ns(&end_cpu) || end_cpu < start_cpu)
        return GOVERNOR_CYCLE_ERROR;
    if (prediction_cpu > UINT64_MAX - (end_cpu - start_cpu))
        return GOVERNOR_CYCLE_OVERRUN;
    governor->last_cycle_cpu_ns = end_cpu - start_cpu + prediction_cpu;
    if (governor->last_cycle_cpu_ns > governor->config.self_cost_cap_ns)
        return GOVERNOR_CYCLE_OVERRUN;
    governor->plan = shadow;
    governor->accuracy_tracker = shadow_tracker;
    return GOVERNOR_CYCLE_OK;
}

size_t governor_commitment_count(const Governor *governor)
{
    return governor == NULL ? 0 : governor->plan.commitment_count;
}

const Commitment *governor_commitment_at(const Governor *governor, size_t index)
{
    if (governor == NULL || index >= governor->plan.commitment_count) return NULL;
    return &governor->plan.commitments[index];
}

size_t governor_accuracy_count(const Governor *governor)
{
    return governor == NULL ? 0 : governor->accuracy_count;
}

const PredictionAccuracyEvent *governor_accuracy_at(const Governor *governor,
                                                    size_t index)
{
    if (governor == NULL || index >= governor->accuracy_count) return NULL;
    return &governor->accuracy_events[index];
}

static bool record_tick(Governor *governor, SimulatorPipe *stream,
                        GovernorRunStats *stats, uint64_t sim_time_ns,
                        FILE *output)
{
    uint64_t start;
    uint64_t end;
    uint64_t deadline = 0;
    GovernorTickMetric *metric;
    GovernorCycleResult result;
    uint64_t period = stream->paced ? stream->real_clock.real_step_ns :
        UINT64_C(1000000000);
    if (stats->metric_count >= GOVERNOR_MAX_METRICS ||
        !platform_clock_now_ns(&stream->real_clock, &start) ||
        (stream->paced && !platform_clock_deadline_ns(&stream->real_clock,
            sim_time_ns, &deadline))) return false;
    result = governor_cycle(governor, sim_time_ns, false);
    if (result == GOVERNOR_CYCLE_ERROR ||
        !platform_clock_now_ns(&stream->real_clock, &end) || end < start)
        return false;
    metric = &stats->metrics[stats->metric_count++];
    metric->sim_time_ns = sim_time_ns;
    if (governor->last_prediction_wall_ns > UINT64_MAX - (end - start))
        return false;
    metric->cycle_time_ns = end - start + governor->last_prediction_wall_ns;
    metric->cpu_time_ns = governor->last_cycle_cpu_ns;
    metric->tick_jitter_ns = stream->paced ?
        (start >= deadline ? (int64_t)(start - deadline) :
                             -(int64_t)(deadline - start)) : 0;
    metric->self_cost_pct_of_period = 100.0 *
        (double)metric->cpu_time_ns / (double)period;
    if (metric->tick_jitter_ns > stats->max_tick_jitter_ns)
        stats->max_tick_jitter_ns = metric->tick_jitter_ns;
    stats->mean_self_cost_pct_of_period += metric->self_cost_pct_of_period;
    if (result == GOVERNOR_CYCLE_OVERRUN) ++stats->overruns;
    ++stats->tick_cycles;
    if (output != NULL && fprintf(output,
        "TICK sim_ns=%" PRIu64 " jitter_ns=%" PRId64
        " cycle_ns=%" PRIu64 " cpu_ns=%" PRIu64
        " self_pct=%.6f overrun=%d\n",
        sim_time_ns, metric->tick_jitter_ns, metric->cycle_time_ns,
        metric->cpu_time_ns, metric->self_cost_pct_of_period,
        result == GOVERNOR_CYCLE_OVERRUN ? 1 : 0) < 0) return false;
    return true;
}

static int compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static bool finish_metrics(GovernorRunStats *stats, FILE *output)
{
    uint64_t values[GOVERNOR_MAX_METRICS];
    size_t i;
    if (stats->metric_count == 0) return false;
    for (i = 0; i < stats->metric_count; ++i)
        values[i] = stats->metrics[i].cycle_time_ns;
    qsort(values, stats->metric_count, sizeof(values[0]), compare_u64);
    stats->cycle_p50_ns = values[(stats->metric_count * 50U + 99U) / 100U - 1U];
    stats->cycle_p99_ns = values[(stats->metric_count * 99U + 99U) / 100U - 1U];
    stats->cycle_max_ns = values[stats->metric_count - 1U];
    stats->mean_self_cost_pct_of_period /= (double)stats->metric_count;
    if (output != NULL && fprintf(output,
        "SUMMARY ticks=%" PRIu64 " faults=%" PRIu64
        " overruns=%" PRIu64 " cycle_p50_ns=%" PRIu64
        " cycle_p99_ns=%" PRIu64 " cycle_max_ns=%" PRIu64
        " max_jitter_ns=%" PRId64 " mean_self_pct=%.6f\n",
        stats->tick_cycles, stats->fault_cycles, stats->overruns,
        stats->cycle_p50_ns, stats->cycle_p99_ns,
        stats->cycle_max_ns, stats->max_tick_jitter_ns,
        stats->mean_self_cost_pct_of_period) < 0) return false;
    return true;
}

bool governor_run_pipe(Governor *governor, SimulatorPipe *stream,
                       GovernorRunStats *stats)
{
    return governor_run_pipe_logged(governor, stream, stats, NULL);
}

bool governor_run_pipe_logged(Governor *governor, SimulatorPipe *stream,
                              GovernorRunStats *stats, FILE *output)
{
    uint64_t next_tick = 0;
    if (governor == NULL || stream == NULL || stats == NULL ||
        governor->scenario == NULL) return false;
    memset(stats, 0, sizeof(*stats));
    for (;;) {
        PlatformIpcMessage message;
        bool ready;
        if (!platform_wait_for_fd(stream->read_fd, &stream->real_clock,
            stream->paced && governor->has_frame, next_tick, &ready)) return false;
        if (!ready) {
            if (next_tick > governor->scenario->duration_ns) continue;
            if (!record_tick(governor, stream, stats, next_tick, output))
                return false;
            if (next_tick > UINT64_MAX - governor->scenario->step_ns) return false;
            next_tick += governor->scenario->step_ns;
            continue;
        }
        if (!platform_ipc_read_message(stream, &message)) return false;
        if (message.type == PLATFORM_IPC_DONE)
            return finish_metrics(stats, output);
        if (message.type == PLATFORM_IPC_FRAME) {
            size_t accuracy_index;
            if (!governor_observe_at(governor, &message.frame,
                                     message.sim_time_ns)) return false;
            for (accuracy_index = 0; output != NULL &&
                 accuracy_index < governor->accuracy_count; ++accuracy_index) {
                const PredictionAccuracyEvent *event =
                    &governor->accuracy_events[accuracy_index];
                int subsystem;
                if (fprintf(output,
                    "PRED task=%" PRIu32 " issued_ns=%" PRIu64
                    " horizon_ns=%" PRIu64 " actual_ns=%" PRIu64
                    " soc=%" PRId64 "/%" PRId64
                    " draw=%" PRId64 "/%" PRId64,
                    event->task_id, event->issued_ns,
                    event->predicted.forecast_horizon_end_ns,
                    event->actual.forecast_horizon_end_ns,
                    event->predicted.soc_permille, event->actual.soc_permille,
                    event->predicted.current_draw_mA,
                    event->actual.current_draw_mA) < 0) return false;
                for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem) {
                    if (fprintf(output, " temp%d=%" PRId64 "/%" PRId64,
                        subsystem, event->predicted.temp_mC[subsystem],
                        event->actual.temp_mC[subsystem]) < 0) return false;
                }
                if (fputc('\n', output) == EOF) return false;
            }
            stats->last_sim_time_ns = message.sim_time_ns;
            while (next_tick <= message.sim_time_ns &&
                   next_tick <= governor->scenario->duration_ns) {
                if (!record_tick(governor, stream, stats, next_tick, output))
                    return false;
                if (next_tick > UINT64_MAX - governor->scenario->step_ns)
                    return false;
                next_tick += governor->scenario->step_ns;
            }
        } else if (message.type == PLATFORM_IPC_FAULT_ESCALATION) {
            if (governor_cycle(governor, message.sim_time_ns, true) ==
                GOVERNOR_CYCLE_ERROR) return false;
            ++stats->fault_cycles;
        } else return false;
    }
}

bool governor_calibrate_self_cost(Governor *governor, size_t repeats,
                                  uint64_t real_tick_interval_ns,
                                  double assumed_active_power_W,
                                  GovernorSelfCost *out)
{
    uint64_t maximum = 0;
    uint64_t cap;
    uint64_t modeled_energy;
    uint16_t cpu_pct;
    double cpu_percent;
    double energy_mJ;
    size_t i;
    if (governor == NULL || out == NULL || !governor->has_frame ||
        governor->plan.commitment_count != 0 || repeats == 0 ||
        real_tick_interval_ns == 0 || assumed_active_power_W <= 0.0 ||
        assumed_active_power_W != assumed_active_power_W) return false;
    for (i = 0; i < repeats; ++i) {
        Governor probe = *governor;
        probe.config.self_cost_cap_ns = UINT64_MAX;
        probe.config.test_injected_cpu_ns_per_task = 0;
        if (governor_cycle(&probe, 0, false) != GOVERNOR_CYCLE_OK)
            return false;
        if (probe.last_cycle_cpu_ns > maximum)
            maximum = probe.last_cycle_cpu_ns;
    }
    if (maximum == 0 || maximum > UINT64_MAX - maximum / 4U - 1000U)
        return false;
    cap = maximum + maximum / 4U + 1000U;
    cpu_percent = (double)cap * 100.0 / (double)real_tick_interval_ns;
    energy_mJ = (double)cap * assumed_active_power_W / 1000000.0;
    if (cpu_percent > 100.0 || energy_mJ > (double)INT64_MAX)
        return false;
    cpu_pct = (uint16_t)cpu_percent;
    if ((double)cpu_pct < cpu_percent) ++cpu_pct;
    if (cpu_pct == 0) cpu_pct = 1;
    modeled_energy = (uint64_t)energy_mJ;
    if ((double)modeled_energy < energy_mJ) ++modeled_energy;
    if (modeled_energy == 0) modeled_energy = 1;
    if (!ledger_update_energy(&governor->plan.ledger,
        (uint16_t)governor->latest.battery_soc_mV_pct.value, 0, 0) ||
        !ledger_set_self_cost(&governor->plan.ledger, cpu_pct,
                              modeled_energy)) return false;
    governor->config.self_cost_cap_ns = cap;
    *out = (GovernorSelfCost){maximum, cap, cpu_pct,
                              modeled_energy, assumed_active_power_W};
    return true;
}
