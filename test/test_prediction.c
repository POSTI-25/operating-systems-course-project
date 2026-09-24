#include "test.h"
#include "core/prediction.h"

static void holt_equations_and_eclipse_reset(void)
{
    HoltFilter filter = {0};
    ASSERT_TRUE(prediction_filter_update(&filter, 100.0, 0.3, 0.1, false));
    ASSERT_TRUE(prediction_filter_update(&filter, 110.0, 0.3, 0.1, false));
    ASSERT_TRUE(filter.level > 102.999 && filter.level < 103.001);
    ASSERT_TRUE(filter.trend > 0.299 && filter.trend < 0.301);
    ASSERT_TRUE(prediction_filter_forecast(&filter, 2.0) > 103.599);
    ASSERT_TRUE(prediction_filter_forecast(&filter, 2.0) < 103.601);
    ASSERT_TRUE(prediction_filter_update(&filter, 120.0, 0.3, 0.1, true));
    ASSERT_TRUE(filter.level > 108.309 && filter.level < 108.311);
    ASSERT_TRUE(filter.trend == 0.0);
}

static void forecasts_each_channel_at_execution_plus_lookahead(void)
{
    PredictionEngine engine;
    TelemetryFrame frame;
    TelemetryFrame forecast;
    uint64_t horizon_end = 0;
    ASSERT_TRUE(prediction_engine_init(&engine, 0.3, 0.1, 2, 2));
    telemetry_frame_init(&frame);
    frame.battery_soc_mV_pct.value = 800;
    frame.current_draw_mA.value = 300;
    frame.temp_mC[SUBSYSTEM_PAYLOAD].value = 20000;
    ASSERT_TRUE(prediction_engine_update(&engine, &frame));
    frame.battery_soc_mV_pct.value = 790;
    frame.current_draw_mA.value = 310;
    frame.temp_mC[SUBSYSTEM_PAYLOAD].value = 20010;
    ASSERT_TRUE(prediction_engine_update(&engine, &frame));
    ASSERT_TRUE(prediction_engine_forecast(&engine, 2, 2, &forecast,
                                           &horizon_end));
    ASSERT_EQ_INT(6, horizon_end);
    ASSERT_EQ_INT(796, forecast.battery_soc_mV_pct.value);
    ASSERT_EQ_INT(20004, forecast.temp_mC[SUBSYSTEM_PAYLOAD].value);
    ASSERT_EQ_INT(304, forecast.current_draw_mA.value);
    ASSERT_EQ_INT(6, forecast.battery_soc_mV_pct.timestamp_ns);
}

static void matches_forecast_with_actual_at_horizon(void)
{
    PredictionTracker tracker = {0};
    TelemetryFrame predicted;
    TelemetryFrame actual;
    PredictionAccuracyEvent events[1];
    size_t count = 99;
    telemetry_frame_init(&predicted);
    telemetry_frame_init(&actual);
    predicted.battery_soc_mV_pct.value = 780;
    predicted.temp_mC[SUBSYSTEM_PAYLOAD].value = 20020;
    predicted.current_draw_mA.value = 320;
    ASSERT_TRUE(prediction_tracker_add(&tracker, 7, 2, 6, &predicted));
    actual.battery_soc_mV_pct.value = 790;
    ASSERT_TRUE(prediction_tracker_observe(&tracker, 4, &actual,
                                            events, 1, &count));
    ASSERT_EQ_INT(0, count);
    actual.battery_soc_mV_pct.value = 775;
    actual.temp_mC[SUBSYSTEM_PAYLOAD].value = 20030;
    actual.current_draw_mA.value = 330;
    ASSERT_TRUE(prediction_tracker_observe(&tracker, 6, &actual,
                                            events, 1, &count));
    ASSERT_EQ_INT(1, count);
    ASSERT_EQ_INT(7, events[0].task_id);
    ASSERT_EQ_INT(780, events[0].predicted.soc_permille);
    ASSERT_EQ_INT(775, events[0].actual.soc_permille);
    ASSERT_EQ_INT(-5, events[0].soc_error_permille);
    ASSERT_EQ_INT(10, events[0].temp_error_mC[SUBSYSTEM_PAYLOAD]);
    ASSERT_EQ_INT(10, events[0].draw_error_mA);
}

int main(void)
{
    RUN_TEST(holt_equations_and_eclipse_reset);
    RUN_TEST(forecasts_each_channel_at_execution_plus_lookahead);
    RUN_TEST(matches_forecast_with_actual_at_horizon);
    return test_failures != 0;
}
