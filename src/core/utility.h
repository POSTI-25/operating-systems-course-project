#ifndef SAGE_UTILITY_H
#define SAGE_UTILITY_H

#include "gate.h"

#define UTILITY_TERM_FAULT UINT8_C(1)
#define UTILITY_TERM_DEADLINE UINT8_C(2)
#define UTILITY_TERM_ENERGY UINT8_C(4)
#define UTILITY_TERM_THERMAL UINT8_C(8)
#define UTILITY_TERM_RESOURCE UINT8_C(16)
#define UTILITY_TERM_ALL UINT8_C(31)

typedef struct {
    uint64_t time_remaining_to_deadline_ns;
    uint64_t time_until_next_tick_ns;
    uint32_t fault_flags;
    int64_t remaining_thermal_margin_mC[N_SUBSYSTEMS];
    int64_t energy_capacity_mJ;
    int64_t free_cpu_pct;
    int64_t free_mem_KB;
    int64_t free_storage_KB;
    int64_t free_comm_pct;
} UtilityContext;

typedef enum {
    SELECTION_EXECUTING = 0,
    SELECTION_DEFERRED = 1,
    SELECTION_REJECTED = 2,
    SELECTION_EMERGENCY_SAFE_MODE_REQUESTED = 3
} SelectionOutcome;

typedef struct {
    SelectionOutcome outcome;
    Candidate selected;
    double utility_score;
} SelectionResult;

bool utility_rank(const SurvivorSet *survivors, const Task *task,
                  const UtilityContext *context, SelectionResult *out);
bool utility_select(const SurvivorSet *survivors, const Task *task,
                    const UtilityContext *context, SelectionResult *out);
bool utility_select_masked(const SurvivorSet *survivors, const Task *task,
    const UtilityContext *context, uint8_t term_mask, SelectionResult *out);

#endif
