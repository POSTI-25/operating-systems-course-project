#include "test.h"
#include "core/revalidation.h"

#include <string.h>

static void fixture(Task *task, AssumptionSnapshot *snapshot,
                    TelemetryFrame *actual, TelemetryFrame *forecast,
                    Ledger *ledger)
{
    LedgerConfig config = {0};
    memset(task, 0, sizeof(*task));
    task->task_id = 7;
    task->primary_subsystem = SUBSYSTEM_PAYLOAD;
    task->deadline_ns = 100;
    task->nominal_exec_ns = 2;
    task->modes[MODE_FULL].energy_cost_mJ = 100;
    task->modes[MODE_FULL].thermal_effect_mC = 100;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->soc_permille = 800;
    snapshot->temp_mC[SUBSYSTEM_PAYLOAD] = 20000;
    snapshot->current_draw_mA = 300;
    snapshot->forecast_horizon_end_ns = 6;
    telemetry_frame_init(actual);
    actual->battery_soc_mV_pct.value = 800;
    actual->temp_mC[SUBSYSTEM_PAYLOAD].value = 20000;
    actual->current_draw_mA.value = 300;
    *forecast = *actual;
    config.capacity[LEDGER_CPU] = 100;
    config.capacity[LEDGER_MEMORY] = 1000;
    config.capacity[LEDGER_STORAGE] = 1000;
    config.capacity[LEDGER_COMM] = 100;
    config.capacity[LEDGER_SENSOR] = 15;
    config.battery_capacity_mJ = 100000;
    config.soc_floor_permille = 200;
    ASSERT_TRUE(ledger_init(ledger, &config));
    ASSERT_TRUE(ledger_update_energy(ledger, 800, 0, 0));
}

static RevalidationInput input_for(const Task *task,
                                   const AssumptionSnapshot *snapshot,
                                   const TelemetryFrame *actual,
                                   const TelemetryFrame *forecast,
                                   const Ledger *ledger, uint64_t now_ns)
{
    RevalidationInput input = {0};
    int i;
    input.task = task;
    input.mode = MODE_FULL;
    input.assumption_snapshot = snapshot;
    input.actual = actual;
    input.forecast = forecast;
    input.ledger = ledger;
    input.now_ns = now_ns;
    input.forecast_horizon_end_ns = now_ns + 4;
    input.step_ns = 2;
    for (i = 0; i < N_SUBSYSTEMS; ++i)
        input.thermal_limit_mC[i] = 100000;
    return input;
}

