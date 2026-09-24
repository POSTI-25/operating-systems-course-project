#include "test.h"
#include "core/governor.h"

#include <string.h>

static void setup_scenario(Scenario *scenario, uint16_t floor)
{
    size_t i;
    memset(scenario, 0, sizeof(*scenario));
    scenario->step_ns = 2;
    scenario->duration_ns = 12;
    scenario->battery_capacity_mJ = 100000;
    scenario->soc_floor_permille = floor;
    scenario->initial.battery_soc_mV_pct.value = 800;
    scenario->initial.cpu_avail_pct.value = 100;
    scenario->initial.mem_avail_KB.value = 1000;
    scenario->initial.storage_avail_KB.value = 1000;
    scenario->tasks.count = 1;
    scenario->tasks.items[0].task_id = 1;
    scenario->tasks.items[0].period_ns = 10;
    scenario->tasks.items[0].deadline_ns = 10;
    scenario->tasks.items[0].nominal_exec_ns = 2;
    scenario->tasks.items[0].criticality_class = 1;
    scenario->tasks.items[0].mission_value = 900;
    scenario->tasks.items[0].primary_subsystem = SUBSYSTEM_PAYLOAD;
    for (i = 0; i < TASK_EXECUTING_MODE_COUNT; ++i)
        scenario->tasks.items[0].modes[i].value_fraction_permille = 1000;
}

static GovernorConfig setup_config(bool prediction_enabled)
{
    GovernorConfig config = {0};
    size_t i;
    config.alpha = 1.0;
    config.beta = 1.0;
    config.lookahead_ns = 2;
    config.prediction_enabled = prediction_enabled;
    config.self_cost_cap_ns = UINT64_MAX;
    for (i = 0; i < N_SUBSYSTEMS; ++i)
        config.thermal_limit_mC[i] = 100000;
    return config;
}

static void predicted_soc_reaches_gate_before_selection(void)
{
    static Scenario scenario;
    Governor predicted;
    Governor baseline;
    GovernorConfig predictive = setup_config(true);
    GovernorConfig current = setup_config(false);
    TelemetryFrame frame;
    setup_scenario(&scenario, 780);
    ASSERT_TRUE(governor_init(&predicted, &scenario, &predictive));
    ASSERT_TRUE(governor_init(&baseline, &scenario, &current));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe(&predicted, &frame));
    ASSERT_TRUE(governor_observe(&baseline, &frame));
    frame.battery_soc_mV_pct.value = 790;
    ASSERT_TRUE(governor_observe(&predicted, &frame));
    ASSERT_TRUE(governor_observe(&baseline, &frame));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&predicted, 2, false));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&baseline, 2, false));
    ASSERT_EQ_INT(0, governor_commitment_count(&predicted));
    ASSERT_EQ_INT(1, governor_commitment_count(&baseline));
}

static void commitment_stores_predicted_assumption_snapshot(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    TelemetryFrame frame;
    const Commitment *commitment;
    setup_scenario(&scenario, 700);
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe(&governor, &frame));
    frame.battery_soc_mV_pct.value = 790;
    ASSERT_TRUE(governor_observe(&governor, &frame));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    commitment = governor_commitment_at(&governor, 0);
    ASSERT_TRUE(commitment != NULL);
    ASSERT_EQ_INT(770, commitment->assumption_snapshot.soc_permille);
    ASSERT_EQ_INT(6, commitment->assumption_snapshot.forecast_horizon_end_ns);
    ASSERT_EQ_INT(2, commitment->commit_time_ns);
    frame.battery_soc_mV_pct.value = 765;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 6));
    ASSERT_EQ_INT(1, governor_accuracy_count(&governor));
    ASSERT_EQ_INT(-5, governor_accuracy_at(&governor, 0)->soc_error_permille);
}

