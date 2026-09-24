#ifndef SAGE_SCENARIO_H
#define SAGE_SCENARIO_H

#include "core/task_model.h"
#include "core/telemetry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SAGE_MAX_UPDATES 256
#define SAGE_MAX_FAULTS 64
#define SAGE_SCENARIO_LINE_MAX 512

typedef struct {
    uint64_t time_ns;
    TelemetryChannelId id;
    unsigned index;
    TelemetryChannel channel;
} ScenarioUpdate;

typedef struct {
    uint64_t time_ns;
    uint32_t set_mask;
    uint32_t clear_mask;
} ScenarioFault;

typedef struct {
    char name[64];
    uint64_t duration_ns;
    uint64_t step_ns;
    bool has_orbit;
    uint64_t orbit_epoch_ns;
    uint64_t orbit_period_ns;
    uint64_t eclipse_offset_ns;
    uint64_t eclipse_duration_ns;
    TelemetryFrame initial;
    ScenarioUpdate updates[SAGE_MAX_UPDATES];
    size_t update_count;
    ScenarioFault faults[SAGE_MAX_FAULTS];
    size_t fault_count;
    TaskTable tasks;
} Scenario;

bool scenario_load_file(FILE *input, Scenario *scenario,
                        char *error, size_t error_size);

#endif
