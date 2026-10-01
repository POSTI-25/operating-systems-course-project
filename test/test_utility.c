#include "test.h"
#include "core/utility.h"

#include <stdint.h>

static LedgerConfig ledger_config(void)
{
    LedgerConfig setup = {0};
    setup.capacity[LEDGER_CPU] = 100;
    setup.capacity[LEDGER_MEMORY] = 1000;
    setup.capacity[LEDGER_STORAGE] = 1000;
    setup.capacity[LEDGER_SENSOR] = 0x0f;
    setup.capacity[LEDGER_COMM] = 100;
    setup.battery_capacity_mJ = 100000;
    setup.soc_floor_permille = 200;
    return setup;
}

static void unsafe_maximum_value_never_reaches_ranking(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 4, .nominal_exec_ns = 2,
        .primary_subsystem = SUBSYSTEM_PAYLOAD };
    TelemetryFrame telemetry;
    GateContext gate = {0};
    UtilityContext utility = {0};
    Candidate candidates[2] = {
        { .task_id = 4, .mode = MODE_FULL, .delivered_value = 1000,
          .predicted_cost = { .energy_cost_mJ = 70000 } },
        { .task_id = 4, .mode = MODE_DEGRADED, .delivered_value = 400,
          .predicted_cost = { .energy_cost_mJ = 100 } }
    };
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    SelectionResult selected;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    gate.telemetry = &telemetry;
    gate.ledger = &ledger;
    gate.remaining_thermal_margin_mC[SUBSYSTEM_PAYLOAD] = 100;
    gate.predicted_completion_time_ns = 2;
    gate.absolute_deadline_ns = 10;
    survivors = gate_filter(candidates, 2, &task, &gate, &filtered);
    utility.time_remaining_to_deadline_ns = 10;
    utility.energy_capacity_mJ = 60000;
    utility.remaining_thermal_margin_mC[SUBSYSTEM_PAYLOAD] = 100;
    utility.free_cpu_pct = 100;
    utility.free_mem_KB = 1000;
    utility.free_storage_KB = 1000;
    utility.free_comm_pct = 100;
    ASSERT_TRUE(utility_select(survivors, &task, &utility, &selected));
    ASSERT_EQ_INT(SELECTION_EXECUTING, selected.outcome);
    ASSERT_EQ_INT(MODE_DEGRADED, selected.selected.mode);
}

static void option_a_uses_natural_denominators(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 5, .nominal_exec_ns = 5,
        .primary_subsystem = SUBSYSTEM_BUS };
    TelemetryFrame telemetry;
    GateContext gate = {0};
    UtilityContext utility = {0};
    Candidate candidate = { .task_id = 5, .mode = MODE_FULL, .delivered_value = 100,
        .predicted_cost = { .energy_cost_mJ = 50, .thermal_effect_mC = 25,
            .resource_demand = { .cpu_pct = 10 } } };
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    SelectionResult selected;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    gate.telemetry = &telemetry;
    gate.ledger = &ledger;
    gate.remaining_thermal_margin_mC[SUBSYSTEM_BUS] = 50;
    gate.predicted_completion_time_ns = 5;
    gate.absolute_deadline_ns = 10;
    survivors = gate_filter(&candidate, 1, &task, &gate, &filtered);
    utility.time_remaining_to_deadline_ns = 10;
    utility.fault_flags = 0x02U;
    utility.remaining_thermal_margin_mC[SUBSYSTEM_BUS] = 50;
    utility.energy_capacity_mJ = 100;
    utility.free_cpu_pct = 20;
    utility.free_mem_KB = 1000;
    utility.free_storage_KB = 1000;
    utility.free_comm_pct = 100;
    ASSERT_TRUE(utility_rank(survivors, &task, &utility, &selected));
    ASSERT_TRUE(selected.utility_score == 248.5);
}

static void fallback_deferred_then_rejected_then_emergency(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 6, .nominal_exec_ns = 4,
        .criticality_class = 1, .primary_subsystem = SUBSYSTEM_ADCS };
    TelemetryFrame telemetry;
    GateContext gate = {0};
    UtilityContext utility = {0};
    Candidate unsafe = { .task_id = 6, .mode = MODE_FULL,
        .predicted_cost = { .energy_cost_mJ = 70000 } };
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    SelectionResult selected;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    gate.telemetry = &telemetry;
    gate.ledger = &ledger;
    gate.predicted_completion_time_ns = 4;
    gate.absolute_deadline_ns = 10;
    survivors = gate_filter(&unsafe, 1, &task, &gate, &filtered);
    utility.time_remaining_to_deadline_ns = 10;
    utility.time_until_next_tick_ns = 2;
    ASSERT_TRUE(utility_select(survivors, &task, &utility, &selected));
    ASSERT_EQ_INT(SELECTION_DEFERRED, selected.outcome);
    utility.time_remaining_to_deadline_ns = 5;
    ASSERT_TRUE(utility_select(survivors, &task, &utility, &selected));
    ASSERT_EQ_INT(SELECTION_REJECTED, selected.outcome);
    task.criticality_class = 0;
    ASSERT_TRUE(utility_select(survivors, &task, &utility, &selected));
    ASSERT_EQ_INT(SELECTION_EMERGENCY_SAFE_MODE_REQUESTED, selected.outcome);
}

