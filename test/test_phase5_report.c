#include "test.h"
#include "phase5_report.h"
#include "report_runner.h"
#include <string.h>

static void setup(Scenario *scenario, Governor *governor)
{
    GovernorConfig config = {0};
    int subsystem;
    memset(scenario, 0, sizeof(*scenario));
    scenario->step_ns = 2;
    scenario->duration_ns = 12;
    scenario->battery_capacity_mJ = 100000;
    scenario->soc_floor_permille = 200;
    scenario->initial.battery_soc_mV_pct.value = 800;
    scenario->initial.cpu_avail_pct.value = 10;
    scenario->tasks.count = 1;
    scenario->tasks.items[0].task_id = 1;
    scenario->tasks.items[0].deadline_ns = 8;
    scenario->tasks.items[0].nominal_exec_ns = 6;
    scenario->tasks.items[0].criticality_class = 1;
    scenario->tasks.items[0].primary_subsystem = SUBSYSTEM_PAYLOAD;
    scenario->tasks.items[0].mission_value = 100;
    scenario->tasks.items[0].modes[0].value_fraction_permille = 1000;
    scenario->tasks.items[0].modes[0].resource_demand.cpu_pct = 20;
    config.variant = GOVERNOR_VARIANT_FIXED_EDF;
    config.alpha = 0.3;
    config.beta = 0.1;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    ASSERT_TRUE(governor_init(governor, scenario, &config));
    ASSERT_TRUE(governor_observe_at(governor, &scenario->initial, 0));
    ASSERT_EQ_INT(GOVERNOR_CYCLE_OK, governor_cycle(governor, 0, false));
}

static void auditor_checks_full_invariants_even_when_engine_skips_them(void)
{
    static Scenario scenario;
    static Governor governor;
    Phase5Metrics metrics;
    TelemetryFrame actual;
    setup(&scenario, &governor);
    phase5_metrics_init(&metrics, &scenario);
    actual = scenario.initial;
    ASSERT_TRUE(phase5_audit_instant(&metrics, &scenario, &governor, &actual, 0));
    ASSERT_EQ_INT(1, metrics.safety_violations);
    ASSERT_EQ_INT(1, metrics.protected_resource_violations);
    ASSERT_EQ_INT(1, metrics.safety_task_instants);
    ASSERT_TRUE(phase5_audit_instant(&metrics, &scenario, &governor, &actual, 0));
    ASSERT_EQ_INT(1, metrics.safety_task_instants);
    actual.cpu_avail_pct.value = 100;
    phase5_metrics_init(&metrics, &scenario);
    ASSERT_TRUE(phase5_audit_instant(&metrics, &scenario, &governor, &actual, 1));
    ASSERT_EQ_INT(0, metrics.safety_violations);
    actual.battery_soc_mV_pct.quality = TELEMETRY_QUALITY_MISSING;
    actual.temp_mC[SUBSYSTEM_PAYLOAD].quality = TELEMETRY_QUALITY_MISSING;
    ASSERT_TRUE(phase5_audit_instant(&metrics, &scenario, &governor, &actual, 2));
    ASSERT_EQ_INT(2, metrics.safety_violations);
    metrics.safety_task_instants = UINT64_MAX;
    ASSERT_TRUE(!phase5_audit_instant(&metrics, &scenario, &governor, &actual, 3));
}

static void terminal_counts_keep_rejection_separate_and_emit_na(void)
{
    static Scenario scenario;
    static Governor governor;
    Phase5Metrics metrics;
    FILE *output = tmpfile();
    char line[2048];
    setup(&scenario, &governor);
    phase5_metrics_init(&metrics, &scenario);
    governor.plan.execution[0].phase = EXECUTION_REJECTED;
    ASSERT_TRUE(phase5_metrics_finish(&metrics, &scenario, &governor));
    ASSERT_EQ_INT(1, metrics.rejected_by_design);
    ASSERT_EQ_INT(0, metrics.deadline_misses);
    ASSERT_TRUE(output != NULL);
    ASSERT_TRUE(phase5_metrics_write_csv(output, "unit", "fixed", &metrics));
    rewind(output);
    ASSERT_TRUE(fgets(line, sizeof(line), output) != NULL);
    ASSERT_TRUE(strstr(line, ",NA,") != NULL);
    ASSERT_TRUE(strstr(line, ",NA,NA\n") != NULL);
    fclose(output);
    governor.plan.execution[0].phase = EXECUTION_PAUSED;
    phase5_metrics_init(&metrics, &scenario);
    ASSERT_TRUE(phase5_metrics_finish(&metrics, &scenario, &governor));
    ASSERT_EQ_INT(1, metrics.deadline_misses);
    ASSERT_EQ_INT(0, metrics.rejected_by_design);
}

