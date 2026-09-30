#include "phase5_report.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

static bool add(uint64_t *value, uint64_t amount)
{
    if (amount > UINT64_MAX - *value) return false;
    *value += amount;
    return true;
}

void phase5_metrics_init(Phase5Metrics *metrics, const Scenario *scenario)
{
    memset(metrics, 0, sizeof(*metrics));
    metrics->energy_survival_time_ns = scenario->duration_ns;
    metrics->energy_survival_censored = true;
}

static uint64_t capacity(TelemetryChannel channel)
{
    return channel.quality == TELEMETRY_QUALITY_OK && channel.value > 0 ?
        (uint64_t)channel.value : 0;
}

static bool task_after(const Task *left, const Task *right)
{
    if (left->criticality_class != right->criticality_class)
        return left->criticality_class > right->criticality_class;
    if (left->deadline_ns != right->deadline_ns)
        return left->deadline_ns > right->deadline_ns;
    return left->task_id > right->task_id;
}

bool phase5_audit_instant(Phase5Metrics *metrics, const Scenario *scenario,
    const Governor *governor, const TelemetryFrame *actual, uint64_t now_ns)
{
    uint64_t caps[LEDGER_DIMENSION_COUNT] = {0};
    uint64_t total[LEDGER_DIMENSION_COUNT] = {0};
    uint64_t ordinary[LEDGER_DIMENSION_COUNT] = {0};
    size_t order[SAGE_MAX_TASKS];
    size_t i;
    int id;
    uint64_t physical_energy = 0;
    if (metrics == NULL || scenario == NULL || governor == NULL || actual == NULL ||
        scenario->tasks.count > SAGE_MAX_TASKS ||
        (metrics->has_audit && now_ns < metrics->last_audit_ns)) return false;
    if (metrics->has_audit && now_ns == metrics->last_audit_ns) return true;
    if (actual->battery_soc_mV_pct.quality == TELEMETRY_QUALITY_OK &&
        actual->battery_soc_mV_pct.value >= 0 &&
        actual->battery_soc_mV_pct.value <= 1000 &&
        actual->battery_soc_mV_pct.value >= scenario->soc_floor_permille) {
        uint64_t delta = (uint64_t)actual->battery_soc_mV_pct.value -
            scenario->soc_floor_permille;
        physical_energy = scenario->battery_capacity_mJ / 1000U * delta +
            scenario->battery_capacity_mJ % 1000U * delta / 1000U;
    }
    if (metrics->energy_survival_censored &&
        actual->battery_soc_mV_pct.quality == TELEMETRY_QUALITY_OK &&
        actual->battery_soc_mV_pct.value < scenario->soc_floor_permille) {
        metrics->energy_survival_time_ns = now_ns;
        metrics->energy_survival_censored = false;
    }
    caps[LEDGER_CPU] = capacity(actual->cpu_avail_pct);
    caps[LEDGER_ENERGY] = physical_energy;
    caps[LEDGER_MEMORY] = capacity(actual->mem_avail_KB);
    caps[LEDGER_STORAGE] = capacity(actual->storage_avail_KB);
    caps[LEDGER_SENSOR] = capacity(actual->sensor_avail_mask);
    caps[LEDGER_COMM] = capacity(actual->comm_window_open) != 0 ? 100U : 0U;
    total[LEDGER_CPU] = (uint64_t)governor->plan.ledger.self_cpu_pct;
    total[LEDGER_ENERGY] = (uint64_t)governor->plan.ledger.self_energy_mJ;
    ordinary[LEDGER_CPU] = total[LEDGER_CPU];
    ordinary[LEDGER_ENERGY] = total[LEDGER_ENERGY];
    for (i = 0; i < scenario->tasks.count; ++i) {
        size_t at = i;
        order[i] = i;
        while (at > 0 && task_after(&scenario->tasks.items[order[at - 1]],
                                    &scenario->tasks.items[order[at]])) {
            size_t swap = order[at - 1];
            order[at - 1] = order[at];
            order[at--] = swap;
        }
    }
    for (i = 0; i < scenario->tasks.count; ++i) {
        size_t index = order[i];
        const Task *task = &scenario->tasks.items[index];
        const ExecutionState *execution = &governor->plan.execution[index];
        const TaskModeProfile *mode;
        uint64_t demand[LEDGER_DIMENSION_COUNT];
        uint64_t failures = 0;
        uint64_t threshold = (uint64_t)scenario->soc_floor_permille +
            governor->config.safety_margin_permille;
        uint64_t safe_energy = 0;
        bool residual = false;
        int64_t temp = actual->temp_mC[task->primary_subsystem].value;
        int64_t limit = governor->config.thermal_limit_mC[task->primary_subsystem];
        if (execution->phase != EXECUTION_RUNNING) continue;
        mode = &task->modes[execution->mode];
        if (!add(&metrics->safety_task_instants, 1)) return false;
        if (actual->battery_soc_mV_pct.value >= 0 &&
            (uint64_t)actual->battery_soc_mV_pct.value >= threshold && threshold <= 1000) {
            uint64_t delta = (uint64_t)actual->battery_soc_mV_pct.value - threshold;
            safe_energy = scenario->battery_capacity_mJ / 1000U * delta +
                scenario->battery_capacity_mJ % 1000U * delta / 1000U;
        }
        if (actual->battery_soc_mV_pct.quality != TELEMETRY_QUALITY_OK ||
            actual->battery_soc_mV_pct.value < 0 ||
            actual->battery_soc_mV_pct.value > 1000 ||
            (uint64_t)actual->battery_soc_mV_pct.value < threshold ||
            mode->energy_cost_mJ > safe_energy) ++failures;
        if (actual->temp_mC[task->primary_subsystem].quality != TELEMETRY_QUALITY_OK ||
            temp >= limit || (mode->thermal_effect_mC > 0 &&
            temp > limit - (int64_t)mode->thermal_effect_mC)) ++failures;
        if (now_ns > task->deadline_ns || execution->remaining_ns >
            task->deadline_ns - now_ns) ++failures;
        demand[LEDGER_CPU] = mode->resource_demand.cpu_pct;
        demand[LEDGER_ENERGY] = mode->energy_cost_mJ;
        demand[LEDGER_MEMORY] = mode->resource_demand.mem_KB;
        demand[LEDGER_STORAGE] = mode->resource_demand.storage_KB;
        demand[LEDGER_SENSOR] = mode->resource_demand.sensor_mask;
        demand[LEDGER_COMM] = mode->resource_demand.comm_pct;
        for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id) {
            uint64_t reserve = (uint64_t)governor->config.protected_reserve[id];
            if (id == LEDGER_SENSOR) {
                if ((total[id] & demand[id]) != 0U) residual = true;
                total[id] |= demand[id];
                if (task->criticality_class != 0) ordinary[id] |= demand[id];
                if ((total[id] & ~caps[id]) != 0U ||
                    (ordinary[id] & reserve) != 0U) residual = true;
            } else {
                if (!add(&total[id], demand[id])) return false;
                if (task->criticality_class != 0 &&
                    !add(&ordinary[id], demand[id])) return false;
                if (total[id] > caps[id] || ordinary[id] >
                    (caps[id] > reserve ? caps[id] - reserve : 0)) residual = true;
            }
        }
        if (residual) {
            ++failures;
            if (!add(&metrics->protected_resource_violations, 1)) return false;
        }
        if (!add(&metrics->safety_violations, failures)) return false;
    }
    metrics->has_audit = true;
    metrics->last_audit_ns = now_ns;
    return true;
}