static void deadline_urgency_uses_full_uint64_denominator(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 8, .nominal_exec_ns = UINT64_MAX / 2U,
        .primary_subsystem = SUBSYSTEM_BUS };
    TelemetryFrame telemetry;
    GateContext gate = {0};
    UtilityContext utility = {0};
    Candidate candidate = { .task_id = 8, .mode = MODE_FULL, .delivered_value = 100 };
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    SelectionResult selected;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    gate.telemetry = &telemetry;
    gate.ledger = &ledger;
    gate.predicted_completion_time_ns = 1;
    gate.absolute_deadline_ns = 2;
    survivors = gate_filter(&candidate, 1, &task, &gate, &filtered);
    utility.time_remaining_to_deadline_ns = UINT64_MAX;
    utility.energy_capacity_mJ = 60000;
    utility.free_cpu_pct = 100;
    utility.free_mem_KB = 1000;
    utility.free_storage_KB = 1000;
    utility.free_comm_pct = 100;
    ASSERT_TRUE(utility_rank(survivors, &task, &utility, &selected));
    ASSERT_TRUE(selected.utility_score > 149.9 && selected.utility_score < 150.1);
}

static void masked_terms_leave_delivered_value_base_intact(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 11, .nominal_exec_ns = 2,
        .primary_subsystem = SUBSYSTEM_BUS };
    TelemetryFrame telemetry;
    GateContext gate = {0};
    UtilityContext utility = {0};
    Candidate candidate = { .task_id = 11, .mode = MODE_FULL,
        .delivered_value = 100,
        .predicted_cost = { .energy_cost_mJ = 50 } };
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    SelectionResult selected;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    gate.telemetry = &telemetry;
    gate.ledger = &ledger;
    gate.absolute_deadline_ns = 10;
    survivors = gate_filter(&candidate, 1, &task, &gate, &filtered);
    utility.fault_flags = UINT32_C(1) << SUBSYSTEM_BUS;
    utility.energy_capacity_mJ = 100;
    ASSERT_TRUE(utility_select_masked(survivors, &task, &utility, 0, &selected));
    ASSERT_TRUE(selected.utility_score == 100.0);
    ASSERT_TRUE(utility_select_masked(survivors, &task, &utility,
        UTILITY_TERM_FAULT, &selected));
    ASSERT_TRUE(selected.utility_score == 200.0);
    ASSERT_TRUE(utility_select_masked(survivors, &task, &utility,
        UTILITY_TERM_ENERGY, &selected));
    ASSERT_TRUE(selected.utility_score == 99.5);
}

static void exact_score_tie_prefers_full_mode(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 9, .nominal_exec_ns = 1,
        .primary_subsystem = SUBSYSTEM_BUS };
    TelemetryFrame telemetry;
    GateContext gate = {0};
    UtilityContext utility = {0};
    Candidate candidates[2] = {
        { .task_id = 9, .mode = MODE_DEGRADED, .delivered_value = 100 },
        { .task_id = 9, .mode = MODE_FULL, .delivered_value = 100 }
    };
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    SelectionResult selected;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    gate.telemetry = &telemetry;
    gate.ledger = &ledger;
    gate.predicted_completion_time_ns = 1;
    gate.absolute_deadline_ns = 2;
    survivors = gate_filter(candidates, 2, &task, &gate, &filtered);
    utility.time_remaining_to_deadline_ns = 2;
    utility.energy_capacity_mJ = 60000;
    utility.free_cpu_pct = 100;
    utility.free_mem_KB = 1000;
    utility.free_storage_KB = 1000;
    utility.free_comm_pct = 100;
    ASSERT_TRUE(utility_rank(survivors, &task, &utility, &selected));
    ASSERT_EQ_INT(MODE_FULL, selected.selected.mode);
}

int main(void)
{
    RUN_TEST(unsafe_maximum_value_never_reaches_ranking);
    RUN_TEST(option_a_uses_natural_denominators);
    RUN_TEST(fallback_deferred_then_rejected_then_emergency);
    RUN_TEST(deadline_urgency_uses_full_uint64_denominator);
    RUN_TEST(masked_terms_leave_delivered_value_base_intact);
    RUN_TEST(exact_score_tie_prefers_full_mode);
    return test_failures != 0;
}