static void auditor_counts_self_cost_against_the_unprotected_pool(void)
{
    static Scenario scenario;
    static Governor governor;
    Phase5Metrics metrics;
    setup(&scenario, &governor);
    scenario.initial.cpu_avail_pct.value = 40;
    governor.config.protected_reserve[LEDGER_CPU] = 20;
    governor.plan.ledger.self_cpu_pct = 5;
    phase5_metrics_init(&metrics, &scenario);
    ASSERT_TRUE(phase5_audit_instant(&metrics, &scenario, &governor,
                                    &scenario.initial, 0));
    ASSERT_EQ_INT(1, metrics.protected_resource_violations);
}

static void cycle_accounting_tracks_recovery_and_rejects_overflow(void)
{
    static Scenario scenario;
    static Governor governor;
    Phase5Metrics metrics;
    setup(&scenario, &governor);
    phase5_metrics_init(&metrics, &scenario);
    ASSERT_TRUE(phase5_note_fault(&metrics, 3, 4));
    governor.last_records[0].has_invalidation = true;
    governor.last_records[0].state_before = EXECUTION_RUNNING;
    governor.last_records[0].state_after = EXECUTION_PAUSED;
    governor.last_cycle_cpu_ns = 500;
    ASSERT_TRUE(phase5_note_cycle(&metrics, &scenario, &governor, 4));
    ASSERT_EQ_INT(1, metrics.replan_count);
    ASSERT_EQ_INT(1, metrics.recovery_latency_ns);
    ASSERT_EQ_INT(500, metrics.cpu_time_ns);
    ASSERT_TRUE(phase5_metrics_finish(&metrics, &scenario, &governor));
    ASSERT_TRUE(!metrics.recovery_latency_censored);
    phase5_metrics_init(&metrics, &scenario);
    ASSERT_TRUE(phase5_note_fault(&metrics, 3, 4));
    ASSERT_TRUE(phase5_metrics_finish(&metrics, &scenario, &governor));
    ASSERT_TRUE(metrics.recovery_latency_censored);
    ASSERT_EQ_INT(9, metrics.recovery_latency_ns);
    metrics.cpu_time_ns = UINT64_MAX;
    ASSERT_TRUE(!phase5_note_cycle(&metrics, &scenario, &governor, 4));
}

static size_t report_row_count(FILE *stream)
{
    char line[2048];
    size_t rows = 0;
    rewind(stream);
    while (fgets(line, sizeof(line), stream) != NULL) ++rows;
    return rows > 0 ? rows - 1 : 0;
}

static bool report_has_pair(FILE *stream, const char *scenario, const char *variant)
{
    char line[2048];
    char key[128];
    if (snprintf(key, sizeof(key), "%s,%s,", scenario, variant) < 0) return false;
    rewind(stream);
    while (fgets(line, sizeof(line), stream) != NULL)
        if (strncmp(line, key, strlen(key)) == 0) return true;
    return false;
}

static void matrix_has_all_pairs_and_reports_real_losses(void)
{
    FILE *stream = tmpfile();
    FILE *summary = tmpfile();
    Phase5Metrics full = {0}, comparator = {0};
    char line[2048];
    ASSERT_TRUE(stream != NULL && summary != NULL);
    ASSERT_TRUE(report_runner_write(stream));
    ASSERT_EQ_INT(32, report_row_count(stream));
    ASSERT_TRUE(report_has_pair(stream, "compound_stress", "SAGE_FULL"));
    ASSERT_TRUE(report_has_pair(stream, "compound_stress", "GOMX_STYLE"));
    ASSERT_TRUE(report_has_pair(stream, "false_alarm_restoration", "RANJBAR_STYLE"));
    rewind(stream);
    ASSERT_TRUE(fgets(line, sizeof(line), stream) != NULL);
    ASSERT_TRUE(strstr(line, "scenario,variant,task_count,") == line);
    ASSERT_TRUE(strstr(line, ",prediction_abs_error\n") != NULL);
    full.critical_task_count = 1;
    comparator.critical_task_count = 1;
    comparator.critical_completed_count = 1;
    full.safety_violations = 2;
    ASSERT_TRUE(report_runner_compare(summary, "synthetic", &full,
                                      "FIXED_EDF", &comparator));
    rewind(summary);
    ASSERT_TRUE(fgets(line, sizeof(line), summary) != NULL);
    ASSERT_TRUE(strstr(line, "trails") != NULL);
    fclose(summary);
    fclose(stream);
}

int main(void)
{
    RUN_TEST(auditor_checks_full_invariants_even_when_engine_skips_them);
    RUN_TEST(terminal_counts_keep_rejection_separate_and_emit_na);
    RUN_TEST(auditor_counts_self_cost_against_the_unprotected_pool);
    RUN_TEST(cycle_accounting_tracks_recovery_and_rejects_overflow);
    RUN_TEST(matrix_has_all_pairs_and_reports_real_losses);
    return test_failures != 0;
}
