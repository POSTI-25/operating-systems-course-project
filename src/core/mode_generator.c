#include "mode_generator.h"

#include <string.h>

size_t mode_generator_generate(const Task *task, const TelemetryFrame *snapshot,
                               Candidate *out, size_t capacity)
{
    size_t i;
    if (task == NULL || snapshot == NULL || out == NULL ||
        capacity < SAGE_MAX_CANDIDATES || task->mission_value > 1000 ||
        task->primary_subsystem < SUBSYSTEM_BATTERY ||
        task->primary_subsystem >= N_SUBSYSTEMS) return 0;
    for (i = 0; i < TASK_EXECUTING_MODE_COUNT; ++i) {
        const TaskModeProfile *profile = &task->modes[i];
        if (profile->value_fraction_permille > 1000) return 0;
    }
    memset(out, 0, SAGE_MAX_CANDIDATES * sizeof(*out));
    for (i = 0; i < TASK_EXECUTING_MODE_COUNT; ++i) {
        const TaskModeProfile *profile = &task->modes[i];
        out[i].task_id = task->task_id;
        out[i].mode = (Mode)i;
        out[i].predicted_cost.energy_cost_mJ = profile->energy_cost_mJ;
        out[i].predicted_cost.thermal_effect_mC = profile->thermal_effect_mC;
        out[i].predicted_cost.resource_demand = profile->resource_demand;
        out[i].delivered_value = (uint32_t)task->mission_value *
            (uint32_t)profile->value_fraction_permille / 1000U;
    }
    out[3].task_id = task->task_id;
    out[3].mode = MODE_DEFERRED;
    out[4].task_id = task->task_id;
    out[4].mode = MODE_REJECTED;
    return SAGE_MAX_CANDIDATES;
}