bool phase5_note_fault(Phase5Metrics *metrics, uint64_t now_ns, uint32_t mask)
{
    int subsystem;
    if (metrics == NULL || (mask & ~UINT32_C(15)) != 0U) return false;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        if ((mask & (1U << (unsigned)subsystem)) != 0U &&
            !metrics->fault_pending[subsystem]) {
            metrics->fault_pending[subsystem] = true;
            metrics->fault_time_ns[subsystem] = now_ns;
        }
    return true;
}

bool phase5_note_due_faults(Phase5Metrics *metrics, const Scenario *scenario,
    uint64_t now_ns, bool seen[SAGE_MAX_FAULTS])
{
    size_t fault;
    if (metrics == NULL || scenario == NULL || seen == NULL ||
        scenario->fault_count > SAGE_MAX_FAULTS) return false;
    for (;;) {
        size_t next = scenario->fault_count;
        for (fault = 0; fault < scenario->fault_count; ++fault)
            if (!seen[fault] && scenario->faults[fault].time_ns <= now_ns &&
                (next == scenario->fault_count ||
                 scenario->faults[fault].time_ns < scenario->faults[next].time_ns))
                next = fault;
        if (next == scenario->fault_count) return true;
        if (!phase5_note_fault(metrics, scenario->faults[next].time_ns,
                               scenario->faults[next].set_mask)) return false;
        seen[next] = true;
    }
}

