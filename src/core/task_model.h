#ifndef SAGE_TASK_MODEL_H
#define SAGE_TASK_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SAGE_MAX_TASKS 32

typedef enum {
    TASK_MODE_FULL = 0,
    TASK_MODE_THROTTLED = 1,
    TASK_MODE_DEGRADED = 2,
    TASK_EXECUTING_MODE_COUNT = 3
} TaskExecutingMode;

typedef struct {
    uint16_t cpu_pct;
    uint32_t mem_KB;
    uint32_t storage_KB;
    uint32_t sensor_mask;
    uint16_t comm_pct;
} ResourceDemand;

typedef struct {
    uint64_t energy_cost_mJ;
    int32_t thermal_effect_mC;
    uint16_t value_fraction_permille;
    ResourceDemand resource_demand;
} TaskModeProfile;

typedef struct {
    uint32_t task_id;
    uint64_t period_ns;
    uint64_t deadline_ns;
    uint64_t nominal_exec_ns;
    uint8_t criticality_class;
    uint16_t mission_value;
    TaskModeProfile modes[TASK_EXECUTING_MODE_COUNT];
} Task;

typedef struct {
    Task items[SAGE_MAX_TASKS];
    unsigned mode_masks[SAGE_MAX_TASKS];
    size_t count;
} TaskTable;

void task_table_init(TaskTable *table);
bool task_table_add(TaskTable *table, const Task *task);
bool task_table_set_mode(TaskTable *table, uint32_t task_id,
                         TaskExecutingMode mode, TaskModeProfile profile);
bool task_table_complete(const TaskTable *table);
const Task *task_table_find(const TaskTable *table, uint32_t task_id);

#endif
