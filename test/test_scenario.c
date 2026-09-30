#include "test.h"
#include "replay.h"
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
    ASSERT_EQ_INT(100000, scenario.battery_capacity_mJ);
    ASSERT_EQ_INT(200, scenario.soc_floor_permille);
    ASSERT_EQ_INT(1, scenario.tasks.count);
    ASSERT_EQ_INT(SUBSYSTEM_PAYLOAD, scenario.tasks.items[0].primary_subsystem);
    ASSERT_EQ_INT(7, scenario.tasks.mode_masks[0]);
    ASSERT_TRUE(telemetry_frame_get(&scenario.initial, TELEMETRY_TEMP_MC,
                                    SUBSYSTEM_ADCS, &channel));
    ASSERT_EQ_INT(23000, channel.value);
    ASSERT_EQ_INT(100, scenario.tasks.items[0].modes[TASK_MODE_FULL].energy_cost_mJ);
    ASSERT_EQ_INT(800, scenario.initial.battery_soc_mV_pct.value);
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

static void battery_capacity_above_ledger_range_is_rejected(void)
{
    FILE *input = tmpfile();
    Scenario scenario;
    char error[128];
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(fputs("BATTERY_CAPACITY_MJ 9223372036854775808\n", input) >= 0);
    rewind(input);
    ASSERT_TRUE(!scenario_load_file(input, &scenario, error, sizeof(error)));
    ASSERT_TRUE(strstr(error, "invalid BATTERY_CAPACITY_MJ") != NULL);
    fclose(input);
}

typedef struct {
    bool eclipse, soc, thermal, lower, fault, residual;
    size_t tasks, faults, updates;
} ScenarioCapture;

static bool capture_purpose(const Governor *governor, const TelemetryFrame *frame,
                            uint64_t now_ns, void *context)
{
    ScenarioCapture *capture = context;
    size_t i;
    (void)now_ns;
    capture->eclipse |= frame->eclipse.value != 0;
    for (i = 0; i < governor->last_record_count; ++i) {
        const LogRecord *record = governor_log_record_at(governor, i);
        int m;
        capture->fault |= record->trigger == DECISION_TRIGGER_FAULT;
        capture->lower |= record->has_selected &&
            (record->selected_mode == MODE_THROTTLED || record->selected_mode == MODE_DEGRADED);
        if (!record->has_candidates) continue;
        for (m = 0; m < TASK_EXECUTING_MODE_COUNT; ++m) {
            capture->soc |= record->candidates[m].failed_predicate == GATE_SOC_FLOOR;
            capture->thermal |= record->candidates[m].failed_predicate == GATE_THERMAL_CEILING;
            capture->residual |= record->candidates[m].failed_predicate == GATE_PROTECTED_RESIDUAL;
        }
    }
    return true;
}

static bool load_and_replay(const char *path, ScenarioCapture *capture)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    FILE *input = fopen(path, "r");
    char error[128];
    int subsystem;
    bool loaded;
    if (input == NULL) return false;
    loaded = scenario_load_file(input, &scenario, error, sizeof(error));
    fclose(input);
    if (!loaded) return false;
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    capture->tasks = scenario.tasks.count;
    capture->faults = scenario.fault_count;
    capture->updates = scenario.update_count;
    return governor_init(&governor, &scenario, &config) &&
        replay_run(&scenario, &governor, NULL, capture_purpose, capture);
}

static void focused_catalog_has_minimal_purpose_evidence(void)
{
    ScenarioCapture capture = {0};
    ASSERT_TRUE(load_and_replay("scenarios/low_soc_eclipse.scn", &capture));
    ASSERT_TRUE(capture.eclipse && capture.soc);
    ASSERT_EQ_INT(1, capture.tasks);
    ASSERT_EQ_INT(0, capture.updates + capture.faults);
    capture = (ScenarioCapture){0};
    ASSERT_TRUE(load_and_replay("scenarios/high_thermal_load.scn", &capture));
    ASSERT_TRUE(capture.thermal && capture.lower);
    ASSERT_EQ_INT(1, capture.tasks);
    ASSERT_EQ_INT(0, capture.updates + capture.faults);
    capture = (ScenarioCapture){0};
    ASSERT_TRUE(load_and_replay("scenarios/sudden_fault.scn", &capture));
    ASSERT_TRUE(capture.fault);
    ASSERT_EQ_INT(1, capture.tasks);
    ASSERT_EQ_INT(1, capture.faults);
    capture = (ScenarioCapture){0};
    ASSERT_TRUE(load_and_replay("scenarios/communication_blackout.scn", &capture));
    ASSERT_TRUE(capture.residual && !capture.soc);
    ASSERT_EQ_INT(1, capture.tasks);
    ASSERT_EQ_INT(0, capture.updates + capture.faults);
    capture = (ScenarioCapture){0};
    ASSERT_TRUE(load_and_replay("scenarios/competing_payload_tasks.scn", &capture));
    ASSERT_TRUE(capture.residual && !capture.soc);
    ASSERT_EQ_INT(2, capture.tasks);
    ASSERT_EQ_INT(0, capture.updates + capture.faults);
}

int main(void)
{
    RUN_TEST(normal_orbit_loads_complete_state_and_tasks);
    RUN_TEST(malformed_directive_reports_line);
    RUN_TEST(duplicate_initial_is_rejected);
    RUN_TEST(overflowing_duration_is_rejected);
    RUN_TEST(battery_capacity_above_ledger_range_is_rejected);
    RUN_TEST(focused_catalog_has_minimal_purpose_evidence);
    return test_failures != 0;
}
