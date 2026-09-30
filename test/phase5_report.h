#ifndef SAGE_PHASE5_REPORT_H
#define SAGE_PHASE5_REPORT_H

#include "core/governor.h"

typedef struct {
    uint64_t task_count, critical_task_count, completed_count;
    uint64_t critical_completed_count, deadline_misses, rejected_by_design;
    uint64_t mission_work_completed, safety_violations, safety_task_instants;
    uint64_t protected_resource_violations, energy_survival_time_ns;
    bool energy_survival_censored;
    uint64_t recovery_latency_ns;
    bool recovery_latency_censored;
    uint64_t replan_count, cpu_time_ns, self_cost_cap_ns;
    double modeled_self_energy_mJ;
    uint64_t prediction_abs_error, prediction_samples;
    bool has_audit;
    uint64_t last_audit_ns, accuracy_epoch;
    bool has_accuracy_epoch, previous_emergency;
    bool fault_pending[N_SUBSYSTEMS];
    uint64_t fault_time_ns[N_SUBSYSTEMS];
} Phase5Metrics;

void phase5_metrics_init(Phase5Metrics *metrics, const Scenario *scenario);
bool phase5_audit_instant(Phase5Metrics *metrics, const Scenario *scenario,
    const Governor *governor, const TelemetryFrame *actual, uint64_t now_ns);
bool phase5_note_fault(Phase5Metrics *metrics, uint64_t now_ns, uint32_t mask);
bool phase5_note_cycle(Phase5Metrics *metrics, const Scenario *scenario,
    const Governor *governor, uint64_t now_ns);
bool phase5_metrics_finish(Phase5Metrics *metrics, const Scenario *scenario,
    const Governor *governor);
bool phase5_metrics_write_csv(FILE *output, const char *scenario,
    const char *variant, const Phase5Metrics *metrics);
bool phase5_metrics_write_header(FILE *output);

#endif
