#include "task_model.h"

#include <string.h>

void task_table_init(TaskTable *table)
{
    if (table != NULL) memset(table, 0, sizeof(*table));
}

const Task *task_table_find(const TaskTable *table, uint32_t task_id)
{
    size_t i;
    if (table == NULL) return NULL;
    for (i = 0; i < table->count; ++i) {
        if (table->items[i].task_id == task_id) return &table->items[i];
    }
    return NULL;
}

bool task_table_add(TaskTable *table, const Task *task)
{
    if (table == NULL || task == NULL || table->count >= SAGE_MAX_TASKS ||
        task_table_find(table, task->task_id) != NULL || task->period_ns == 0 ||
        task->deadline_ns == 0 || task->nominal_exec_ns == 0 ||
        task->mission_value > 1000 || task->primary_subsystem < SUBSYSTEM_BATTERY ||
        task->primary_subsystem >= N_SUBSYSTEMS) return false;
    table->items[table->count] = *task;
    memset(table->items[table->count].modes, 0,
           sizeof(table->items[table->count].modes));
    table->mode_masks[table->count] = 0;
    ++table->count;
    return true;
}

bool task_table_set_mode(TaskTable *table, uint32_t task_id,
                         TaskExecutingMode mode, TaskModeProfile profile)
{
    size_t i;
    unsigned bit;
    if (table == NULL || mode < TASK_MODE_FULL || mode >= TASK_EXECUTING_MODE_COUNT ||
        profile.value_fraction_permille > 1000 ||
        profile.resource_demand.cpu_pct > 100 ||
        profile.resource_demand.comm_pct > 100) return false;
    bit = 1U << (unsigned)mode;
    for (i = 0; i < table->count; ++i) {
        if (table->items[i].task_id == task_id) {
            if ((table->mode_masks[i] & bit) != 0) return false;
            table->items[i].modes[mode] = profile;
            table->mode_masks[i] |= bit;
            return true;
        }
    }
    return false;
}

bool task_table_complete(const TaskTable *table)
{
    size_t i;
    if (table == NULL) return false;
    for (i = 0; i < table->count; ++i) {
        if (table->mode_masks[i] != 7U) return false;
    }
    return true;
}
