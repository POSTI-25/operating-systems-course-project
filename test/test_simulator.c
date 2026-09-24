#include "test.h"
#include "sim/simulator.h"

#include <stdint.h>

static void orbit_update_and_fault_events_emit_once(void)
{
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    Scenario scenario;
    PlatformClock clock;
    Simulator sim;
    TelemetryFrame frame;
    TelemetryChannel channel;
    uint64_t time;
    char error[128];
    const uint64_t expected[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    size_t i;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.faults[0] = (ScenarioFault){ 7, 0x4U, 0U };
    scenario.faults[1] = (ScenarioFault){ 9, 0U, 0x4U };
    scenario.fault_count = 2;
    ASSERT_TRUE(platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, 99));
    ASSERT_TRUE(simulator_init(&sim, &scenario, &clock));
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        ASSERT_EQ_INT(SIMULATOR_FRAME, simulator_next(&sim, &frame, &time));
        ASSERT_EQ_INT(expected[i], time);
        if (time == 3 || time == 11) {
            ASSERT_TRUE(telemetry_frame_get(&frame, TELEMETRY_ECLIPSE, 0, &channel));
            ASSERT_EQ_INT(1, channel.value);
            ASSERT_EQ_INT(time, channel.timestamp_ns);
        }
        if (time == 6) {
            ASSERT_TRUE(telemetry_frame_get(&frame, TELEMETRY_BATTERY_SOC_MV_PCT, 0, &channel));
            ASSERT_EQ_INT(79000, channel.value);
            ASSERT_EQ_INT(6, channel.timestamp_ns);
        }
        if (time == 7 || time == 9) {
            ASSERT_TRUE(telemetry_frame_get(&frame, TELEMETRY_FAULT_FLAGS, 0, &channel));
            ASSERT_EQ_INT(time == 7 ? 4 : 0, channel.value);
            ASSERT_EQ_INT(time, channel.timestamp_ns);
        }
    }
    ASSERT_EQ_INT(SIMULATOR_DONE, simulator_next(&sim, &frame, &time));
}

static void orbit_epoch_can_end_an_initial_eclipse(void)
{
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    Scenario scenario;
    PlatformClock clock;
    Simulator sim;
    TelemetryFrame frame;
    uint64_t time;
    char error[128];
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.orbit_epoch_ns = 1;
    scenario.initial.eclipse.value = 1;
    ASSERT_TRUE(platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, 0));
    ASSERT_TRUE(simulator_init(&sim, &scenario, &clock));
    ASSERT_EQ_INT(SIMULATOR_FRAME, simulator_next(&sim, &frame, &time));
    ASSERT_EQ_INT(0, time);
    ASSERT_EQ_INT(1, frame.eclipse.value);
    ASSERT_EQ_INT(SIMULATOR_FRAME, simulator_next(&sim, &frame, &time));
    ASSERT_EQ_INT(1, time);
    ASSERT_EQ_INT(0, frame.eclipse.value);
    ASSERT_EQ_INT(1, frame.eclipse.timestamp_ns);
}

static void maximum_end_time_is_emitted(void)
{
    Scenario scenario = {0};
    PlatformClock clock;
    Simulator sim;
    TelemetryFrame frame;
    uint64_t time;
    scenario.duration_ns = UINT64_MAX;
    scenario.step_ns = UINT64_MAX;
    ASSERT_TRUE(platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, 0));
    ASSERT_TRUE(simulator_init(&sim, &scenario, &clock));
    ASSERT_EQ_INT(SIMULATOR_FRAME, simulator_next(&sim, &frame, &time));
    ASSERT_EQ_INT(0, time);
    ASSERT_EQ_INT(SIMULATOR_FRAME, simulator_next(&sim, &frame, &time));
    ASSERT_TRUE(time == UINT64_MAX);
    ASSERT_EQ_INT(SIMULATOR_DONE, simulator_next(&sim, &frame, &time));
}

int main(void)
{
    RUN_TEST(orbit_update_and_fault_events_emit_once);
    RUN_TEST(orbit_epoch_can_end_an_initial_eclipse);
    RUN_TEST(maximum_end_time_is_emitted);
    return test_failures != 0;
}
