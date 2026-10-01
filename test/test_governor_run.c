#include "test.h"
#include "core/governor.h"
#include "platform/platform_ipc.h"

#include <string.h>

static void full_orbit_and_fault_event_run_over_pipe(void)
{
    static Scenario scenario;
    static Governor governor;
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    char error[128];
    GovernorConfig config = {0};
    SimulatorPipe stream;
    GovernorRunStats stats;
    FILE *metrics = tmpfile();
    char line[2048];
    size_t prediction_lines = 0;
    size_t decision_lines = 0;
    size_t fault_decision_lines = 0;
    size_t i;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.tasks.items[0].nominal_exec_ns = 6;
    scenario.tasks.items[0].deadline_ns = 30;
    scenario.fault_count = 1;
    scenario.faults[0].time_ns = 3;
    scenario.faults[0].set_mask = 0x04U;
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    for (i = 0; i < N_SUBSYSTEMS; ++i)
        config.thermal_limit_mC[i] = 100000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(metrics != NULL);
    ASSERT_TRUE(platform_ipc_spawn(&stream, &scenario, 0));
    ASSERT_TRUE(governor_run_pipe_logged(&governor, &stream, &stats, metrics));
    ASSERT_TRUE(platform_ipc_close(&stream));
    ASSERT_EQ_INT(7, stats.tick_cycles);
    ASSERT_EQ_INT(1, stats.fault_cycles);
    ASSERT_TRUE(governor_revalidation_state_at(&governor, 0)->invalid);
    ASSERT_EQ_INT(INVALIDATION_OBSERVED_FAULT,
                  governor_revalidation_state_at(&governor, 0)->record.cause_type);
    ASSERT_EQ_INT(3,
                  governor_revalidation_state_at(&governor, 0)->record.trigger_time_ns);
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    ASSERT_EQ_INT(12, stats.last_sim_time_ns);
    ASSERT_EQ_INT(1, governor_commitment_count(&governor));
    ASSERT_EQ_INT(7, stats.metric_count);
    ASSERT_TRUE(stats.cycle_p50_ns <= stats.cycle_p99_ns);
    ASSERT_TRUE(stats.cycle_p99_ns <= stats.cycle_max_ns);
    for (i = 0; i < stats.metric_count; ++i)
        ASSERT_TRUE(stats.metrics[i].cycle_time_ns >= stats.metrics[i].cpu_time_ns);
    rewind(metrics);
    ASSERT_TRUE(fgets(line, sizeof(line), metrics) != NULL);
    ASSERT_TRUE(strstr(line, "TICK ") == line);
    while (fgets(line, sizeof(line), metrics) != NULL) {
        if (strstr(line, "PRED ") == line) ++prediction_lines;
        if (strstr(line, "DECISION ") == line) {
            ++decision_lines;
            if (strstr(line, "trigger=FAULT") != NULL)
                ++fault_decision_lines;
        }
    }
    ASSERT_TRUE(prediction_lines > 0);
    ASSERT_EQ_INT(8, decision_lines);
    ASSERT_EQ_INT(1, fault_decision_lines);
    ASSERT_TRUE(strstr(line, "SUMMARY ") == line);
    fclose(metrics);
}

static void overrun_emits_no_tentative_decision_record(void)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    SimulatorPipe stream;
    GovernorRunStats stats;
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    FILE *output = tmpfile();
    char error[128];
    char line[2048];
    size_t overruns = 0;
    size_t decisions = 0;
    size_t i;
    ASSERT_TRUE(input != NULL && output != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.self_cost_cap_ns = 1;
    config.test_injected_cpu_ns_per_task = 2000000;
    for (i = 0; i < N_SUBSYSTEMS; ++i)
        config.thermal_limit_mC[i] = 100000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(platform_ipc_spawn(&stream, &scenario, 0));
    ASSERT_TRUE(governor_run_pipe_logged(&governor, &stream, &stats, output));
    ASSERT_TRUE(platform_ipc_close(&stream));
    rewind(output);
    while (fgets(line, sizeof(line), output) != NULL) {
        if (strstr(line, "OVERRUN ") == line) ++overruns;
        if (strstr(line, "DECISION ") == line) ++decisions;
    }
    ASSERT_TRUE(overruns > 0);
    ASSERT_EQ_INT(0, decisions);
    ASSERT_EQ_INT(0, governor.last_record_count);
    fclose(output);
}

static void variant_fault_cadence_runs_through_the_same_pipe(void)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    SimulatorPipe stream;
    GovernorRunStats stats;
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    char error[128];
    int variant;
    int subsystem;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.tasks.items[0].nominal_exec_ns = 6;
    scenario.tasks.items[0].deadline_ns = 30;
    scenario.fault_count = 1;
    scenario.faults[0].time_ns = 3;
    scenario.faults[0].set_mask = 4;
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = 2;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    for (variant = 0; variant < 4; ++variant) {
        config.variant = (GovernorVariant)variant;
        ASSERT_TRUE(governor_init(&governor, &scenario, &config));
        ASSERT_TRUE(platform_ipc_spawn(&stream, &scenario, 0));
        ASSERT_TRUE(governor_run_pipe(&governor, &stream, &stats));
        ASSERT_TRUE(platform_ipc_close(&stream));
        ASSERT_EQ_INT(7, stats.tick_cycles);
        ASSERT_EQ_INT(variant == 0 || variant == 3 ? 1 : 0, stats.fault_cycles);
        if (variant == 0 || variant == 3)
            ASSERT_EQ_INT(3, governor.plan.revalidation[0].record.trigger_time_ns);
        else
            ASSERT_EQ_INT(EXECUTION_COMPLETE, governor.plan.execution[0].phase);
    }
}

static void final_partial_tick_completes_over_pipe(void)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    SimulatorPipe stream;
    GovernorRunStats stats;
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    char error[128];
    int subsystem;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.duration_ns = 3;
    scenario.update_count = 0;
    scenario.tasks.items[0].nominal_exec_ns = 3;
    scenario.tasks.items[0].deadline_ns = 3;
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(platform_ipc_spawn(&stream, &scenario, 0));
    ASSERT_TRUE(governor_run_pipe(&governor, &stream, &stats));
    ASSERT_TRUE(platform_ipc_close(&stream));
    ASSERT_EQ_INT(2, stats.tick_cycles);
    ASSERT_EQ_INT(EXECUTION_COMPLETE, governor.plan.execution[0].phase);
    ASSERT_EQ_INT(3, governor.plan.execution[0].completed_ns);
}

int main(void)
{
    RUN_TEST(full_orbit_and_fault_event_run_over_pipe);
    RUN_TEST(overrun_emits_no_tentative_decision_record);
    RUN_TEST(variant_fault_cadence_runs_through_the_same_pipe);
    RUN_TEST(final_partial_tick_completes_over_pipe);
    return test_failures != 0;
}
