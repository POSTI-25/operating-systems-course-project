#include "gate.h"

#include <string.h>

#define SURVIVOR_MAGIC UINT32_C(0x53414745)

struct SurvivorSet {
    uint32_t magic;
    size_t count;
    Candidate items[TASK_EXECUTING_MODE_COUNT];
    uint64_t checksum;
};

_Static_assert(sizeof(struct SurvivorSet) <= GATE_SURVIVOR_STORAGE_BYTES,
               "Gate survivor storage is too small");

static uint64_t survivor_checksum(const struct SurvivorSet *survivors)
{
    const unsigned char *bytes = (const unsigned char *)(const void *)survivors;
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < offsetof(struct SurvivorSet, checksum); ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool survivor_valid(const struct SurvivorSet *survivors)
{
    return survivors->magic == SURVIVOR_MAGIC &&
        survivors->count <= TASK_EXECUTING_MODE_COUNT &&
        survivors->checksum == survivor_checksum(survivors);
}

static GateEvaluation failed(GatePredicate predicate)
{
    GateEvaluation result = { false, predicate };
    return result;
}

GateEvaluation gate_evaluate(const Candidate *candidate, const Task *task,
                             const GateContext *context)
{
    uint64_t physical_capacity;
    uint16_t floor;
    uint32_t threshold;
    uint32_t delta;
    uint64_t available_mJ;
    ResourceDemand demand;
    if (candidate == NULL || task == NULL || context == NULL ||
        context->telemetry == NULL || context->ledger == NULL ||
        candidate->task_id != task->task_id || task->primary_subsystem < SUBSYSTEM_BATTERY ||
        task->primary_subsystem >= N_SUBSYSTEMS ||
        !ledger_energy_config(context->ledger, &physical_capacity, &floor))
        return failed(GATE_INVALID_INPUT);
    threshold = (uint32_t)floor + (uint32_t)context->safety_margin_permille;
    if (threshold > 1000 || context->telemetry->battery_soc_mV_pct.value < 0 ||
        context->telemetry->battery_soc_mV_pct.value > 1000 ||
        (uint64_t)context->telemetry->battery_soc_mV_pct.value < threshold)
        return failed(GATE_SOC_FLOOR);
    delta = (uint32_t)context->telemetry->battery_soc_mV_pct.value - threshold;
    available_mJ = (physical_capacity / 1000U) * delta +
        ((physical_capacity % 1000U) * delta) / 1000U;
    if (candidate->predicted_cost.energy_cost_mJ > available_mJ)
        return failed(GATE_SOC_FLOOR);
    if ((int64_t)candidate->predicted_cost.thermal_effect_mC >
        context->remaining_thermal_margin_mC[task->primary_subsystem])
        return failed(GATE_THERMAL_CEILING);
    demand = candidate->predicted_cost.resource_demand;
    if (!ledger_can_reserve(context->ledger, task->criticality_class, demand,
                            candidate->predicted_cost.energy_cost_mJ))
        return failed(GATE_PROTECTED_RESIDUAL);
    if (context->predicted_completion_time_ns > context->absolute_deadline_ns)
        return failed(GATE_DEADLINE);
    return (GateEvaluation){ true, GATE_PREDICATE_NONE };
}

const SurvivorSet *gate_filter(const Candidate *candidates, size_t count,
                               const Task *task, const GateContext *context,
                               GateFilterResult *result)
{
    struct SurvivorSet survivors = {0};
    size_t i;
    if (candidates == NULL || task == NULL || context == NULL || result == NULL ||
        count > SAGE_MAX_CANDIDATES) return NULL;
    memset(result, 0, sizeof(*result));
    survivors.magic = SURVIVOR_MAGIC;
    result->candidate_count = count;
    for (i = 0; i < count; ++i) {
        result->evaluations[i] = gate_evaluate(&candidates[i], task, context);
        if (result->evaluations[i].passed && candidates[i].mode >= MODE_FULL &&
            candidates[i].mode <= MODE_DEGRADED) {
            if (survivors.count >= TASK_EXECUTING_MODE_COUNT) return NULL;
            survivors.items[survivors.count++] = candidates[i];
        }
    }
    survivors.checksum = survivor_checksum(&survivors);
    memcpy(result->survivor_storage, &survivors, sizeof(survivors));
    return (const SurvivorSet *)(const void *)result->survivor_storage;
}

size_t gate_survivor_count(const SurvivorSet *survivors)
{
    struct SurvivorSet contents;
    if (survivors == NULL) return 0;
    memcpy(&contents, (const void *)survivors, sizeof(contents));
    return survivor_valid(&contents) ? contents.count : 0;
}

bool gate_survivor_valid(const SurvivorSet *survivors)
{
    struct SurvivorSet contents;
    if (survivors == NULL) return false;
    memcpy(&contents, (const void *)survivors, sizeof(contents));
    return survivor_valid(&contents);
}

bool gate_survivor_at(const SurvivorSet *survivors, size_t index, Candidate *out)
{
    struct SurvivorSet contents;
    if (survivors == NULL || out == NULL) return false;
    memcpy(&contents, (const void *)survivors, sizeof(contents));
    if (!survivor_valid(&contents) || index >= contents.count) return false;
    *out = contents.items[index];
    return true;
}
