#include "execution_adapter.h"

#include <limits.h>

static bool executing_mode(Mode mode)
{
    return mode == MODE_FULL || mode == MODE_THROTTLED ||
        mode == MODE_DEGRADED;
}

bool execution_start(ExecutionState *state, const Task *task, Mode mode,
                     uint64_t now_ns)
{
    if (state == NULL || task == NULL || state->phase != EXECUTION_PENDING ||
        !executing_mode(mode) || task->nominal_exec_ns == 0 ||
        task->nominal_exec_ns > UINT64_MAX - now_ns)
        return false;
    *state = (ExecutionState){0};
    state->phase = EXECUTION_RUNNING;
    state->remaining_ns = task->nominal_exec_ns;
    state->last_resume_ns = now_ns;
    state->mode = mode;
    return true;
}

bool execution_advance(ExecutionState *state, const Task *task, uint64_t now_ns,
                       bool *completed)
{
    uint64_t elapsed;
    if (state == NULL || task == NULL || completed == NULL ||
        state->phase == EXECUTION_PENDING ||
        state->phase == EXECUTION_REJECTED ||
        now_ns < state->last_resume_ns)
        return false;
    *completed = false;
    if (state->phase == EXECUTION_RUNNING) {
        if (!executing_mode(state->mode) || state->remaining_ns == 0)
            return false;
        elapsed = now_ns - state->last_resume_ns;
        if (elapsed >= state->remaining_ns) {
            const TaskModeProfile *profile = &task->modes[state->mode];
            if (profile->value_fraction_permille > 1000U ||
                task->mission_value > 1000U)
                return false;
            state->completed_ns = state->last_resume_ns + state->remaining_ns;
            state->remaining_ns = 0;
            state->phase = EXECUTION_COMPLETE;
            state->delivered_value = (uint32_t)task->mission_value *
                profile->value_fraction_permille / 1000U;
            *completed = true;
        } else {
            state->remaining_ns -= elapsed;
        }
    }
    state->last_resume_ns = now_ns;
    return true;
}

bool execution_pause(ExecutionState *state, const Task *task, uint64_t now_ns)
{
    bool completed;
    if (state == NULL || task == NULL ||
        state->phase != EXECUTION_RUNNING ||
        !execution_advance(state, task, now_ns, &completed) || completed)
        return false;
    state->phase = EXECUTION_PAUSED;
    return true;
}

bool execution_resume(ExecutionState *state, Mode mode, uint64_t now_ns)
{
    if (state == NULL || state->phase != EXECUTION_PAUSED ||
        !executing_mode(mode) || state->remaining_ns == 0 ||
        now_ns < state->last_resume_ns ||
        state->remaining_ns > UINT64_MAX - now_ns)
        return false;
    state->phase = EXECUTION_RUNNING;
    state->last_resume_ns = now_ns;
    state->mode = mode;
    return true;
}
