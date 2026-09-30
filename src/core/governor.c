#define _POSIX_C_SOURCE 200809L

#include "governor.h"
#include "platform/platform_wait.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

bool governor_variant_policy(GovernorVariant variant, GovernorVariantPolicy *out)
{
    static const GovernorVariantPolicy policies[] = {
        {0x1f, GATE_MASK_ALL, UTILITY_TERM_ALL, 0x3f, true, true},
        {0x01, 0, 0, 0, false, false},
        {0x09, GATE_MASK_SOC | GATE_MASK_DEADLINE,
         UTILITY_TERM_DEADLINE | UTILITY_TERM_ENERGY, 0x02, false, false},
        {0x1f, GATE_MASK_THERMAL | GATE_MASK_PROTECTED | GATE_MASK_DEADLINE,
         UTILITY_TERM_FAULT | UTILITY_TERM_DEADLINE | UTILITY_TERM_THERMAL |
         UTILITY_TERM_RESOURCE, 0x3d, false, true}
    };
    if (out == NULL || variant < GOVERNOR_VARIANT_SAGE_FULL ||
        variant > GOVERNOR_VARIANT_RANJBAR_STYLE) return false;
    *out = policies[variant];
    return true;
}

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
    GovernorVariantPolicy policy;
    size_t i;
    if (governor == NULL || scenario == NULL || config == NULL ||
        scenario->tasks.count > SAGE_MAX_TASKS || scenario->step_ns == 0 ||
        !governor_variant_policy(config->variant, &policy))
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
    if (governor->observation_epoch == UINT64_MAX) return false;
    ++governor->observation_epoch;
    return true;
}

static bool predicted_for_task(const Governor *governor, const Task *task,
                               uint64_t now_ns, TelemetryFrame *forecast,
                               uint64_t *horizon_end_ns)
{
    GovernorVariantPolicy policy;
    if (!governor_variant_policy(governor->config.variant, &policy)) return false;
    if (!prediction_engine_forecast(&governor->prediction, task->nominal_exec_ns,
                                    now_ns, forecast, horizon_end_ns)) return false;
    if (!governor->config.prediction_enabled || !policy.prediction_enabled)
        *forecast = governor->latest;
    return true;
}

static bool ledger_snapshot(const Ledger *ledger,
                            LedgerDimension out[LEDGER_DIMENSION_COUNT])
{
    int id;
    for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id)
        if (!ledger_query(ledger, (LedgerDimensionId)id, &out[id])) return false;
    return true;
}

static DecisionReason phase_reason(ExecutionPhase phase)
{
    if (phase == EXECUTION_RUNNING) return DECISION_REASON_ALREADY_RUNNING;
    if (phase == EXECUTION_PAUSED) return DECISION_REASON_PAUSED;
    if (phase == EXECUTION_COMPLETE) return DECISION_REASON_COMPLETED;
    if (phase == EXECUTION_REJECTED) return DECISION_REASON_REJECTED;
    return DECISION_REASON_DEFERRED;
}

