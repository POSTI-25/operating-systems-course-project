#include "test.h"
#include "adapter/execution_adapter.h"

#include <stdint.h>

static Task example_task(void)
{
    Task task = {0};
    task.task_id = 7;
    task.period_ns = 1;
    task.deadline_ns = 100;
    task.nominal_exec_ns = 6;
    task.mission_value = 800;
    task.modes[MODE_FULL].value_fraction_permille = 1000;
    task.modes[MODE_DEGRADED].value_fraction_permille = 400;
    return task;
}

static void pause_preserves_work_and_resume_uses_new_mode(void)
{
    Task task = example_task();
    ExecutionState state = {0};
    bool completed = false;
    ASSERT_TRUE(execution_start(&state, &task, MODE_FULL, 2));
    ASSERT_EQ_INT(EXECUTION_RUNNING, state.phase);
    ASSERT_EQ_INT(6, state.remaining_ns);
    ASSERT_TRUE(execution_advance(&state, &task, 5, &completed));
    ASSERT_EQ_INT(0, completed);
    ASSERT_EQ_INT(3, state.remaining_ns);
    ASSERT_TRUE(execution_pause(&state, &task, 5));
    ASSERT_EQ_INT(EXECUTION_PAUSED, state.phase);
    ASSERT_TRUE(execution_advance(&state, &task, 20, &completed));
    ASSERT_EQ_INT(0, completed);
    ASSERT_EQ_INT(3, state.remaining_ns);
    ASSERT_TRUE(execution_resume(&state, MODE_DEGRADED, 20));
    ASSERT_TRUE(execution_advance(&state, &task, 27, &completed));
    ASSERT_TRUE(completed);
    ASSERT_EQ_INT(EXECUTION_COMPLETE, state.phase);
    ASSERT_EQ_INT(23, state.completed_ns);
    ASSERT_EQ_INT(320, state.delivered_value);
    ASSERT_TRUE(execution_advance(&state, &task, 80, &completed));
    ASSERT_EQ_INT(0, completed);
    ASSERT_EQ_INT(EXECUTION_COMPLETE, state.phase);
    ASSERT_TRUE(!execution_start(&state, &task, MODE_FULL, 81));
}

static void exact_boundary_and_reverse_time_are_checked(void)
{
    Task task = example_task();
    ExecutionState state = {0};
    bool completed = false;
    ASSERT_TRUE(execution_start(&state, &task, MODE_FULL, 2));
    ASSERT_TRUE(!execution_advance(&state, &task, 1, &completed));
    ASSERT_EQ_INT(6, state.remaining_ns);
    ASSERT_TRUE(execution_advance(&state, &task, 8, &completed));
    ASSERT_TRUE(completed);
    ASSERT_EQ_INT(8, state.completed_ns);
    ASSERT_EQ_INT(800, state.delivered_value);
}

static void pause_and_resume_reject_reverse_time_and_overflow(void)
{
    Task task = example_task();
    ExecutionState state = {0};
    bool completed = false;
    ASSERT_TRUE(execution_start(&state, &task, MODE_FULL, 2));
    ASSERT_TRUE(execution_pause(&state, &task, 4));
    ASSERT_TRUE(!execution_resume(&state, MODE_FULL, 3));
    ASSERT_TRUE(execution_advance(&state, &task, 20, &completed));
    ASSERT_TRUE(!execution_resume(&state, MODE_FULL, 19));
    ASSERT_TRUE(execution_resume(&state, MODE_FULL, 20));
    ASSERT_TRUE(!execution_pause(&state, &task, 19));
    state = (ExecutionState){0};
    ASSERT_TRUE(!execution_start(&state, &task, MODE_FULL, UINT64_MAX));
    ASSERT_TRUE(!execution_start(&state, &task, MODE_DEFERRED, 0));
}

int main(void)
{
    RUN_TEST(pause_preserves_work_and_resume_uses_new_mode);
    RUN_TEST(exact_boundary_and_reverse_time_are_checked);
    RUN_TEST(pause_and_resume_reject_reverse_time_and_overflow);
    return test_failures != 0;
}
