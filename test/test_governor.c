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

int main(void)
{
    RUN_TEST(predicted_soc_reaches_gate_before_selection);
    RUN_TEST(commitment_stores_predicted_assumption_snapshot);
    RUN_TEST(overrun_discards_shadow_commitments);
    RUN_TEST(calibration_sets_hard_cap_and_modeled_ledger_reserve);
    RUN_TEST(class_zero_runs_before_backlog_order);
    RUN_TEST(differing_horizons_share_conservative_energy_budget);
    RUN_TEST(prediction_update_cost_counts_toward_cycle_cap);
    return test_failures != 0;
}