static bool select_task(Governor *governor, GovernorPlan *shadow,
                        PredictionTracker *tracker, size_t index,
                        uint64_t now_ns, bool permit_commit,
                        LogRecord *record)
{
    Task task_view = governor->scenario->tasks.items[index];
    const Task *task = &task_view;
    TelemetryFrame forecast;
    uint64_t horizon_end;
    Candidate candidates[SAGE_MAX_CANDIDATES];
    Candidate enabled[SAGE_MAX_CANDIDATES];
    size_t enabled_count = 0;
    size_t evaluation_index = 0;
    GovernorVariantPolicy policy;
    size_t count;
    GateContext gate = {0};
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    UtilityContext utility = {0};
    SelectionResult selected = {0};
    LedgerDimension dimension;
    int i;
    if (!governor_variant_policy(governor->config.variant, &policy)) return false;
    if (shadow->execution[index].phase == EXECUTION_PAUSED)
        task_view.nominal_exec_ns = shadow->execution[index].remaining_ns;
    if (!predicted_for_task(governor, task, now_ns, &forecast, &horizon_end) ||
        forecast.battery_soc_mV_pct.value < 0 ||
        forecast.battery_soc_mV_pct.value > 1000) return false;
    if (policy.prediction_enabled && !prediction_tracker_add(tracker, task->task_id,
                                 now_ns, horizon_end, &forecast)) return false;
    count = mode_generator_generate(task, &forecast, candidates, SAGE_MAX_CANDIDATES);
    if (count == 0) return false;
    gate.telemetry = &forecast;
    gate.ledger = &shadow->ledger;
    gate.ignored_ledger_dimensions = (uint8_t)(LEDGER_MASK_ALL & ~policy.ledger_mask);
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
    for (i = 0; i < (int)count; ++i)
        if ((policy.mode_mask & (1U << (unsigned int)candidates[i].mode)) != 0U)
            enabled[enabled_count++] = candidates[i];
    survivors = gate_filter_masked(enabled, enabled_count, task, &gate,
                                   policy.gate_mask, &filtered);
    if (survivors == NULL) return false;
    record->has_candidates = true;
    for (i = 0; i < (int)count; ++i) {
        CandidateLogEntry *entry = &record->candidates[i];
        entry->mode = candidates[i].mode;
        entry->skip_mask = (uint8_t)(GATE_MASK_ALL & ~policy.gate_mask);
        entry->failed_predicate = GATE_PREDICATE_NONE;
        if ((policy.mode_mask & (1U << (unsigned int)candidates[i].mode)) == 0U) {
            entry->verdict = CANDIDATE_VERDICT_DISABLED_BY_VARIANT;
            continue;
        }
        if (candidates[i].mode >= MODE_DEFERRED) {
            entry->verdict = CANDIDATE_VERDICT_FALLBACK_ONLY;
            entry->failed_predicate = GATE_PREDICATE_NONE;
        } else {
            entry->verdict = filtered.evaluations[evaluation_index].passed ?
                CANDIDATE_VERDICT_PASS : CANDIDATE_VERDICT_ELIMINATED;
            entry->failed_predicate = filtered.evaluations[evaluation_index].failed_predicate;
        }
        ++evaluation_index;
    }
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
    if (!utility_select_masked(survivors, task, &utility,
                               policy.utility_term_mask, &selected)) return false;
    if (selected.outcome == SELECTION_EMERGENCY_SAFE_MODE_REQUESTED) {
        shadow->emergency_safe_mode = true;
        shadow->emergency_task_id = task->task_id;
        record->reason = DECISION_REASON_EMERGENCY_SAFE_MODE;
    } else if (selected.outcome == SELECTION_EXECUTING && permit_commit) {
        Commitment *entry;
        size_t slot;
        for (slot = 0; slot < shadow->commitment_count; ++slot)
            if (shadow->commitments[slot].task_id == task->task_id) break;
        if (ledger_has_reservation(&shadow->ledger, task->task_id)) {
            record->reason = DECISION_REASON_RESTORE_BLOCKED_BUDGET;
            return true;
        }
        if ((slot == shadow->commitment_count &&
             shadow->commitment_count >= SAGE_MAX_TASKS) ||
            !ledger_commit_masked(&shadow->ledger, task->task_id, task->criticality_class,
                selected.selected.predicted_cost.resource_demand,
                selected.selected.predicted_cost.energy_cost_mJ,
                policy.ledger_mask)) return false;
        entry = &shadow->commitments[slot];
        if (slot == shadow->commitment_count) ++shadow->commitment_count;
        entry->task_id = task->task_id;
        entry->mode = selected.selected.mode;
        entry->assumption_snapshot.soc_permille = forecast.battery_soc_mV_pct.value;
        for (i = 0; i < N_SUBSYSTEMS; ++i)
            entry->assumption_snapshot.temp_mC[i] = forecast.temp_mC[i].value;
        entry->assumption_snapshot.current_draw_mA = forecast.current_draw_mA.value;
        entry->assumption_snapshot.forecast_horizon_end_ns = horizon_end;
        entry->commit_time_ns = now_ns;
        shadow->revalidation[index].observed_baseline.soc_permille =
            governor->latest.battery_soc_mV_pct.value;
        for (i = 0; i < N_SUBSYSTEMS; ++i)
            shadow->revalidation[index].observed_baseline.temp_mC[i] =
                governor->latest.temp_mC[i].value;
        shadow->revalidation[index].observed_baseline.current_draw_mA =
            governor->latest.current_draw_mA.value;
        shadow->revalidation[index].has_observed_baseline = true;
        shadow->commitment_active[index] = true;
        shadow->pending[index] = false;
        record->has_selected = true;
        record->selected_mode = selected.selected.mode;
        record->has_score = true;
        record->score = selected.utility_score;
        record->reason = DECISION_REASON_SELECTED;
        if (shadow->execution[index].phase == EXECUTION_PENDING &&
            !execution_start(&shadow->execution[index], task,
                             selected.selected.mode, now_ns)) return false;
    } else if (selected.outcome == SELECTION_REJECTED && permit_commit) {
        shadow->pending[index] = false;
        shadow->execution[index].phase = EXECUTION_REJECTED;
        record->has_selected = true;
        record->selected_mode = MODE_REJECTED;
        record->reason = DECISION_REASON_REJECTED;
    } else if (selected.outcome == SELECTION_DEFERRED && permit_commit) {
        record->has_selected = true;
        record->selected_mode = MODE_DEFERRED;
        record->reason = DECISION_REASON_DEFERRED;
    } else if (!permit_commit) {
        record->reason = DECISION_REASON_PAUSED;
    }
    return true;
}

