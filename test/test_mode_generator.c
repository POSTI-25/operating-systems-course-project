#include "test.h"
#include "core/mode_generator.h"

static void profiles_generate_five_canonical_modes(void)
{
    Task task = { .task_id = 42, .period_ns = 10, .deadline_ns = 9,
        .nominal_exec_ns = 2, .mission_value = 901,
        .primary_subsystem = SUBSYSTEM_PAYLOAD };
    TelemetryFrame snapshot;
    Candidate candidates[SAGE_MAX_CANDIDATES];
    task.modes[TASK_MODE_FULL] = (TaskModeProfile){ 100, 20, 1000, { 30, 40, 50, 1, 10 } };
    task.modes[TASK_MODE_THROTTLED] = (TaskModeProfile){ 70, 10, 700, { 20, 30, 40, 1, 5 } };
    task.modes[TASK_MODE_DEGRADED] = (TaskModeProfile){ 40, -5, 400, { 10, 20, 30, 0, 0 } };
    telemetry_frame_init(&snapshot);
    ASSERT_EQ_INT(5, mode_generator_generate(&task, &snapshot, candidates, 5));
    ASSERT_EQ_INT(MODE_FULL, candidates[0].mode);
    ASSERT_EQ_INT(MODE_THROTTLED, candidates[1].mode);
    ASSERT_EQ_INT(MODE_DEGRADED, candidates[2].mode);
    ASSERT_EQ_INT(MODE_DEFERRED, candidates[3].mode);
    ASSERT_EQ_INT(MODE_REJECTED, candidates[4].mode);
    ASSERT_EQ_INT(901, candidates[0].delivered_value);
    ASSERT_EQ_INT(630, candidates[1].delivered_value);
    ASSERT_EQ_INT(360, candidates[2].delivered_value);
    ASSERT_EQ_INT(70, candidates[1].predicted_cost.energy_cost_mJ);
    ASSERT_EQ_INT(30, candidates[1].predicted_cost.resource_demand.mem_KB);
    ASSERT_EQ_INT(0, candidates[3].delivered_value);
    ASSERT_EQ_INT(0, candidates[4].predicted_cost.energy_cost_mJ);
}

static void short_output_capacity_does_not_write(void)
{
    Task task = { .task_id = 1 };
    TelemetryFrame snapshot;
    Candidate candidate = { .task_id = 99 };
    telemetry_frame_init(&snapshot);
    ASSERT_EQ_INT(0, mode_generator_generate(&task, &snapshot, &candidate, 1));
    ASSERT_EQ_INT(99, candidate.task_id);
}

int main(void)
{
    RUN_TEST(profiles_generate_five_canonical_modes);
    RUN_TEST(short_output_capacity_does_not_write);
    return test_failures != 0;
}
