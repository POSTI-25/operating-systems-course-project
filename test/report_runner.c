#include "report_runner.h"
#include "replay.h"

#include <inttypes.h>
#include <string.h>

#define REPORT_TICK_NS UINT64_C(10000000)
#define REPORT_CALIBRATION_RUNS 64U
#define REPORT_POWER_W 2.0

typedef struct {
    Phase5Metrics *metrics;
    const Scenario *scenario;
} ReportContext;

static bool report_cycle(const Governor *governor, const TelemetryFrame *frame,
                         uint64_t now_ns, void *context)
{
    ReportContext *report = context;
    return phase5_note_cycle(report->metrics, report->scenario, governor,
                             now_ns) &&
        phase5_audit_instant(report->metrics, report->scenario, governor,
                             frame, now_ns);
}

static bool run_report_pair(const char *path, GovernorVariant variant,
                            FILE *output)
{
    static Scenario scenario;
    static Governor governor;
    Phase5Metrics metrics;
    ReportContext context;
    GovernorConfig config = {0};
    GovernorSelfCost calibration;
    FILE *input;
    char error[128];
    const char *name;
    int subsystem;
    size_t fault;
    input = fopen(path, "r");
    if (input == NULL) return false;
    if (!scenario_load_file(input, &scenario, error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", path, error);
        fclose(input);
        return false;
    }
    fclose(input);
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    config.variant = variant;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    if (!governor_init(&governor, &scenario, &config) ||
        !governor_observe_at(&governor, &scenario.initial, 0) ||
        !governor_calibrate_self_cost(&governor, REPORT_CALIBRATION_RUNS,
            REPORT_TICK_NS, REPORT_POWER_W, &calibration)) {
        fprintf(stderr, "calibration failed: %s variant=%d\n", path, (int)variant);
        return false;
    }
    /* The calibration sample is separate from the replay's first observation. */
    governor.pending_prediction_cpu_ns = 0;
    governor.pending_prediction_wall_ns = 0;
    phase5_metrics_init(&metrics, &scenario);
    for (fault = 0; fault < scenario.fault_count; ++fault)
        if (!phase5_note_fault(&metrics, scenario.faults[fault].time_ns,
                               scenario.faults[fault].set_mask)) return false;
    context.metrics = &metrics;
    context.scenario = &scenario;
    if (!replay_run(&scenario, &governor, NULL, report_cycle, &context) ||
        !phase5_metrics_finish(&metrics, &scenario, &governor)) {
        fprintf(stderr, "replay/audit failed: %s variant=%d\n", path, (int)variant);
        return false;
    }
    name = variant == GOVERNOR_VARIANT_SAGE_FULL ? "SAGE_FULL" :
        variant == GOVERNOR_VARIANT_FIXED_EDF ? "FIXED_EDF" :
        variant == GOVERNOR_VARIANT_GOMX_STYLE ? "GOMX_STYLE" : "RANJBAR_STYLE";
    return phase5_metrics_write_csv(output, scenario.name, name, &metrics);
}

bool report_runner_write(FILE *output)
{
    static const char *paths[] = {
        "scenarios/normal_orbit.scn", "scenarios/low_soc_eclipse.scn",
        "scenarios/high_thermal_load.scn", "scenarios/sudden_fault.scn",
        "scenarios/communication_blackout.scn",
        "scenarios/competing_payload_tasks.scn",
        "scenarios/compound_stress.scn", "scenarios/false_alarm_restoration.scn"
    };
    size_t scenario;
    int variant;
    if (output == NULL || !phase5_metrics_write_header(output)) return false;
    for (scenario = 0; scenario < sizeof(paths) / sizeof(paths[0]); ++scenario)
        for (variant = GOVERNOR_VARIANT_SAGE_FULL;
             variant <= GOVERNOR_VARIANT_RANJBAR_STYLE; ++variant)
            if (!run_report_pair(paths[scenario], (GovernorVariant)variant,
                                 output)) return false;
    return true;
}

bool report_runner_compare(FILE *output, const char *scenario,
    const Phase5Metrics *full, const char *comparator_name,
    const Phase5Metrics *comparator)
{
    bool critical_loss;
    bool safety_loss;
    const char *status;
    if (output == NULL || scenario == NULL || full == NULL ||
        comparator_name == NULL || comparator == NULL) return false;
    critical_loss = full->critical_task_count > 0 &&
        comparator->critical_task_count > 0 &&
        full->critical_completed_count * comparator->critical_task_count <
        comparator->critical_completed_count * full->critical_task_count;
    safety_loss = full->safety_violations > comparator->safety_violations;
    status = safety_loss || critical_loss ? "trails" :
        full->safety_violations < comparator->safety_violations ||
        (full->critical_task_count > 0 &&
         full->critical_completed_count > comparator->critical_completed_count) ?
        "leads" : "ties";
    return fprintf(output, "SAGE_FULL %s %s on %s: safety=%" PRIu64 "/%" PRIu64
        " critical=%" PRIu64 "/%" PRIu64 " vs %" PRIu64 "/%" PRIu64 "\n",
        status, comparator_name, scenario, full->safety_violations,
        comparator->safety_violations, full->critical_completed_count,
        full->critical_task_count, comparator->critical_completed_count,
        comparator->critical_task_count) >= 0;
}