static bool capacity_violation(const Governor *governor,
                               const GovernorPlan *plan,
                               bool violated[LEDGER_DIMENSION_COUNT])
{
    uint64_t total[LEDGER_DIMENSION_COUNT] = {0};
    uint64_t ordinary[LEDGER_DIMENSION_COUNT] = {0};
    size_t i;
    int id;
    total[LEDGER_CPU] = (uint64_t)plan->ledger.self_cpu_pct;
    total[LEDGER_ENERGY] = (uint64_t)plan->ledger.self_energy_mJ;
    ordinary[LEDGER_CPU] = total[LEDGER_CPU];
    ordinary[LEDGER_ENERGY] = total[LEDGER_ENERGY];
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        const Task *task = &governor->scenario->tasks.items[i];
        const TaskModeProfile *mode;
        size_t slot;
        uint64_t demand[LEDGER_DIMENSION_COUNT];
        if (!plan->commitment_active[i]) continue;
        for (slot = 0; slot < plan->commitment_count; ++slot)
            if (plan->commitments[slot].task_id == task->task_id) break;
        if (slot == plan->commitment_count) return false;
        mode = &task->modes[plan->commitments[slot].mode];
        demand[LEDGER_CPU] = mode->resource_demand.cpu_pct;
        demand[LEDGER_ENERGY] = mode->energy_cost_mJ;
        demand[LEDGER_MEMORY] = mode->resource_demand.mem_KB;
        demand[LEDGER_STORAGE] = mode->resource_demand.storage_KB;
        demand[LEDGER_SENSOR] = mode->resource_demand.sensor_mask;
        demand[LEDGER_COMM] = mode->resource_demand.comm_pct;
        for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id) {
            if (id == LEDGER_SENSOR) {
                total[id] |= demand[id];
                if (task->criticality_class != 0) ordinary[id] |= demand[id];
            } else {
                if (demand[id] > UINT64_MAX - total[id]) return false;
                total[id] += demand[id];
                if (task->criticality_class != 0) {
                    if (demand[id] > UINT64_MAX - ordinary[id]) return false;
                    ordinary[id] += demand[id];
                }
            }
        }
    }
    for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id) {
        int64_t cap = plan->ledger.config.capacity[id];
        int64_t reserve = plan->ledger.config.protected_reserve[id];
        if (id == LEDGER_SENSOR)
            violated[id] = ((uint32_t)total[id] & ~(uint32_t)cap) != 0U ||
                ((uint32_t)ordinary[id] &
                 ((uint32_t)reserve | ~(uint32_t)cap)) != 0U;
        else
            violated[id] = cap < 0 || reserve < 0 ||
                total[id] > (uint64_t)cap ||
                ordinary[id] > (uint64_t)(cap > reserve ? cap - reserve : 0);
    }
    return true;
}

