#include "test.h"
#include "replay.h"

typedef struct {
    size_t count;
    uint64_t times[8];
    DecisionTrigger triggers[8];
    ExecutionPhase phases[8];
} Seen;

static bool capture(const Governor *governor, const TelemetryFrame *frame,
                    uint64_t now_ns, GovernorCycleResult result,
                    void *context)
{
    Seen *seen = context;
    const LogRecord *record = governor_log_record_at(governor, 0);
    (void)frame;
    (void)result;
    if (seen->count >= 8 || record == NULL) return false;
    seen->times[seen->count] = now_ns;
    seen->triggers[seen->count] = record->trigger;
    seen->phases[seen->count++] = record->state_after;
    return true;
}

static void replay_preserves_pipe_cadence_and_completion_boundary(void)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    Seen seen = {0};
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    char error[128];
    int subsystem;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.duration_ns = 4;
    scenario.tasks.items[0].nominal_exec_ns = 3;
    scenario.tasks.items[0].deadline_ns = 20;
    scenario.fault_count = 2;
    scenario.faults[0] = (ScenarioFault){.time_ns = 3, .set_mask = 4};
    scenario.faults[1] = (ScenarioFault){.time_ns = 4, .set_mask = 8};
    config.alpha = 0.3;
    config.beta = 0.1;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(replay_run(&scenario, &governor, NULL, capture, &seen));
    ASSERT_EQ_INT(5, seen.count);
    ASSERT_EQ_INT(0, seen.times[0]);
    ASSERT_EQ_INT(2, seen.times[1]);
    ASSERT_EQ_INT(3, seen.times[2]);
    ASSERT_EQ_INT(4, seen.times[3]);
    ASSERT_EQ_INT(4, seen.times[4]);
    ASSERT_EQ_INT(EXECUTION_COMPLETE, seen.phases[2]);
    ASSERT_EQ_INT(DECISION_TRIGGER_TICK, seen.triggers[3]);
    ASSERT_EQ_INT(DECISION_TRIGGER_FAULT, seen.triggers[4]);
    config.variant = GOVERNOR_VARIANT_GOMX_STYLE;
    seen = (Seen){0};
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(replay_run(&scenario, &governor, NULL, capture, &seen));
    ASSERT_EQ_INT(3, seen.count);
    ASSERT_EQ_INT(4, seen.times[2]);
}

static void final_partial_tick_settles_execution_without_new_decision(void)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    Seen seen = {0};
    FILE *input = fopen("scenarios/normal_orbit.scn", "r");
    char error[128];
    int subsystem;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    scenario.duration_ns = 3;
    scenario.update_count = 0;
    scenario.tasks.items[0].nominal_exec_ns = 3;
    scenario.tasks.items[0].deadline_ns = 3;
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(replay_run(&scenario, &governor, NULL, capture, &seen));
    ASSERT_EQ_INT(2, seen.count);
    ASSERT_EQ_INT(EXECUTION_COMPLETE, governor.plan.execution[0].phase);
    ASSERT_EQ_INT(3, governor.plan.execution[0].completed_ns);
    ASSERT_TRUE(!governor_commitment_active(&governor, 0));
}

typedef struct {
    bool fault_in_eclipse, observed_fault, shedding, emergency;
    bool predicted, upgraded, restored;
    uint64_t risk_horizon, paused_remaining, restore_time;
    int64_t commit_soc, risk_soc;
} Evidence;

static bool evidence_cycle(const Governor *governor, const TelemetryFrame *frame,
                           uint64_t now_ns, GovernorCycleResult result,
                           void *context)
{
    Evidence *evidence = context;
    size_t i;
    (void)result;
    evidence->fault_in_eclipse |= frame->eclipse.value != 0 &&
        ((uint32_t)frame->fault_flags.value & 8U) != 0U;
    for (i = 0; i < governor->last_record_count; ++i) {
        const LogRecord *record = governor_log_record_at(governor, i);
        evidence->emergency |= record->reason == DECISION_REASON_EMERGENCY_SAFE_MODE;
        if (record->has_invalidation) {
            evidence->observed_fault |= record->invalidation.cause_type == INVALIDATION_OBSERVED_FAULT;
            evidence->shedding |= record->invalidation.trigger_quantity ==
                REVALIDATION_TRIGGER_PROTECTED_RESIDUAL && record->task_id == 2;
            evidence->upgraded |= record->invalidation.upgraded_from_predicted;
            if (record->invalidation.cause_type == INVALIDATION_PREDICTED_RISK &&
                record->invalidation.trigger_time_ns == now_ns) {
                TelemetryFrame forecast;
                uint64_t horizon;
                evidence->predicted = true;
                evidence->risk_horizon = record->invalidation.forecast_horizon_end;
                evidence->paused_remaining = record->remaining_ns;
                if (!prediction_engine_forecast(&governor->prediction, 12,
                    now_ns, &forecast, &horizon)) return false;
                evidence->risk_soc = forecast.battery_soc_mV_pct.value;
            }
        }
        if (record->restoration) {
            if (now_ns < evidence->risk_horizon ||
                record->remaining_ns != evidence->paused_remaining) return false;
            evidence->restored = true;
            evidence->restore_time = now_ns;
        }
        if (now_ns == 8 && record->state_after == EXECUTION_RUNNING)
            evidence->commit_soc = governor->plan.commitments[0].assumption_snapshot.soc_permille;
    }
    return true;
}