static uint64_t magnitude(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

bool phase5_note_cycle(Phase5Metrics *metrics, const Scenario *scenario,
    const Governor *governor, uint64_t now_ns, GovernorCycleResult result)
{
    size_t i;
    bool emergency = governor->plan.emergency_safe_mode && !metrics->previous_emergency;
    if (!add(&metrics->cpu_time_ns, governor->last_cycle_cpu_ns)) return false;
    metrics->self_cost_cap_ns = governor->config.self_cost_cap_ns;
    if (emergency && !add(&metrics->replan_count, 1)) return false;
    if (result == GOVERNOR_CYCLE_ERROR) return false;
    for (i = 0; result == GOVERNOR_CYCLE_OK &&
                i < governor->last_record_count; ++i) {
        const LogRecord *record = governor_log_record_at(governor, i);
        size_t index;
        bool invalidated = record->has_invalidation &&
            record->state_before == EXECUTION_RUNNING &&
            record->state_after == EXECUTION_PAUSED;
        bool transitioned = invalidated || record->restoration || emergency;
        if ((invalidated || record->restoration) &&
            !add(&metrics->replan_count, 1)) return false;
        if (!transitioned) continue;
        for (index = 0; index < scenario->tasks.count; ++index)
            if (scenario->tasks.items[index].task_id == record->task_id) break;
        if (index < scenario->tasks.count) {
            Subsystem subsystem = scenario->tasks.items[index].primary_subsystem;
            if (metrics->fault_pending[subsystem]) {
                uint64_t latency;
                if (now_ns < metrics->fault_time_ns[subsystem]) return false;
                latency = now_ns - metrics->fault_time_ns[subsystem];
                if (latency > metrics->recovery_latency_ns)
                    metrics->recovery_latency_ns = latency;
                metrics->fault_pending[subsystem] = false;
            }
        }
    }
    metrics->previous_emergency = governor->plan.emergency_safe_mode;
    if (!metrics->has_accuracy_epoch || metrics->accuracy_epoch != governor->observation_epoch) {
        for (i = 0; i < governor_accuracy_count(governor); ++i) {
            const PredictionAccuracyEvent *event = governor_accuracy_at(governor, i);
            int subsystem;
            if (!add(&metrics->prediction_abs_error, magnitude(event->soc_error_permille)) ||
                !add(&metrics->prediction_abs_error, magnitude(event->draw_error_mA)))
                return false;
            for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
                if (!add(&metrics->prediction_abs_error,
                         magnitude(event->temp_error_mC[subsystem]))) return false;
            if (!add(&metrics->prediction_samples, 1)) return false;
        }
        metrics->accuracy_epoch = governor->observation_epoch;
        metrics->has_accuracy_epoch = true;
    }
    return true;
}

bool phase5_metrics_finish(Phase5Metrics *metrics, const Scenario *scenario,
    const Governor *governor)
{
    size_t i;
    int subsystem;
    metrics->task_count = scenario->tasks.count;
    for (i = 0; i < scenario->tasks.count; ++i) {
        const Task *task = &scenario->tasks.items[i];
        const ExecutionState *execution = governor_execution_at(governor, i);
        if (task->criticality_class == 0) ++metrics->critical_task_count;
        if (execution->phase == EXECUTION_COMPLETE) {
            ++metrics->completed_count;
            if (task->criticality_class == 0) ++metrics->critical_completed_count;
            if (!add(&metrics->mission_work_completed, execution->delivered_value)) return false;
        }
        if (execution->phase == EXECUTION_REJECTED) ++metrics->rejected_by_design;
        else if (task->deadline_ns <= scenario->duration_ns &&
            (execution->phase != EXECUTION_COMPLETE || execution->completed_ns > task->deadline_ns))
            ++metrics->deadline_misses;
    }
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        if (metrics->fault_pending[subsystem]) {
            uint64_t latency;
            if (scenario->duration_ns < metrics->fault_time_ns[subsystem]) return false;
            latency = scenario->duration_ns - metrics->fault_time_ns[subsystem];
            if (latency > metrics->recovery_latency_ns) metrics->recovery_latency_ns = latency;
            metrics->recovery_latency_censored = true;
        }
    metrics->modeled_self_energy_mJ = (double)metrics->cpu_time_ns * 2.0 / 1000000.0;
    return true;
}

bool phase5_metrics_write_header(FILE *output)
{
    return fputs("scenario,variant,task_count,critical_task_count,completed_count,critical_completed_count,deadline_misses,rejected_by_design,mission_work_completed,completion_ratio,critical_completion_ratio,safety_violations,safety_task_instants,safety_violation_rate,protected_resource_violations,energy_survival_time_ns,energy_survival_censored,recovery_latency_ns,recovery_latency_censored,replan_count,cpu_time_ns,self_cost_cap_ns,modeled_self_energy_mJ,tick_jitter_ns,prediction_abs_error\n", output) >= 0;
}

static bool ratio(FILE *output, uint64_t numerator, uint64_t denominator)
{
    return denominator == 0 ? fputs("NA,", output) >= 0 :
        fprintf(output, "%.6f,", (double)numerator / (double)denominator) >= 0;
}

bool phase5_metrics_write_csv(FILE *output, const char *scenario,
    const char *variant, const Phase5Metrics *m)
{
    if (fprintf(output, "%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
        ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",", scenario, variant,
        m->task_count, m->critical_task_count, m->completed_count,
        m->critical_completed_count, m->deadline_misses, m->rejected_by_design,
        m->mission_work_completed) < 0 ||
        !ratio(output, m->completed_count, m->task_count) ||
        !ratio(output, m->critical_completed_count, m->critical_task_count) ||
        fprintf(output, "%" PRIu64 ",%" PRIu64 ",", m->safety_violations,
                m->safety_task_instants) < 0 ||
        !ratio(output, m->safety_violations, m->safety_task_instants) ||
        fprintf(output, "%" PRIu64 ",%" PRIu64 ",%u,%" PRIu64 ",%u,%" PRIu64
            ",%" PRIu64 ",%" PRIu64 ",%.6f,NA,", m->protected_resource_violations,
            m->energy_survival_time_ns, (unsigned)m->energy_survival_censored,
            m->recovery_latency_ns, (unsigned)m->recovery_latency_censored,
            m->replan_count, m->cpu_time_ns, m->self_cost_cap_ns,
            m->modeled_self_energy_mJ) < 0) return false;
    return m->prediction_samples == 0 ? fputs("NA\n", output) >= 0 :
        fprintf(output, "%" PRIu64 "\n", m->prediction_abs_error) >= 0;
}