static bool task_contributes(const TaskModeProfile *mode,
                             const bool violated[LEDGER_DIMENSION_COUNT])
{
    return (violated[LEDGER_CPU] && mode->resource_demand.cpu_pct != 0) ||
        (violated[LEDGER_ENERGY] && mode->energy_cost_mJ != 0) ||
        (violated[LEDGER_MEMORY] && mode->resource_demand.mem_KB != 0) ||
        (violated[LEDGER_STORAGE] && mode->resource_demand.storage_KB != 0) ||
        (violated[LEDGER_SENSOR] && mode->resource_demand.sensor_mask != 0) ||
        (violated[LEDGER_COMM] && mode->resource_demand.comm_pct != 0);
}

static bool trim_overcommitted(Governor *governor, GovernorPlan *plan,
                               uint64_t now_ns, LogRecord *records,
                               bool *record_touched)
{
    size_t attempts;
    GovernorVariantPolicy policy;
    if (!governor_variant_policy(governor->config.variant, &policy)) return false;
    for (attempts = 0; attempts < governor->scenario->tasks.count; ++attempts) {
        bool violated[LEDGER_DIMENSION_COUNT] = {0};
        bool any = false;
        size_t victim = governor->scenario->tasks.count;
        size_t victim_slot = 0;
        size_t i;
        int id;
        if (!capacity_violation(governor, plan, violated)) return false;
        for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id) {
            if ((policy.ledger_mask & (1U << (unsigned)id)) == 0U)
                violated[id] = false;
            any |= violated[id];
        }
        if (!any) return true;
        for (i = 0; i < governor->scenario->tasks.count; ++i) {
            const Task *task = &governor->scenario->tasks.items[i];
            size_t slot;
            if (!plan->commitment_active[i]) continue;
            for (slot = 0; slot < plan->commitment_count; ++slot)
                if (plan->commitments[slot].task_id == task->task_id) break;
            if (slot == plan->commitment_count ||
                !task_contributes(&task->modes[plan->commitments[slot].mode],
                                  violated)) continue;
            if (victim == governor->scenario->tasks.count ||
                task->criticality_class >
                    governor->scenario->tasks.items[victim].criticality_class ||
                (task->criticality_class ==
                    governor->scenario->tasks.items[victim].criticality_class &&
                 (plan->commitments[slot].commit_time_ns >
                    plan->commitments[victim_slot].commit_time_ns ||
                  (plan->commitments[slot].commit_time_ns ==
                    plan->commitments[victim_slot].commit_time_ns &&
                   slot > victim_slot)))) {
                victim = i;
                victim_slot = slot;
            }
        }
        if (victim == governor->scenario->tasks.count) return true;
        if (!ledger_snapshot(&plan->ledger, records[victim].ledger_before))
            return false;
        if (!execution_pause(&plan->execution[victim],
                &governor->scenario->tasks.items[victim], now_ns)) return false;
        if (!revalidation_force_capacity_invalidation(&plan->revalidation[victim],
                &governor->scenario->tasks.items[victim],
                plan->commitments[victim_slot].mode, now_ns) ||
            !ledger_release_rate(&plan->ledger,
                governor->scenario->tasks.items[victim].task_id)) return false;
        (void)ledger_release_stock(&plan->ledger,
            governor->scenario->tasks.items[victim].task_id);
        plan->commitment_active[victim] = false;
        records[victim].has_invalidation = true;
        records[victim].invalidation = plan->revalidation[victim].record;
        records[victim].reason = DECISION_REASON_PAUSED;
        record_touched[victim] = true;
        if (!ledger_snapshot(&plan->ledger, records[victim].ledger_after))
            return false;
    }
    return true;
}

GovernorCycleResult governor_cycle(Governor *governor, uint64_t now_ns,
                                    bool fault_escalation)
{
    uint32_t mask = 0;
    if (fault_escalation && governor != NULL)
        mask = (uint32_t)governor->latest.fault_flags.value;
    return governor_cycle_with_fault_mask(governor, now_ns, mask);
}

