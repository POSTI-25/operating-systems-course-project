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
    char line[256];
    size_t prediction_lines = 0;
    size_t i;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
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
    }
    ASSERT_TRUE(prediction_lines > 0);
    ASSERT_TRUE(strstr(line, "SUMMARY ") == line);
    fclose(metrics);
}

int main(void)
{
    RUN_TEST(full_orbit_and_fault_event_run_over_pipe);
    return test_failures != 0;
}