static bool load_evidence(const char *path, Scenario *scenario,
                          Governor *governor, Evidence *evidence)
{
    FILE *input = fopen(path, "r");
    GovernorConfig config = {0};
    char error[128];
    int subsystem;
    bool loaded;
    if (input == NULL) return false;
    loaded = scenario_load_file(input, scenario, error, sizeof(error));
    fclose(input);
    if (!loaded) return false;
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario->step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    return governor_init(governor, scenario, &config) &&
        replay_run(scenario, governor, NULL, evidence_cycle, evidence);
}

static void compound_and_false_alarm_have_required_end_to_end_evidence(void)
{
    static Scenario scenario;
    static Governor governor;
    Evidence evidence = {0};
    ASSERT_TRUE(load_evidence("scenarios/compound_stress.scn", &scenario, &governor, &evidence));
    ASSERT_EQ_INT(3, scenario.tasks.count);
    ASSERT_EQ_INT(2, scenario.fault_count);
    ASSERT_EQ_INT(1, scenario.update_count);
    ASSERT_EQ_INT(260, scenario.initial.battery_soc_mV_pct.value);
    ASSERT_EQ_INT(99900, scenario.initial.temp_mC[SUBSYSTEM_ADCS].value);
    ASSERT_EQ_INT(SUBSYSTEM_PAYLOAD, scenario.tasks.items[1].primary_subsystem);
    ASSERT_EQ_INT(SUBSYSTEM_PAYLOAD, scenario.tasks.items[2].primary_subsystem);
    ASSERT_TRUE(evidence.fault_in_eclipse && evidence.observed_fault);
    ASSERT_TRUE(evidence.shedding && evidence.emergency);
    evidence = (Evidence){0};
    ASSERT_TRUE(load_evidence("scenarios/false_alarm_restoration.scn", &scenario, &governor, &evidence));
    ASSERT_TRUE(evidence.predicted && !evidence.upgraded && evidence.restored);
    ASSERT_EQ_INT(24, evidence.risk_horizon);
    ASSERT_EQ_INT(24, evidence.restore_time);
    ASSERT_EQ_INT(10, evidence.paused_remaining);
    ASSERT_EQ_INT(34, governor.plan.execution[0].completed_ns);
    printf("false_alarm_trace commit_soc=%lld risk_soc=%lld horizon=%llu restore=%llu\n",
        (long long)evidence.commit_soc, (long long)evidence.risk_soc,
        (unsigned long long)evidence.risk_horizon, (unsigned long long)evidence.restore_time);
}

typedef struct {
    Governor *governor;
    Evidence evidence;
    bool forced_overrun;
    bool late_admission;
    bool full_restoration;
} DelayedAdmission;

static bool delayed_admission_cycle(const Governor *governor,
    const TelemetryFrame *frame, uint64_t now_ns,
    GovernorCycleResult result, void *context)
{
    DelayedAdmission *delayed = context;
    const LogRecord *record = governor_log_record_at(governor, 0);
    if (now_ns == 6) {
        delayed->governor->config.self_cost_cap_ns = 1;
        delayed->governor->config.test_injected_cpu_ns_per_task = 1000000;
    }
    if (now_ns == 8) {
        delayed->forced_overrun = result == GOVERNOR_CYCLE_OVERRUN;
        delayed->governor->config.self_cost_cap_ns = UINT64_MAX;
        delayed->governor->config.test_injected_cpu_ns_per_task = 0;
        return delayed->forced_overrun;
    }
    if (result != GOVERNOR_CYCLE_OK) return false;
    if (now_ns == 10 && record != NULL)
        delayed->late_admission = record->has_selected &&
            record->selected_mode <= MODE_DEGRADED;
    if (record != NULL && record->restoration)
        delayed->full_restoration = record->has_selected &&
            record->selected_mode == MODE_FULL;
    return evidence_cycle(governor, frame, now_ns, result,
                          &delayed->evidence);
}

static void false_alarm_restores_after_one_lost_admission_tick(void)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    GovernorSelfCost calibration;
    DelayedAdmission delayed = {.governor = &governor};
    FILE *input = fopen("scenarios/false_alarm_restoration.scn", "r");
    char error[128];
    int subsystem;
    ASSERT_TRUE(input != NULL);
    ASSERT_TRUE(scenario_load_file(input, &scenario, error, sizeof(error)));
    fclose(input);
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    ASSERT_TRUE(governor_init(&governor, &scenario, &config));
    ASSERT_TRUE(governor_observe_at(&governor, &scenario.initial, 0));
    ASSERT_TRUE(governor_calibrate_self_cost(&governor, 64, 10000000, 2.0,
                                             &calibration));
    governor.config.self_cost_cap_ns = UINT64_MAX;
    governor.pending_prediction_cpu_ns = 0;
    governor.pending_prediction_wall_ns = 0;
    ASSERT_TRUE(replay_run(&scenario, &governor, NULL,
                           delayed_admission_cycle, &delayed));
    ASSERT_TRUE(delayed.forced_overrun);
    ASSERT_TRUE(delayed.late_admission && delayed.full_restoration);
    ASSERT_TRUE(delayed.evidence.predicted && delayed.evidence.restored &&
                !delayed.evidence.upgraded);
    ASSERT_EQ_INT(EXECUTION_COMPLETE, governor.plan.execution[0].phase);
    ASSERT_EQ_INT(900, governor.plan.execution[0].delivered_value);
}

int main(void)
{
    RUN_TEST(replay_preserves_pipe_cadence_and_completion_boundary);
    RUN_TEST(final_partial_tick_settles_execution_without_new_decision);
    RUN_TEST(compound_and_false_alarm_have_required_end_to_end_evidence);
    RUN_TEST(false_alarm_restores_after_one_lost_admission_tick);
    return test_failures != 0;
}
