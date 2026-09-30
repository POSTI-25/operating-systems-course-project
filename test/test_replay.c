#include "test.h"
#include "replay.h"

typedef struct {
    size_t count;
    uint64_t times[8];
    DecisionTrigger triggers[8];
    ExecutionPhase phases[8];
} Seen;

static bool capture(const Governor *governor, const TelemetryFrame *frame,
                    uint64_t now_ns, void *context)
{
    Seen *seen = context;
    const LogRecord *record = governor_log_record_at(governor, 0);
    (void)frame;
    if (seen->count >= 8 || record == NULL) return false;
    seen->times[seen->count] = now_ns;
    seen->triggers[seen->count] = record->trigger;
    seen->phases[seen->count++] = record->state_after;
    return true;
}

static void replay_preserves_pipe_cadence_and_completion_boundary(void)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    Seen seen = {0};
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    char error[128];
    int subsystem;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.duration_ns = 4;
    scenario.tasks.items[0].nominal_exec_ns = 3;
    scenario.tasks.items[0].deadline_ns = 20;
    scenario.fault_count = 2;
    scenario.faults[0] = (ScenarioFault){.time_ns = 3, .set_mask = 4};
    scenario.faults[1] = (ScenarioFault){.time_ns = 4, .set_mask = 8};
    config.alpha = 0.3;
    config.beta = 0.1;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(replay_run(&scenario, &governor, NULL, capture, &seen));
    ASSERT_EQ_INT(5, seen.count);
    ASSERT_EQ_INT(0, seen.times[0]);
    ASSERT_EQ_INT(2, seen.times[1]);
    ASSERT_EQ_INT(3, seen.times[2]);
    ASSERT_EQ_INT(4, seen.times[3]);
    ASSERT_EQ_INT(4, seen.times[4]);
    ASSERT_EQ_INT(EXECUTION_COMPLETE, seen.phases[2]);
    ASSERT_EQ_INT(DECISION_TRIGGER_TICK, seen.triggers[3]);
    ASSERT_EQ_INT(DECISION_TRIGGER_FAULT, seen.triggers[4]);
    config.variant = GOVERNOR_VARIANT_GOMX_STYLE;
    seen = (Seen){0};
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(replay_run(&scenario, &governor, NULL, capture, &seen));
    ASSERT_EQ_INT(3, seen.count);
    ASSERT_EQ_INT(4, seen.times[2]);
}

int main(void)
{
    RUN_TEST(replay_preserves_pipe_cadence_and_completion_boundary);
    return test_failures != 0;
}
