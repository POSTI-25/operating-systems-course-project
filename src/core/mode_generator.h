#ifndef SAGE_MODE_GENERATOR_H
#define SAGE_MODE_GENERATOR_H

#include "task_model.h"

#include <stddef.h>
#include <stdint.h>

#define SAGE_MAX_CANDIDATES 5

typedef enum {
    MODE_FULL = 0,
    MODE_THROTTLED = 1,
    MODE_DEGRADED = 2,
    MODE_DEFERRED = 3,
    MODE_REJECTED = 4
} Mode;

typedef struct {
    uint64_t energy_cost_mJ;
    int32_t thermal_effect_mC;
    ResourceDemand resource_demand;
} CandidateCost;

typedef struct {
    uint32_t task_id;
    Mode mode;
    CandidateCost predicted_cost;
    uint32_t delivered_value;
} Candidate;

size_t mode_generator_generate(const Task *task, const TelemetryFrame *snapshot,
                               Candidate *out, size_t capacity);

#endif
