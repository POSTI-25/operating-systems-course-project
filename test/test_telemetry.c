#include "test.h"
#include "core/telemetry.h"

static void channels_keep_independent_metadata(void)
{
    TelemetryFrame frame;
    TelemetryChannel got;
    TelemetryChannel battery = { 80000, 3, TELEMETRY_QUALITY_STALE };
    TelemetryChannel temp = { -1200, 7, TELEMETRY_QUALITY_MISSING };
    telemetry_frame_init(&frame);
    ASSERT_TRUE(telemetry_frame_set(&frame, TELEMETRY_BATTERY_SOC_MV_PCT, 0, battery));
    ASSERT_TRUE(telemetry_frame_set(&frame, TELEMETRY_TEMP_MC, SUBSYSTEM_ADCS, temp));
    ASSERT_TRUE(telemetry_frame_get(&frame, TELEMETRY_BATTERY_SOC_MV_PCT, 0, &got));
    ASSERT_EQ_INT(80000, got.value);
    ASSERT_EQ_INT(3, got.timestamp_ns);
    ASSERT_EQ_INT(TELEMETRY_QUALITY_STALE, got.quality);
    ASSERT_TRUE(telemetry_frame_get(&frame, TELEMETRY_TEMP_MC, SUBSYSTEM_ADCS, &got));
    ASSERT_EQ_INT(-1200, got.value);
    ASSERT_EQ_INT(7, got.timestamp_ns);
    ASSERT_EQ_INT(TELEMETRY_QUALITY_MISSING, got.quality);
}

static void invalid_values_and_indices_are_rejected(void)
{
    TelemetryFrame frame;
    TelemetryChannel bad = { 2, 0, TELEMETRY_QUALITY_OK };
    telemetry_frame_init(&frame);
    ASSERT_TRUE(!telemetry_frame_set(&frame, TELEMETRY_ECLIPSE, 0, bad));
    bad.value = 101;
    ASSERT_TRUE(!telemetry_frame_set(&frame, TELEMETRY_CPU_AVAIL_PCT, 0, bad));
    bad.value = 0;
    ASSERT_TRUE(!telemetry_frame_set(&frame, TELEMETRY_TEMP_MC, N_SUBSYSTEMS, bad));
    ASSERT_TRUE(!telemetry_frame_set(&frame, TELEMETRY_ECLIPSE, 1, bad));
    bad.quality = (TelemetryQuality)7;
    ASSERT_TRUE(!telemetry_frame_set(&frame, TELEMETRY_TEMP_MC, 0, bad));
}

int main(void)
{
    RUN_TEST(channels_keep_independent_metadata);
    RUN_TEST(invalid_values_and_indices_are_rejected);
    return test_failures != 0;
}
