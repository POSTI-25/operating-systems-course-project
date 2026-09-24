#ifndef SAGE_GOVERNOR_H
#define SAGE_GOVERNOR_H

#include "gate.h"
#include "prediction.h"
#include "revalidation.h"
#include "utility.h"
#include "sim/scenario.h"
#include "platform/platform_ipc.h"
#include <stdio.h>

#define GOVERNOR_MAX_METRICS 10000

typedef struct {
    uint32_t task_id;
    Mode mode;
    AssumptionSnapshot assumption_snapshot;
    uint64_t commit_time_ns;
} Commitment;

typedef struct {
    double alpha;
    double beta;
    uint64_t lookahead_ns;
    uint64_t self_cost_cap_ns;
    int64_t thermal_limit_mC[N_SUBSYSTEMS];
    int64_t protected_reserve[LEDGER_DIMENSION_COUNT];
    uint16_t safety_margin_permille;
    bool prediction_enabled;
    uint64_t test_injected_cpu_ns_per_task;
    uint64_t test_injected_prediction_cpu_ns;
} GovernorConfig;

typedef struct {
    Ledger ledger;
    Commitment commitments[SAGE_MAX_TASKS];
    bool pending[SAGE_MAX_TASKS];
    bool commitment_active[SAGE_MAX_TASKS];
    RevalidationState revalidation[SAGE_MAX_TASKS];
    bool emergency_safe_mode;
    uint32_t emergency_task_id;
    uint64_t energy_refresh_epoch;
    uint64_t last_energy_soc_timestamp_ns;
    bool has_energy_soc_timestamp;
    size_t commitment_count;
} GovernorPlan;

typedef struct {
    const Scenario *scenario;
    GovernorConfig config;
    GovernorPlan plan;
    PredictionEngine prediction;
    PredictionTracker accuracy_tracker;
    PredictionAccuracyEvent accuracy_events[PREDICTION_MAX_PENDING];
    size_t accuracy_count;
    TelemetryFrame latest;
    bool has_frame;
    uint64_t observation_epoch;
    uint64_t last_cycle_cpu_ns;
    uint64_t pending_prediction_cpu_ns;
    uint64_t pending_prediction_wall_ns;
    uint64_t last_prediction_wall_ns;
} Governor;

typedef enum {
    GOVERNOR_CYCLE_OK = 0,
    GOVERNOR_CYCLE_OVERRUN = 1,
    GOVERNOR_CYCLE_ERROR = 2
} GovernorCycleResult;

typedef struct {
    uint64_t sim_time_ns;
    int64_t tick_jitter_ns;
    uint64_t cycle_time_ns;
    uint64_t cpu_time_ns;
    double self_cost_pct_of_period;
} GovernorTickMetric;

typedef struct {
    uint64_t tick_cycles;
    uint64_t fault_cycles;
    uint64_t overruns;
    uint64_t last_sim_time_ns;
    GovernorTickMetric metrics[GOVERNOR_MAX_METRICS];
    size_t metric_count;
    uint64_t cycle_p50_ns;
    uint64_t cycle_p99_ns;
    uint64_t cycle_max_ns;
    int64_t max_tick_jitter_ns;
    double mean_self_cost_pct_of_period;
} GovernorRunStats;

typedef struct {
    uint64_t measured_max_cpu_ns;
    uint64_t cap_cpu_ns;
    uint16_t reserved_cpu_pct;
    uint64_t modeled_energy_mJ;
    double assumed_active_power_W;
} GovernorSelfCost;

bool governor_init(Governor *governor, const Scenario *scenario,
                   const GovernorConfig *config);
bool governor_observe(Governor *governor, const TelemetryFrame *frame);
bool governor_observe_at(Governor *governor, const TelemetryFrame *frame,
                         uint64_t now_ns);
GovernorCycleResult governor_cycle(Governor *governor, uint64_t now_ns,
                                    bool fault_escalation);
GovernorCycleResult governor_cycle_with_fault_mask(Governor *governor,
                                                    uint64_t now_ns,
                                                    uint32_t fault_set_mask);
size_t governor_commitment_count(const Governor *governor);
const Commitment *governor_commitment_at(const Governor *governor, size_t index);
bool governor_commitment_active(const Governor *governor, size_t task_index);
const RevalidationState *governor_revalidation_state_at(const Governor *governor,
                                                        size_t task_index);
bool governor_emergency_safe_mode(const Governor *governor);
size_t governor_accuracy_count(const Governor *governor);
const PredictionAccuracyEvent *governor_accuracy_at(const Governor *governor,
                                                    size_t index);
bool governor_run_pipe(Governor *governor, SimulatorPipe *stream,
                       GovernorRunStats *stats);
bool governor_run_pipe_logged(Governor *governor, SimulatorPipe *stream,
                              GovernorRunStats *stats, FILE *output);
bool governor_calibrate_self_cost(Governor *governor, size_t repeats,
                                  uint64_t real_tick_interval_ns,
                                  double assumed_active_power_W,
                                  GovernorSelfCost *out);

#endif