GovernorCycleResult governor_cycle_with_fault_mask(Governor *governor,
                                                    uint64_t now_ns,
                                                    uint32_t fault_set_mask)
{
    GovernorPlan shadow;
    GovernorPlan before;
    PredictionTracker shadow_tracker;
    LogRecord records[SAGE_MAX_TASKS] = {0};
    bool record_touched[SAGE_MAX_TASKS] = {0};
    size_t i;
    size_t order[SAGE_MAX_TASKS];
    size_t processed = 0;
    uint64_t start_cpu;
    uint64_t end_cpu;
    uint16_t budget_soc;
    uint64_t harvest_horizon = UINT64_MAX;
    uint64_t harvest_mJ = 0;
    uint64_t prediction_cpu;
    size_t initial_commitment_count;
    GovernorVariantPolicy policy;
    if (governor == NULL || !governor->has_frame) return GOVERNOR_CYCLE_ERROR;
    if (!governor_variant_policy(governor->config.variant, &policy))
        return GOVERNOR_CYCLE_ERROR;
    if (!process_cpu_time_ns(&start_cpu)) return GOVERNOR_CYCLE_ERROR;
    prediction_cpu = governor->pending_prediction_cpu_ns;
    governor->pending_prediction_cpu_ns = 0;
    governor->last_prediction_wall_ns = governor->pending_prediction_wall_ns;
    governor->pending_prediction_wall_ns = 0;
    shadow = governor->plan;
    before = governor->plan;
    initial_commitment_count = shadow.commitment_count;
    shadow_tracker = governor->accuracy_tracker;
    shadow.emergency_safe_mode = false;
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        records[i].tick = now_ns / governor->scenario->step_ns;
        records[i].sim_time_ns = now_ns;
        records[i].trigger = fault_set_mask != 0U ?
            DECISION_TRIGGER_FAULT : DECISION_TRIGGER_TICK;
        records[i].task_id = governor->scenario->tasks.items[i].task_id;
        records[i].state_before = before.execution[i].phase;
        records[i].state_after = before.execution[i].phase;
        records[i].reason = phase_reason(before.execution[i].phase);
        if (!ledger_snapshot(&before.ledger, records[i].ledger_before) ||
            !ledger_snapshot(&before.ledger, records[i].ledger_after))
            return GOVERNOR_CYCLE_ERROR;
    }
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        bool completed = false;
        const Task *task = &governor->scenario->tasks.items[i];
        if (shadow.execution[i].phase != EXECUTION_RUNNING) continue;
        if (!execution_advance(&shadow.execution[i], task, now_ns,
                               &completed)) return GOVERNOR_CYCLE_ERROR;
        if (completed) {
            if (!ledger_release_rate(&shadow.ledger, task->task_id))
                return GOVERNOR_CYCLE_ERROR;
            (void)ledger_release_stock(&shadow.ledger, task->task_id);
            shadow.commitment_active[i] = false;
        }
    }
    if (!ledger_set_telemetry_capacities(&shadow.ledger,
            governor->latest.cpu_avail_pct.quality == TELEMETRY_QUALITY_OK &&
                governor->latest.cpu_avail_pct.value >= 0 &&
                governor->latest.cpu_avail_pct.value <= 100 ?
                (uint16_t)governor->latest.cpu_avail_pct.value : 0U,
            governor->latest.mem_avail_KB.quality == TELEMETRY_QUALITY_OK &&
                governor->latest.mem_avail_KB.value > 0 ?
                (uint64_t)governor->latest.mem_avail_KB.value : 0U,
            governor->latest.storage_avail_KB.quality == TELEMETRY_QUALITY_OK &&
                governor->latest.storage_avail_KB.value > 0 ?
                (uint64_t)governor->latest.storage_avail_KB.value : 0U,
            governor->latest.sensor_avail_mask.quality == TELEMETRY_QUALITY_OK &&
                governor->latest.sensor_avail_mask.value >= 0 &&
                governor->latest.sensor_avail_mask.value <= UINT32_MAX ?
                (uint32_t)governor->latest.sensor_avail_mask.value : 0U,
            governor->latest.comm_window_open.quality == TELEMETRY_QUALITY_OK &&
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
        if (shadow.pending[i] || shadow.commitment_active[i]) {
            uint64_t duration = governor->scenario->tasks.items[i].nominal_exec_ns;
            if (duration > UINT64_MAX - governor->config.lookahead_ns)
                return GOVERNOR_CYCLE_ERROR;
            duration += governor->config.lookahead_ns;
            if (duration < harvest_horizon) harvest_horizon = duration;
        }
    }
    if (governor->config.variant == GOVERNOR_VARIANT_GOMX_STYLE &&
        harvest_horizon != UINT64_MAX &&
        governor->latest.eclipse.quality == TELEMETRY_QUALITY_OK &&
        governor->latest.eclipse.value == 0 &&
        governor->latest.solar_input_mW.quality == TELEMETRY_QUALITY_OK &&
        governor->latest.solar_input_mW.value > 0) {
        uint64_t power = (uint64_t)governor->latest.solar_input_mW.value;
        if (harvest_horizon > UINT64_MAX / power) return GOVERNOR_CYCLE_ERROR;
        harvest_mJ = power * harvest_horizon / UINT64_C(1000000000);
    }
    if (governor->latest.battery_soc_mV_pct.quality == TELEMETRY_QUALITY_OK &&
        (!shadow.has_energy_soc_timestamp ||
         governor->latest.battery_soc_mV_pct.timestamp_ns >
             shadow.last_energy_soc_timestamp_ns)) {
        if (!ledger_update_energy(&shadow.ledger, budget_soc, harvest_mJ, 0))
            return GOVERNOR_CYCLE_ERROR;
        shadow.energy_refresh_epoch = governor->observation_epoch;
        shadow.last_energy_soc_timestamp_ns =
            governor->latest.battery_soc_mV_pct.timestamp_ns;
        shadow.has_energy_soc_timestamp = true;
    }
    if (!trim_overcommitted(governor, &shadow, now_ns, records,
                             record_touched))
        return GOVERNOR_CYCLE_ERROR;
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        size_t at = i;
        order[i] = i;
        while (at > 0) {
            const Task *left = &governor->scenario->tasks.items[order[at - 1]];
            const Task *right = &governor->scenario->tasks.items[order[at]];
            bool follows = left->criticality_class > right->criticality_class;
            if (left->criticality_class == right->criticality_class &&
                governor->config.variant == GOVERNOR_VARIANT_FIXED_EDF)
                follows = left->deadline_ns > right->deadline_ns ||
                    (left->deadline_ns == right->deadline_ns &&
                     left->task_id > right->task_id);
            if (!follows) break;
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
            if (!ledger_snapshot(&shadow.ledger,
                    records[index].ledger_before)) return GOVERNOR_CYCLE_ERROR;
            if (!select_task(governor, &shadow, &shadow_tracker, index,
                             now_ns, true, &records[index]) ||
                !process_cpu_time_ns(&end_cpu) || end_cpu < start_cpu)
                return GOVERNOR_CYCLE_ERROR;
            record_touched[index] = true;
            if (!ledger_snapshot(&shadow.ledger,
                    records[index].ledger_after)) return GOVERNOR_CYCLE_ERROR;
            if (shadow.emergency_safe_mode) break;
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
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        RevalidationInput check = {0};
        RevalidationEvent event;
        TelemetryFrame forecast;
        uint64_t horizon_end;
        size_t slot;
        int subsystem;
        const Task *task = &governor->scenario->tasks.items[i];
        if (shadow.execution[i].phase == EXECUTION_COMPLETE ||
            shadow.execution[i].phase == EXECUTION_REJECTED ||
            shadow.pending[i] ||
            (!shadow.commitment_active[i] && !shadow.revalidation[i].invalid))
            continue;
        for (slot = 0; slot < shadow.commitment_count; ++slot)
            if (shadow.commitments[slot].task_id == task->task_id) break;
        if (slot == shadow.commitment_count ||
            !predicted_for_task(governor, task, now_ns, &forecast,
                                &horizon_end)) return GOVERNOR_CYCLE_ERROR;
        check.task = task;
        check.mode = shadow.commitments[slot].mode;
        check.assumption_snapshot = &shadow.commitments[slot].assumption_snapshot;
        check.actual = &governor->latest;
        check.forecast = &forecast;
        check.ledger = &shadow.ledger;
        check.safety_margin_permille = governor->config.safety_margin_permille;
        check.now_ns = now_ns;
        check.forecast_horizon_end_ns = horizon_end;
        check.step_ns = governor->scenario->step_ns;
        check.observation_epoch = governor->observation_epoch;
        check.fault_set_mask = fault_set_mask;
        check.is_out_of_band = fault_set_mask != 0U;
        for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
            check.thermal_limit_mC[subsystem] =
                governor->config.thermal_limit_mC[subsystem];
        if (!record_touched[i] &&
            !ledger_snapshot(&shadow.ledger,
                records[i].ledger_before)) return GOVERNOR_CYCLE_ERROR;
        event = revalidation_step_masked(&shadow.revalidation[i], &check,
            policy.gate_mask, policy.ledger_mask, policy.immediate_fault_cycle);
        if (event == REVALIDATION_ERROR) return GOVERNOR_CYCLE_ERROR;
        if (event == REVALIDATION_INVALIDATE) {
            records[i].has_invalidation = true;
            records[i].invalidation = shadow.revalidation[i].record;
            if (!execution_pause(&shadow.execution[i], task, now_ns))
                return GOVERNOR_CYCLE_ERROR;
            if (!ledger_release_rate(&shadow.ledger, task->task_id))
                return GOVERNOR_CYCLE_ERROR;
            (void)ledger_release_stock(&shadow.ledger, task->task_id);
            shadow.commitment_active[i] = false;
            if (!select_task(governor, &shadow, &shadow_tracker, i,
                             now_ns, false, &records[i])) return GOVERNOR_CYCLE_ERROR;
        } else if (event == REVALIDATION_RESTORE_READY) {
            if (!select_task(governor, &shadow, &shadow_tracker, i,
                             now_ns, true, &records[i])) return GOVERNOR_CYCLE_ERROR;
            if (shadow.commitment_active[i] &&
                !revalidation_complete_restore(&shadow.revalidation[i],
                    now_ns, governor->scenario->step_ns))
                return GOVERNOR_CYCLE_ERROR;
            if (shadow.commitment_active[i] &&
                !execution_resume(&shadow.execution[i],
                    shadow.commitments[slot].mode, now_ns))
                return GOVERNOR_CYCLE_ERROR;
            if (shadow.commitment_active[i]) {
                records[i].restoration = true;
                records[i].has_invalidation = true;
                records[i].invalidation = shadow.revalidation[i].record;
            } else if (records[i].reason !=
                DECISION_REASON_RESTORE_BLOCKED_BUDGET) {
                records[i].reason = DECISION_REASON_RESTORE_BLOCKED_GATE;
                records[i].has_selected = false;
                records[i].has_score = false;
            }
        } else if (event == REVALIDATION_UPGRADE) {
            records[i].has_invalidation = true;
            records[i].invalidation = shadow.revalidation[i].record;
        }
        if (event != REVALIDATION_NONE) {
            record_touched[i] = true;
            if (!ledger_snapshot(&shadow.ledger,
                    records[i].ledger_after)) return GOVERNOR_CYCLE_ERROR;
        }
    }
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        const Task *task = &governor->scenario->tasks.items[i];
        if (task->criticality_class == 0 && shadow.revalidation[i].invalid &&
            !shadow.commitment_active[i]) {
            shadow.emergency_safe_mode = true;
            shadow.emergency_task_id = task->task_id;
            break;
        }
    }
    if (shadow.emergency_safe_mode) {
        size_t slot;
        for (i = 0; i < governor->scenario->tasks.count; ++i) {
            uint32_t task_id = governor->scenario->tasks.items[i].task_id;
            if (!shadow.commitment_active[i] || before.commitment_active[i])
                continue;
            if (!ledger_cancel_tentative(&shadow.ledger, task_id))
                return GOVERNOR_CYCLE_ERROR;
            shadow.pending[i] = before.pending[i];
            shadow.commitment_active[i] = false;
            shadow.revalidation[i] = before.revalidation[i];
            shadow.execution[i] = before.execution[i];
            records[i].has_selected = false;
            records[i].has_score = false;
            records[i].restoration = false;
            records[i].reason = DECISION_REASON_EMERGENCY_SAFE_MODE;
            record_touched[i] = true;
            if (!ledger_snapshot(&shadow.ledger,
                    records[i].ledger_after)) return GOVERNOR_CYCLE_ERROR;
            for (slot = 0; slot < shadow.commitment_count; ++slot)
                if (shadow.commitments[slot].task_id == task_id) {
                    if (slot < initial_commitment_count)
                        shadow.commitments[slot] = before.commitments[slot];
                    else shadow.commitments[slot] = (Commitment){0};
                    break;
                }
        }
        shadow.commitment_count = initial_commitment_count;
    }
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        ExecutionState *execution = &shadow.execution[i];
        records[i].state_after = execution->phase;
        records[i].remaining_ns = execution->remaining_ns;
        records[i].has_completed_ns = execution->phase == EXECUTION_COMPLETE;
        records[i].completed_ns = execution->completed_ns;
        records[i].delivered_value = execution->delivered_value;
        if (execution->phase == EXECUTION_COMPLETE)
            records[i].reason = DECISION_REASON_COMPLETED;
        else if (shadow.emergency_safe_mode &&
                 execution->phase == EXECUTION_PENDING &&
                 !records[i].has_candidates)
            records[i].reason = DECISION_REASON_EMERGENCY_SAFE_MODE;
        if (!record_touched[i] &&
            !ledger_snapshot(&shadow.ledger,
                records[i].ledger_after)) return GOVERNOR_CYCLE_ERROR;
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
    memcpy(governor->last_records, records,
           governor->scenario->tasks.count * sizeof(records[0]));
    governor->last_record_count = governor->scenario->tasks.count;
    return GOVERNOR_CYCLE_OK;
}

