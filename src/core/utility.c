#include "utility.h"

#include <string.h>

static double clamp_ratio(uint64_t numerator, int64_t denominator)
{
    double ratio;
    if (numerator == 0) return 0.0;
    if (denominator <= 0) return 1.0;
    ratio = (double)numerator / (double)denominator;
    return ratio > 1.0 ? 1.0 : ratio;
}

static double thermal_ratio(int32_t effect, int64_t margin)
{
    if (effect <= 0) return 0.0;
    return clamp_ratio((uint64_t)effect, margin);
}

static double candidate_score(const Candidate *candidate, const Task *task,
                              const UtilityContext *context, uint8_t mask)
{
    uint64_t deadline_ceiling = context->time_remaining_to_deadline_ns == 0 ?
        1 : context->time_remaining_to_deadline_ns;
    double deadline_urgency = (double)task->nominal_exec_ns /
        (double)deadline_ceiling;
    if (deadline_urgency > 1.0) deadline_urgency = 1.0;
    double fault_urgency = (context->fault_flags &
        (UINT32_C(1) << (unsigned)task->primary_subsystem)) != 0 ? 1.0 : 0.0;
    double energy_cost = clamp_ratio(candidate->predicted_cost.energy_cost_mJ,
                                      context->energy_capacity_mJ);
    double thermal_risk = thermal_ratio(candidate->predicted_cost.thermal_effect_mC,
        context->remaining_thermal_margin_mC[task->primary_subsystem]);
    const ResourceDemand *demand = &candidate->predicted_cost.resource_demand;
    double pressure = clamp_ratio(demand->cpu_pct, context->free_cpu_pct);
    double part = clamp_ratio(demand->mem_KB, context->free_mem_KB);
    if (part > pressure) pressure = part;
    part = clamp_ratio(demand->storage_KB, context->free_storage_KB);
    if (part > pressure) pressure = part;
    part = clamp_ratio(demand->comm_pct, context->free_comm_pct);
    if (part > pressure) pressure = part;
    if ((mask & UTILITY_TERM_FAULT) == 0U) fault_urgency = 0.0;
    if ((mask & UTILITY_TERM_DEADLINE) == 0U) deadline_urgency = 0.0;
    if ((mask & UTILITY_TERM_ENERGY) == 0U) energy_cost = 0.0;
    if ((mask & UTILITY_TERM_THERMAL) == 0U) thermal_risk = 0.0;
    if ((mask & UTILITY_TERM_RESOURCE) == 0U) pressure = 0.0;
    return (1.0 + fault_urgency + deadline_urgency) *
        (double)candidate->delivered_value - (energy_cost + thermal_risk + pressure);
}

static bool rank_masked(const SurvivorSet *survivors, const Task *task,
                  const UtilityContext *context, uint8_t mask, SelectionResult *out)
{
    size_t count;
    size_t i;
    bool found = false;
    if (survivors == NULL || !gate_survivor_valid(survivors) ||
        task == NULL || context == NULL || out == NULL ||
        task->primary_subsystem < SUBSYSTEM_BATTERY ||
        task->primary_subsystem >= N_SUBSYSTEMS) return false;
    count = gate_survivor_count(survivors);
    for (i = 0; i < count; ++i) {
        Candidate candidate;
        double score;
        if (!gate_survivor_at(survivors, i, &candidate) ||
            candidate.mode > MODE_DEGRADED || candidate.task_id != task->task_id)
            return false;
        score = candidate_score(&candidate, task, context, mask);
        if (!found || score > out->utility_score ||
            (score == out->utility_score && candidate.mode < out->selected.mode)) {
            out->outcome = SELECTION_EXECUTING;
            out->selected = candidate;
            out->utility_score = score;
            found = true;
        }
    }
    return found;
}

bool utility_select(const SurvivorSet *survivors, const Task *task,
                    const UtilityContext *context, SelectionResult *out)
{
    return utility_select_masked(survivors, task, context, UTILITY_TERM_ALL, out);
}

bool utility_rank(const SurvivorSet *survivors, const Task *task,
                  const UtilityContext *context, SelectionResult *out)
{
    return rank_masked(survivors, task, context, UTILITY_TERM_ALL, out);
}

bool utility_select_masked(const SurvivorSet *survivors, const Task *task,
    const UtilityContext *context, uint8_t term_mask, SelectionResult *out)
{
    bool future_window;
    if (survivors == NULL || !gate_survivor_valid(survivors) ||
        task == NULL || context == NULL || out == NULL ||
        (term_mask & ~UTILITY_TERM_ALL) != 0)
        return false;
    if (gate_survivor_count(survivors) > 0)
        return rank_masked(survivors, task, context, term_mask, out);
    memset(out, 0, sizeof(*out));
    out->selected.task_id = task->task_id;
    if (task->criticality_class == 0) {
        out->outcome = SELECTION_EMERGENCY_SAFE_MODE_REQUESTED;
        return true;
    }
    future_window = context->time_until_next_tick_ns <=
        context->time_remaining_to_deadline_ns &&
        task->nominal_exec_ns <= context->time_remaining_to_deadline_ns -
        context->time_until_next_tick_ns;
    out->outcome = future_window ? SELECTION_DEFERRED : SELECTION_REJECTED;
    out->selected.mode = future_window ? MODE_DEFERRED : MODE_REJECTED;
    return true;
}
