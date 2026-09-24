#include "test.h"
#include "sim/scenario.h"

#include <string.h>

static void normal_orbit_loads_complete_state_and_tasks(void)
{
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    Scenario scenario;
    TelemetryChannel channel;
    char error[128] = {0};
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    ASSERT_EQ_INT(12, scenario.duration_ns);
    ASSERT_EQ_INT(1, scenario.tasks.count);
    ASSERT_EQ_INT(7, scenario.tasks.mode_masks[0]);
    ASSERT_TRUE(telemetry_frame_get(&scenario.initial, TELEMETRY_TEMP_MC,
                                    SUBSYSTEM_ADCS, &channel));
    ASSERT_EQ_INT(23000, channel.value);
    ASSERT_EQ_INT(100, scenario.tasks.items[0].modes[TASK_MODE_FULL].energy_cost_mJ);
}

static void malformed_directive_reports_line(void)
{
    FILE *input = tmpfile();
    Scenario scenario;
    char error[128] = {0};
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(fputs("SCENARIO x\nUNKNOWN 1\n", input) >= 0);
    rewind(input);
    ASSERT_TRUE(!scenario_load_file(input, &scenario, error, sizeof(error)));
    ASSERT_TRUE(strstr(error, "line 2") != NULL);
    fclose(input);
}

static void duplicate_initial_is_rejected(void)
{
    FILE *source = fopen("scenarios/normal_orbit.scn", "r");
    FILE *input = tmpfile();
    Scenario scenario;
    char buffer[512];
    char error[128];
    ASSERT_TRUE(source != NULL && input != NULL);
    while (fgets(buffer, sizeof(buffer), source) != NULL) {
        ASSERT_TRUE(fputs(buffer, input) >= 0);
    }
    ASSERT_TRUE(fputs("INITIAL ECLIPSE 0 0 OK\n", input) >= 0);
    rewind(input);
    ASSERT_TRUE(!scenario_load_file(input, &scenario, error, sizeof(error)));
    ASSERT_TRUE(strstr(error, "duplicate") != NULL);
    fclose(source);
    fclose(input);
}

static void overflowing_duration_is_rejected(void)
{
    FILE *input = tmpfile();
    Scenario scenario;
    char error[128];
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(fputs("DURATION_NS 18446744073709551616\n", input) >= 0);
    rewind(input);
    ASSERT_TRUE(!scenario_load_file(input, &scenario, error, sizeof(error)));
    ASSERT_TRUE(strstr(error, "line 1") != NULL);
    fclose(input);
}

int main(void)
{
    RUN_TEST(normal_orbit_loads_complete_state_and_tasks);
    RUN_TEST(malformed_directive_reports_line);
    RUN_TEST(duplicate_initial_is_rejected);
    RUN_TEST(overflowing_duration_is_rejected);
    return test_failures != 0;
}