size_t governor_commitment_count(const Governor *governor)
{
    return governor == NULL ? 0 : governor->plan.commitment_count;
}

bool governor_commitment_active(const Governor *governor, size_t task_index)
{
    return governor != NULL && governor->scenario != NULL &&
        task_index < governor->scenario->tasks.count &&
        governor->plan.commitment_active[task_index];
}

const RevalidationState *governor_revalidation_state_at(const Governor *governor,
                                                        size_t task_index)
{
    return governor == NULL || governor->scenario == NULL ||
        task_index >= governor->scenario->tasks.count ? NULL :
        &governor->plan.revalidation[task_index];
}

const ExecutionState *governor_execution_at(const Governor *governor,
                                            size_t task_index)
{
    return governor == NULL || governor->scenario == NULL ||
        task_index >= governor->scenario->tasks.count ? NULL :
        &governor->plan.execution[task_index];
}

const LogRecord *governor_log_record_at(const Governor *governor,
                                        size_t index)
{
    return governor == NULL || index >= governor->last_record_count ? NULL :
        &governor->last_records[index];
}

bool governor_emergency_safe_mode(const Governor *governor)
{
    return governor != NULL && governor->plan.emergency_safe_mode;
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

static bool write_decision_records(FILE *output, const Governor *governor);

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
    if (result == GOVERNOR_CYCLE_OK) {
        if (!write_decision_records(output, governor)) return false;
    } else if (output != NULL &&
        fprintf(output, "OVERRUN sim_ns=%" PRIu64 " trigger=TICK\n",
                sim_time_ns) < 0) return false;
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

static bool write_decision_records(FILE *output, const Governor *governor)
{
    size_t i;
    if (output == NULL) return true;
    for (i = 0; i < governor->last_record_count; ++i)
        if (!logger_write(output, &governor->last_records[i])) return false;
    return true;
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
            GovernorVariantPolicy policy;
            if (!governor_variant_policy(governor->config.variant, &policy))
                return false;
            if (!policy.immediate_fault_cycle) continue;
            GovernorCycleResult result = governor_cycle_with_fault_mask(governor,
                    message.sim_time_ns, message.fault_set_mask);
            if (result == GOVERNOR_CYCLE_ERROR) return false;
            if (result == GOVERNOR_CYCLE_OK) {
                if (!write_decision_records(output, governor)) return false;
            } else {
                ++stats->overruns;
                if (output != NULL && fprintf(output,
                    "OVERRUN sim_ns=%" PRIu64 " trigger=FAULT\n",
                    message.sim_time_ns) < 0) return false;
            }
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
