#include "test.h"
#include "core/gate.h"
#include "core/utility.h"

#include <string.h>

static LedgerConfig ledger_config(void)
{
    LedgerConfig setup = {0};
    setup.capacity[LEDGER_CPU] = 100;
    setup.capacity[LEDGER_MEMORY] = 1000;
    setup.capacity[LEDGER_STORAGE] = 1000;
    setup.capacity[LEDGER_SENSOR] = 0x0f;
    setup.capacity[LEDGER_COMM] = 100;
    setup.protected_reserve[LEDGER_CPU] = 20;
    setup.battery_capacity_mJ = 100000;
    setup.soc_floor_permille = 200;
    return setup;
}

static void rejects_each_specific_predicate(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 1, .period_ns = 10, .deadline_ns = 9,
        .nominal_exec_ns = 2, .mission_value = 900,
        .primary_subsystem = SUBSYSTEM_PAYLOAD };
    TelemetryFrame telemetry;
    GateContext context = {0};
    Candidate candidate = { .task_id = 1, .mode = MODE_FULL, .delivered_value = 900 };
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    context.telemetry = &telemetry;
    context.ledger = &ledger;
    context.remaining_thermal_margin_mC[SUBSYSTEM_PAYLOAD] = 100;
    context.predicted_completion_time_ns = 5;
    context.absolute_deadline_ns = 10;
    candidate.predicted_cost.energy_cost_mJ = 70000;
    ASSERT_EQ_INT(GATE_SOC_FLOOR,
                  gate_evaluate(&candidate, &task, &context).failed_predicate);
    candidate.predicted_cost.energy_cost_mJ = 100;
    candidate.predicted_cost.thermal_effect_mC = 101;
    ASSERT_EQ_INT(GATE_THERMAL_CEILING,
                  gate_evaluate(&candidate, &task, &context).failed_predicate);
    candidate.predicted_cost.thermal_effect_mC = 50;
    ASSERT_TRUE(ledger_set_rate_capacities(&ledger, 100, 0));
    candidate.predicted_cost.resource_demand.comm_pct = 1;
    ASSERT_EQ_INT(GATE_PROTECTED_RESIDUAL,
                  gate_evaluate(&candidate, &task, &context).failed_predicate);
    ASSERT_TRUE(ledger_set_rate_capacities(&ledger, 100, 100));
    context.predicted_completion_time_ns = 11;
    ASSERT_EQ_INT(GATE_DEADLINE,
                  gate_evaluate(&candidate, &task, &context).failed_predicate);
    context.predicted_completion_time_ns = 5;
    ASSERT_TRUE(gate_evaluate(&candidate, &task, &context).passed);
}

static void only_gate_survivors_reach_the_set(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 2, .period_ns = 10, .deadline_ns = 9,
        .nominal_exec_ns = 2, .mission_value = 1000,
        .primary_subsystem = SUBSYSTEM_PAYLOAD };
    TelemetryFrame telemetry;
    GateContext context = {0};
    Candidate candidates[2] = {
        { .task_id = 2, .mode = MODE_FULL, .delivered_value = 1000,
          .predicted_cost = { .energy_cost_mJ = 70000 } },
        { .task_id = 2, .mode = MODE_DEGRADED, .delivered_value = 400,
          .predicted_cost = { .energy_cost_mJ = 100 } }
    };
    GateFilterResult result;
    const SurvivorSet *survivors;
    Candidate got;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    context.telemetry = &telemetry;
    context.ledger = &ledger;
    context.remaining_thermal_margin_mC[SUBSYSTEM_PAYLOAD] = 100;
    context.predicted_completion_time_ns = 5;
    context.absolute_deadline_ns = 10;
    survivors = gate_filter(candidates, 2, &task, &context, &result);
    ASSERT_TRUE(survivors != NULL);
    ASSERT_EQ_INT(GATE_SOC_FLOOR, result.evaluations[0].failed_predicate);
    ASSERT_EQ_INT(1, gate_survivor_count(survivors));
    ASSERT_TRUE(gate_survivor_at(survivors, 0, &got));
    ASSERT_EQ_INT(MODE_DEGRADED, got.mode);
}