static void observed_deviation_needs_three_clear_ticks(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    actual.battery_soc_mV_pct.value = 790;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(INVALIDATION_OBSERVED_DEVIATION, state.record.cause_type);
    ASSERT_EQ_INT(REVALIDATION_TRIGGER_SOC, state.record.trigger_quantity);
    ASSERT_EQ_INT(0, state.record.clear_count);
    actual.battery_soc_mV_pct.value = 800;
    input.now_ns = 4;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    ASSERT_EQ_INT(1, state.record.clear_count);
    input.now_ns = 6;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 8;
    ASSERT_EQ_INT(REVALIDATION_RESTORE_READY,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(3, state.record.clear_count);
}

static void false_predicted_risk_restores_at_horizon(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    forecast.battery_soc_mV_pct.value = 199;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(INVALIDATION_PREDICTED_RISK, state.record.cause_type);
    ASSERT_EQ_INT(6, state.record.forecast_horizon_end);
    input.now_ns = 4;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 6;
    forecast.battery_soc_mV_pct.value = 800;
    ASSERT_EQ_INT(REVALIDATION_RESTORE_READY,
                  revalidation_step(&state, &input));
}

static void confirmed_prediction_upgrades_and_needs_clear_ticks(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    forecast.temp_mC[SUBSYSTEM_PAYLOAD].value = 100000;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(INVALIDATION_PREDICTED_RISK, state.record.cause_type);
    input.now_ns = 4;
    actual.temp_mC[SUBSYSTEM_PAYLOAD].value = 100000;
    ASSERT_EQ_INT(REVALIDATION_UPGRADE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(INVALIDATION_OBSERVED_DEVIATION, state.record.cause_type);
    ASSERT_TRUE(state.record.upgraded_from_predicted);
    actual.temp_mC[SUBSYSTEM_PAYLOAD].value = 20000;
    input.now_ns = 6;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 8;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 10;
    ASSERT_EQ_INT(REVALIDATION_RESTORE_READY,
                  revalidation_step(&state, &input));
}

static void dwell_suppresses_same_cause_at_tolerance_boundary(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    actual.battery_soc_mV_pct.value = 794;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    actual.battery_soc_mV_pct.value = 795;
    input.now_ns = 4;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 6;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 8;
    ASSERT_EQ_INT(REVALIDATION_RESTORE_READY,
                  revalidation_step(&state, &input));
    ASSERT_TRUE(revalidation_complete_restore(&state, 8, 2));
    actual.battery_soc_mV_pct.value = 794;
    input.now_ns = 10;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 14;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
}

static void confirmation_at_exact_horizon_upgrades_to_observed(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    forecast.battery_soc_mV_pct.value = 199;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    input.now_ns = 6;
    actual.battery_soc_mV_pct.value = 199;
    ASSERT_EQ_INT(REVALIDATION_UPGRADE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(INVALIDATION_OBSERVED_DEVIATION, state.record.cause_type);
}

static void hard_soc_gate_failure_overrides_small_deviation_tolerance(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    ledger.config.soc_floor_permille = 790;
    task.modes[MODE_FULL].energy_cost_mJ = 1000;
    actual.battery_soc_mV_pct.value = 799;
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(INVALIDATION_OBSERVED_DEVIATION, state.record.cause_type);
}

static void protected_residual_failure_overrides_same_cause_dwell(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    actual.battery_soc_mV_pct.value = 790;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    actual.battery_soc_mV_pct.value = 800;
    input.now_ns = 4;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 6;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 8;
    ASSERT_EQ_INT(REVALIDATION_RESTORE_READY,
                  revalidation_step(&state, &input));
    ASSERT_TRUE(revalidation_complete_restore(&state, 8, 2));
    ledger.config.capacity[LEDGER_CPU] = 0;
    ledger.self_cpu_pct = 10;
    input.now_ns = 10;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(REVALIDATION_TRIGGER_PROTECTED_RESIDUAL,
                  state.record.trigger_quantity);
}

static void clear_ticks_require_fresh_consecutive_observations(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    input.observation_epoch = 1;
    actual.battery_soc_mV_pct.value = 790;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    actual.battery_soc_mV_pct.value = 800;
    input.now_ns = 4;
    input.observation_epoch = 2;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    ASSERT_EQ_INT(1, state.record.clear_count);
    input.now_ns = 6;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 8;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    ASSERT_EQ_INT(1, state.record.clear_count);
    input.now_ns = 10;
    input.observation_epoch = 3;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    ASSERT_EQ_INT(1, state.record.clear_count);
    input.now_ns = 12;
    input.observation_epoch = 4;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
    input.now_ns = 14;
    input.observation_epoch = 5;
    ASSERT_EQ_INT(REVALIDATION_RESTORE_READY,
                  revalidation_step(&state, &input));
}

static void active_fault_upgrades_predicted_risk_and_blocks_restoration(void)
{
    Task task;
    AssumptionSnapshot snapshot;
    TelemetryFrame actual, forecast;
    Ledger ledger;
    RevalidationState state = {0};
    RevalidationInput input;
    fixture(&task, &snapshot, &actual, &forecast, &ledger);
    input = input_for(&task, &snapshot, &actual, &forecast, &ledger, 2);
    forecast.battery_soc_mV_pct.value = 199;
    ASSERT_EQ_INT(REVALIDATION_INVALIDATE,
                  revalidation_step(&state, &input));
    actual.fault_flags.value = UINT32_C(1) << SUBSYSTEM_PAYLOAD;
    forecast.battery_soc_mV_pct.value = 800;
    input.now_ns = 6;
    ASSERT_EQ_INT(REVALIDATION_UPGRADE,
                  revalidation_step(&state, &input));
    ASSERT_EQ_INT(INVALIDATION_OBSERVED_FAULT, state.record.cause_type);
    ASSERT_TRUE(state.record.upgraded_from_predicted);
    ASSERT_EQ_INT(0, state.record.clear_count);
    input.now_ns = 8;
    ASSERT_EQ_INT(REVALIDATION_NONE, revalidation_step(&state, &input));
}

int main(void)
{
    RUN_TEST(observed_deviation_needs_three_clear_ticks);
    RUN_TEST(false_predicted_risk_restores_at_horizon);
    RUN_TEST(confirmed_prediction_upgrades_and_needs_clear_ticks);
    RUN_TEST(dwell_suppresses_same_cause_at_tolerance_boundary);
    RUN_TEST(confirmation_at_exact_horizon_upgrades_to_observed);
    RUN_TEST(hard_soc_gate_failure_overrides_small_deviation_tolerance);
    RUN_TEST(protected_residual_failure_overrides_same_cause_dwell);
    RUN_TEST(clear_ticks_require_fresh_consecutive_observations);
    RUN_TEST(active_fault_upgrades_predicted_risk_and_blocks_restoration);
    return test_failures != 0;
}