static void overrun_discards_shadow_commitments(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(false);
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 2;
    scenario.tasks.items[1] = scenario.tasks.items[0];
    scenario.tasks.items[1].task_id = 2;
    config.self_cost_cap_ns = 1000000;
    config.test_injected_cpu_ns_per_task = 2000000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(governor_observe(&governor, &scenario.initial));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OVERRUN,
                  governor_cycle(&governor, 0, false));
    ASSERT_EQ_INT(0, governor_commitment_count(&governor));
    ASSERT_EQ_INT(0, governor.accuracy_tracker.count);
    config.test_injected_cpu_ns_per_task = 0;
    governor.config = config;
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_EQ_INT(2, governor_commitment_count(&governor));
    ASSERT_EQ_INT(2, governor.accuracy_tracker.count);
}

static void calibration_sets_hard_cap_and_modeled_ledger_reserve(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    GovernorSelfCost cost;
    LedgerDimension cpu;
    LedgerDimension energy;
    setup_scenario(&scenario, 700);
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(governor_observe(&governor, &scenario.initial));
    ASSERT_TRUE(governor_calibrate_self_cost(&governor, 16,
                                             UINT64_C(1000000000), 2.0,
                                             &cost));
    ASSERT_TRUE(cost.measured_max_cpu_ns > 0);
    ASSERT_TRUE(cost.cap_cpu_ns > cost.measured_max_cpu_ns);
    ASSERT_EQ_INT(cost.cap_cpu_ns, governor.config.self_cost_cap_ns);
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_CPU, &cpu));
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_ENERGY, &energy));
    ASSERT_EQ_INT(cost.reserved_cpu_pct, cpu.committed);
    ASSERT_EQ_INT(cost.modeled_energy_mJ, energy.committed);
    ASSERT_TRUE(cost.modeled_energy_mJ > 0);
}

static void class_zero_runs_before_backlog_order(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(false);
    const Commitment *first;
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 2;
    scenario.tasks.items[1] = scenario.tasks.items[0];
    scenario.tasks.items[1].task_id = 2;
    scenario.tasks.items[1].criticality_class = 0;
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode) {
        scenario.tasks.items[0].modes[mode].resource_demand.cpu_pct = 90;
        scenario.tasks.items[1].modes[mode].resource_demand.cpu_pct = 30;
    }
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(governor_observe(&governor, &scenario.initial));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    first = governor_commitment_at(&governor, 0);
    ASSERT_TRUE(first != NULL);
    ASSERT_EQ_INT(2, first->task_id);
}

static void differing_horizons_share_conservative_energy_budget(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    TelemetryFrame frame;
    size_t task_index;
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 2;
    scenario.tasks.items[1] = scenario.tasks.items[0];
    scenario.tasks.items[1].task_id = 2;
    scenario.tasks.items[1].nominal_exec_ns = 8;
    scenario.tasks.items[1].deadline_ns = 20;
    for (task_index = 0; task_index < 2; ++task_index)
        for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode)
            scenario.tasks.items[task_index].modes[mode].energy_cost_mJ = 8000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe(&governor, &frame));
    frame.battery_soc_mV_pct.value = 810;
    ASSERT_TRUE(governor_observe(&governor, &frame));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_EQ_INT(1, governor_commitment_count(&governor));
}

static void prediction_update_cost_counts_toward_cycle_cap(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 0;
    config.self_cost_cap_ns = 1000000;
    config.test_injected_prediction_cpu_ns = 2000000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(governor_observe(&governor, &scenario.initial));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OVERRUN,
                  governor_cycle(&governor, 0, false));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK,
                  governor_cycle(&governor, 0, false));
}

static void observed_invalidation_releases_and_restores_through_gate(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    TelemetryFrame frame;
    LedgerDimension cpu;
    LedgerDimension energy;
    size_t mode;
    setup_scenario(&scenario, 700);
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode) {
        scenario.tasks.items[0].modes[mode].resource_demand.cpu_pct = 10;
        scenario.tasks.items[0].modes[mode].energy_cost_mJ = 100;
    }
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
    frame.battery_soc_mV_pct.value = 790;
    frame.battery_soc_mV_pct.timestamp_ns = 2;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    ASSERT_EQ_INT(INVALIDATION_OBSERVED_DEVIATION,
                  governor_revalidation_state_at(&governor, 0)->record.cause_type);
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_CPU, &cpu));
    ASSERT_EQ_INT(0, cpu.committed);
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_ENERGY, &energy));
    ASSERT_EQ_INT(100, energy.committed);
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_ENERGY, &energy));
    ASSERT_EQ_INT(100, energy.committed);
    frame.battery_soc_mV_pct.value = 800;
    frame.battery_soc_mV_pct.timestamp_ns = 4;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 4));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 4, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    frame.battery_soc_mV_pct.timestamp_ns = 6;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 6));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 6, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    frame.battery_soc_mV_pct.timestamp_ns = 8;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 8));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 8, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
    ASSERT_TRUE(!governor_revalidation_state_at(&governor, 0)->invalid);
}