static void mutated_survivor_is_rejected(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 3, .primary_subsystem = SUBSYSTEM_PAYLOAD };
    TelemetryFrame telemetry;
    GateContext context = {0};
    Candidate candidate = { .task_id = 3, .mode = MODE_FULL,
        .delivered_value = UINT32_C(0x12345678) };
    GateFilterResult result;
    const SurvivorSet *survivors;
    unsigned char value_bytes[sizeof(candidate.delivered_value)];
    UtilityContext utility = {0};
    SelectionResult selected;
    size_t i;
    bool found = false;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    context.telemetry = &telemetry;
    context.ledger = &ledger;
    context.remaining_thermal_margin_mC[SUBSYSTEM_PAYLOAD] = 100;
    context.absolute_deadline_ns = 10;
    survivors = gate_filter(&candidate, 1, &task, &context, &result);
    ASSERT_TRUE(survivors != NULL);
    ASSERT_EQ_INT(1, gate_survivor_count(survivors));
    memcpy(value_bytes, &candidate.delivered_value, sizeof(value_bytes));
    for (i = 0; i + sizeof(value_bytes) <= sizeof(result.survivor_storage); ++i) {
        if (memcmp(&result.survivor_storage[i], value_bytes,
                   sizeof(value_bytes)) == 0) {
            result.survivor_storage[i] ^= 1U;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
    ASSERT_EQ_INT(0, gate_survivor_count(survivors));
    ASSERT_TRUE(!gate_survivor_valid(survivors));
    ASSERT_TRUE(!utility_select(survivors, &task, &utility, &selected));
}

static void missing_predicate_channels_fail_conservatively(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 4, .primary_subsystem = SUBSYSTEM_PAYLOAD };
    TelemetryFrame telemetry;
    GateContext context = {0};
    Candidate candidate = { .task_id = 4, .mode = MODE_FULL };
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 800;
    telemetry.temp_mC[SUBSYSTEM_PAYLOAD].value = 20000;
    context.telemetry = &telemetry;
    context.ledger = &ledger;
    context.remaining_thermal_margin_mC[SUBSYSTEM_PAYLOAD] = 1000;
    context.absolute_deadline_ns = 10;
    ASSERT_TRUE(gate_evaluate(&candidate, &task, &context).passed);
    telemetry.battery_soc_mV_pct.quality = TELEMETRY_QUALITY_MISSING;
    ASSERT_EQ_INT(GATE_SOC_FLOOR,
                  gate_evaluate(&candidate, &task, &context).failed_predicate);
    telemetry.battery_soc_mV_pct.quality = TELEMETRY_QUALITY_OK;
    telemetry.temp_mC[SUBSYSTEM_PAYLOAD].quality = TELEMETRY_QUALITY_MISSING;
    ASSERT_EQ_INT(GATE_THERMAL_CEILING,
                  gate_evaluate(&candidate, &task, &context).failed_predicate);
}

static void masked_gate_checks_only_enabled_existing_predicates(void)
{
    Ledger ledger;
    LedgerConfig setup = ledger_config();
    Task task = { .task_id = 10, .primary_subsystem = SUBSYSTEM_PAYLOAD };
    TelemetryFrame telemetry;
    GateContext context = {0};
    Candidate candidate = { .task_id = 10, .mode = MODE_FULL,
        .predicted_cost = { .thermal_effect_mC = 200 } };
    GateFilterResult filtered;
    const SurvivorSet *survivors;
    ASSERT_TRUE(ledger_init(&ledger, &setup));
    ASSERT_TRUE(ledger_update_energy(&ledger, 800, 0, 0));
    telemetry_frame_init(&telemetry);
    telemetry.battery_soc_mV_pct.value = 150;
    context.telemetry = &telemetry;
    context.ledger = &ledger;
    context.remaining_thermal_margin_mC[SUBSYSTEM_PAYLOAD] = 100;
    context.absolute_deadline_ns = 10;
    survivors = gate_filter_masked(&candidate, 1, &task, &context,
                                   GATE_MASK_ALL, &filtered);
    ASSERT_TRUE(survivors != NULL);
    ASSERT_EQ_INT(GATE_SOC_FLOOR, filtered.evaluations[0].failed_predicate);
    survivors = gate_filter_masked(&candidate, 1, &task, &context, 0, &filtered);
    ASSERT_EQ_INT(1, gate_survivor_count(survivors));
    survivors = gate_filter_masked(&candidate, 1, &task, &context,
        GATE_MASK_SOC | GATE_MASK_DEADLINE, &filtered);
    ASSERT_EQ_INT(GATE_SOC_FLOOR, filtered.evaluations[0].failed_predicate);
    survivors = gate_filter_masked(&candidate, 1, &task, &context,
        GATE_MASK_THERMAL | GATE_MASK_PROTECTED | GATE_MASK_DEADLINE,
        &filtered);
    ASSERT_EQ_INT(GATE_THERMAL_CEILING,
                  filtered.evaluations[0].failed_predicate);
}

int main(void)
{
    RUN_TEST(rejects_each_specific_predicate);
    RUN_TEST(only_gate_survivors_reach_the_set);
    RUN_TEST(mutated_survivor_is_rejected);
    RUN_TEST(missing_predicate_channels_fail_conservatively);
    RUN_TEST(masked_gate_checks_only_enabled_existing_predicates);
    return test_failures != 0;
}
