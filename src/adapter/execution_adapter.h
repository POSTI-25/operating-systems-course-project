#ifndef SAGE_EXECUTION_ADAPTER_H
#define SAGE_EXECUTION_ADAPTER_H

#include "core/mode_generator.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EXECUTION_PENDING = 0,
    EXECUTION_RUNNING = 1,
    EXECUTION_PAUSED = 2,
    EXECUTION_COMPLETE = 3,
    EXECUTION_REJECTED = 4
} ExecutionPhase;

typedef struct {
    ExecutionPhase phase;
    uint64_t remaining_ns;
    uint64_t last_resume_ns;
    uint64_t completed_ns;
    Mode mode;
    uint32_t delivered_value;
} ExecutionState;

bool execution_start(ExecutionState *state, const Task *task, Mode mode,
                     uint64_t now_ns);
bool execution_advance(ExecutionState *state, const Task *task, uint64_t now_ns,
                       bool *completed);
bool execution_pause(ExecutionState *state, const Task *task, uint64_t now_ns);
bool execution_resume(ExecutionState *state, Mode mode, uint64_t now_ns);

#endif