static void class_zero_capacity_breach_enters_no_dispatch_safe_mode(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.items[0].criticality_class = 0;
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode)
        scenario.tasks.items[0].modes[mode].resource_demand.cpu_pct = 90;
    scenario.initial.cpu_avail_pct.value = 50;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(governor_observe_at(&governor, &scenario.initial, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_TRUE(governor_emergency_safe_mode(&governor));
    ASSERT_EQ_INT(0, governor_commitment_count(&governor));
}

static void false_predicted_risk_restores_at_horizon_via_gate(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    TelemetryFrame frame;
    size_t mode;
    setup_scenario(&scenario, 790);
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode)
        scenario.tasks.items[0].modes[mode].energy_cost_mJ = 100;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
    frame.battery_soc_mV_pct.value = 795;
    frame.battery_soc_mV_pct.timestamp_ns = 2;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    ASSERT_EQ_INT(INVALIDATION_PREDICTED_RISK,
                  governor_revalidation_state_at(&governor, 0)->record.cause_type);
    ASSERT_EQ_INT(6,
                  governor_revalidation_state_at(&governor, 0)->record.forecast_horizon_end);
    frame.battery_soc_mV_pct.value = 800;
    frame.battery_soc_mV_pct.timestamp_ns = 4;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 4));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 4, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    frame.battery_soc_mV_pct.timestamp_ns = 6;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 6));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 6, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
}

static void missing_memory_capacity_blocks_a_task(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    size_t mode;
    setup_scenario(&scenario, 700);
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode)
        scenario.tasks.items[0].modes[mode].resource_demand.mem_KB = 100;
    scenario.initial.mem_avail_KB.quality = TELEMETRY_QUALITY_MISSING;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(governor_observe_at(&governor, &scenario.initial, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_EQ_INT(0, governor_commitment_count(&governor));
}

static void emergency_safe_mode_discards_same_cycle_normal_dispatch(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    TelemetryFrame frame;
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 2;
    scenario.tasks.items[0].criticality_class = 0;
    scenario.tasks.items[0].primary_subsystem = SUBSYSTEM_ADCS;
    scenario.tasks.items[0].deadline_ns = 30;
    scenario.tasks.items[1] = scenario.tasks.items[0];
    scenario.tasks.items[1].task_id = 2;
    scenario.tasks.items[1].criticality_class = 1;
    scenario.tasks.items[1].primary_subsystem = SUBSYSTEM_PAYLOAD;
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode) {
        scenario.tasks.items[0].modes[mode].resource_demand.cpu_pct = 10;
        scenario.tasks.items[1].modes[mode].resource_demand.cpu_pct = 20;
    }
    scenario.initial.cpu_avail_pct.value = 15;
    config.thermal_limit_mC[SUBSYSTEM_ADCS] = 21000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_EQ_INT(1, governor_commitment_count(&governor));
    frame.cpu_avail_pct.value = 100;
    frame.temp_mC[SUBSYSTEM_ADCS].value = 21000;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(governor_emergency_safe_mode(&governor));
    ASSERT_EQ_INT(1, governor_commitment_count(&governor));
    ASSERT_TRUE(!governor_commitment_active(&governor, 1));
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 4));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 4, false));
    ASSERT_TRUE(governor_emergency_safe_mode(&governor));
    ASSERT_EQ_INT(1, governor_commitment_count(&governor));
}

