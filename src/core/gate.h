#ifndef SAGE_GATE_H
#define SAGE_GATE_H

#include "ledger.h"
#include "mode_generator.h"

#include <stddef.h>

#define GATE_MASK_SOC UINT8_C(1)
#define GATE_MASK_THERMAL UINT8_C(2)
#define GATE_MASK_PROTECTED UINT8_C(4)
#define GATE_MASK_DEADLINE UINT8_C(8)
#define GATE_MASK_ALL UINT8_C(15)

typedef enum {
    GATE_PREDICATE_NONE = 0,
    GATE_SOC_FLOOR = 1,
    GATE_THERMAL_CEILING = 2,
    GATE_PROTECTED_RESIDUAL = 3,
    GATE_DEADLINE = 4,
    GATE_INVALID_INPUT = 5
} GatePredicate;

typedef struct {
    bool passed;
    GatePredicate failed_predicate;
} GateEvaluation;

typedef struct {
    const TelemetryFrame *telemetry;
    const Ledger *ledger;
    int64_t remaining_thermal_margin_mC[N_SUBSYSTEMS];
    uint16_t safety_margin_permille;
    uint64_t predicted_completion_time_ns;
    uint64_t absolute_deadline_ns;
    uint8_t ignored_ledger_dimensions; /* Zero preserves the full Gate. */
} GateContext;

typedef struct SurvivorSet SurvivorSet;

#define GATE_SURVIVOR_STORAGE_BYTES 256

typedef struct {
    GateEvaluation evaluations[SAGE_MAX_CANDIDATES];
    size_t candidate_count;
    _Alignas(max_align_t) unsigned char survivor_storage[GATE_SURVIVOR_STORAGE_BYTES];
} GateFilterResult;

GateEvaluation gate_evaluate(const Candidate *candidate, const Task *task,
                             const GateContext *context);
const SurvivorSet *gate_filter(const Candidate *candidates, size_t count,
                               const Task *task, const GateContext *context,
                               GateFilterResult *result);
size_t gate_survivor_count(const SurvivorSet *survivors);
const SurvivorSet *gate_filter_masked(const Candidate *candidates, size_t count,
    const Task *task, const GateContext *context, uint8_t gate_mask,
    GateFilterResult *result);
bool gate_survivor_valid(const SurvivorSet *survivors);
bool gate_survivor_at(const SurvivorSet *survivors, size_t index, Candidate *out);

#endif