static void clear_count_alone_cannot_restore_without_gate_pass(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    TelemetryFrame frame;
    uint64_t tick;
    setup_scenario(&scenario, 700);
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    frame.battery_soc_mV_pct.value = 790;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    frame.battery_soc_mV_pct.value = 800;
    for (tick = 4; tick <= 8; tick += 2) {
        ASSERT_TRUE(governor_observe_at(&governor, &frame, tick));
        if (tick == 8) {
            governor.prediction.soc.level = 0.0;
            governor.prediction.soc.trend = 0.0;
        }
        ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, tick, false));
    }
    ASSERT_EQ_INT(3,
                  governor_revalidation_state_at(&governor, 0)->record.clear_count);
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    ASSERT_TRUE(governor_revalidation_state_at(&governor, 0)->invalid);
}

static void aggregate_energy_deficit_releases_lowest_priority_first(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(false);
    TelemetryFrame frame;
    LedgerDimension energy;
    size_t task_index;
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 2;
    scenario.tasks.items[1] = scenario.tasks.items[0];
    scenario.tasks.items[1].task_id = 2;
    scenario.tasks.items[1].criticality_class = 2;
    for (task_index = 0; task_index < 2; ++task_index)
        for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode)
            scenario.tasks.items[task_index].modes[mode].energy_cost_mJ = 5000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
    ASSERT_TRUE(governor_commitment_active(&governor, 1));
    frame.battery_soc_mV_pct.value = 799;
    frame.battery_soc_mV_pct.timestamp_ns = 2;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
    ASSERT_TRUE(!governor_commitment_active(&governor, 1));
    ASSERT_EQ_INT(REVALIDATION_TRIGGER_PROTECTED_RESIDUAL,
        governor_revalidation_state_at(&governor, 1)->record.trigger_quantity);
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_ENERGY, &energy));
    ASSERT_EQ_INT(10000, energy.committed);
}

static void held_energy_requires_fresh_quality_ok_soc_sample(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(false);
    TelemetryFrame frame;
    LedgerDimension energy;
    size_t mode;
    setup_scenario(&scenario, 700);
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode)
        scenario.tasks.items[0].modes[mode].energy_cost_mJ = 100;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    frame.battery_soc_mV_pct.value = 790;
    frame.battery_soc_mV_pct.timestamp_ns = 2;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 4));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 4, false));
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_ENERGY, &energy));
    ASSERT_EQ_INT(100, energy.committed);
    frame.battery_soc_mV_pct.quality = TELEMETRY_QUALITY_STALE;
    frame.battery_soc_mV_pct.timestamp_ns = 6;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 6));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 6, false));
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_ENERGY, &energy));
    ASSERT_EQ_INT(100, energy.committed);
    frame.battery_soc_mV_pct.quality = TELEMETRY_QUALITY_OK;
    frame.battery_soc_mV_pct.timestamp_ns = 8;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 8));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 8, false));
    ASSERT_TRUE(ledger_query(&governor.plan.ledger, LEDGER_ENERGY, &energy));
    ASSERT_EQ_INT(0, energy.committed);
}

static void future_snapshot_does_not_invalidate_at_commit_time(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(true);
    TelemetryFrame frame;
    setup_scenario(&scenario, 700);
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    frame.battery_soc_mV_pct.value = 810;
    frame.battery_soc_mV_pct.timestamp_ns = 2;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_EQ_INT(1, governor_commitment_count(&governor));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
}

static void emergency_cancels_same_cycle_restoration(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(false);
    TelemetryFrame frame;
    uint64_t tick;
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 2;
    scenario.tasks.items[0].criticality_class = 0;
    scenario.tasks.items[0].primary_subsystem = SUBSYSTEM_ADCS;
    scenario.tasks.items[0].deadline_ns = 30;
    scenario.tasks.items[1] = scenario.tasks.items[0];
    scenario.tasks.items[1].task_id = 2;
    scenario.tasks.items[1].criticality_class = 1;
    scenario.tasks.items[1].primary_subsystem = SUBSYSTEM_PAYLOAD;
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode) {
        scenario.tasks.items[0].modes[mode].resource_demand.cpu_pct = 10;
        scenario.tasks.items[1].modes[mode].resource_demand.cpu_pct = 10;
    }
    config.thermal_limit_mC[SUBSYSTEM_ADCS] = 21000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 1));
    frame.fault_flags.value = UINT32_C(1) << SUBSYSTEM_PAYLOAD;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(!governor_commitment_active(&governor, 1));
    frame.fault_flags.value = 0;
    for (tick = 4; tick <= 8; tick += 2) {
        if (tick == 8) frame.temp_mC[SUBSYSTEM_ADCS].value = 21000;
        ASSERT_TRUE(governor_observe_at(&governor, &frame, tick));
        ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, tick, false));
    }
    ASSERT_TRUE(governor_emergency_safe_mode(&governor));
    ASSERT_TRUE(!governor_commitment_active(&governor, 1));
    ASSERT_TRUE(governor_revalidation_state_at(&governor, 1)->invalid);
}

static void restoration_waits_for_soc_refresh_without_cycle_error(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(false);
    TelemetryFrame frame;
    uint64_t tick;
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.items[0].deadline_ns = 30;
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode)
        scenario.tasks.items[0].modes[mode].energy_cost_mJ = 100;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    frame.battery_soc_mV_pct.value = 790;
    frame.battery_soc_mV_pct.timestamp_ns = 2;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    frame.battery_soc_mV_pct.value = 800;
    for (tick = 4; tick <= 8; tick += 2) {
        ASSERT_TRUE(governor_observe_at(&governor, &frame, tick));
        ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, tick, false));
    }
    ASSERT_TRUE(governor_revalidation_state_at(&governor, 0)->invalid);
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
    frame.battery_soc_mV_pct.timestamp_ns = 10;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 10));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 10, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
}

static void aggregate_cpu_deficit_sheds_newest_within_class(void)
{
    static Scenario scenario;
    Governor governor;
    GovernorConfig config = setup_config(false);
    TelemetryFrame frame;
    size_t mode;
    setup_scenario(&scenario, 700);
    scenario.tasks.count = 2;
    scenario.tasks.items[1] = scenario.tasks.items[0];
    scenario.tasks.items[1].task_id = 2;
    for (mode = 0; mode < TASK_EXECUTING_MODE_COUNT; ++mode) {
        scenario.tasks.items[0].modes[mode].resource_demand.cpu_pct = 20;
        scenario.tasks.items[1].modes[mode].resource_demand.cpu_pct = 20;
    }
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    frame = scenario.initial;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 0, false));
    frame.cpu_avail_pct.value = 30;
    ASSERT_TRUE(governor_observe_at(&governor, &frame, 2));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(&governor, 2, false));
    ASSERT_TRUE(governor_commitment_active(&governor, 0));
    ASSERT_TRUE(!governor_commitment_active(&governor, 1));
}

int main(void)
{
    RUN_TEST(predicted_soc_reaches_gate_before_selection);
    RUN_TEST(commitment_stores_predicted_assumption_snapshot);
    RUN_TEST(overrun_discards_shadow_commitments);
    RUN_TEST(calibration_sets_hard_cap_and_modeled_ledger_reserve);
    RUN_TEST(class_zero_runs_before_backlog_order);
    RUN_TEST(differing_horizons_share_conservative_energy_budget);
    RUN_TEST(prediction_update_cost_counts_toward_cycle_cap);
    RUN_TEST(observed_invalidation_releases_and_restores_through_gate);
    RUN_TEST(class_zero_capacity_breach_enters_no_dispatch_safe_mode);
    RUN_TEST(false_predicted_risk_restores_at_horizon_via_gate);
    RUN_TEST(missing_memory_capacity_blocks_a_task);
    RUN_TEST(emergency_safe_mode_discards_same_cycle_normal_dispatch);
    RUN_TEST(clear_count_alone_cannot_restore_without_gate_pass);
    RUN_TEST(aggregate_energy_deficit_releases_lowest_priority_first);
    RUN_TEST(held_energy_requires_fresh_quality_ok_soc_sample);
    RUN_TEST(future_snapshot_does_not_invalidate_at_commit_time);
    RUN_TEST(emergency_cancels_same_cycle_restoration);
    RUN_TEST(restoration_waits_for_soc_refresh_without_cycle_error);
    RUN_TEST(aggregate_cpu_deficit_sheds_newest_within_class);
    return test_failures != 0;
}
