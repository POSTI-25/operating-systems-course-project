/* Observation-only reference replay. Scheduling stays in the existing engine. */
#include "replay.h"
#include <inttypes.h>
#include <string.h>

static const char *phase_name(ExecutionPhase phase)
{
    static const char *const names[] = {"PENDING", "RUNNING", "PAUSED", "COMPLETE", "REJECTED"};
    return phase >= EXECUTION_PENDING && phase <= EXECUTION_REJECTED ? names[phase] : "UNKNOWN";
}

static bool print_frame(const TelemetryFrame *frame, uint64_t now)
{
    int id;
    if (printf("FRAME t=%" PRIu64, now) < 0) return false;
    for (id = 0; id < TELEMETRY_CHANNEL_COUNT; ++id) {
        unsigned count = id == TELEMETRY_TEMP_MC ? N_SUBSYSTEMS : 1U;
        unsigned index;
        for (index = 0; index < count; ++index) {
            TelemetryChannel channel;
            if (!telemetry_frame_get(frame, (TelemetryChannelId)id, index, &channel) ||
                printf(" %s", telemetry_channel_name((TelemetryChannelId)id)) < 0) return false;
            if (id == TELEMETRY_TEMP_MC && printf("[%u]", index) < 0) return false;
            if (printf("=%" PRId64 "@%" PRIu64 ":%s", channel.value,
                channel.timestamp_ns, telemetry_quality_name(channel.quality)) < 0) return false;
        }
    }
    return putchar('\n') != EOF;
}

static bool print_states(const Governor *governor)
{
    size_t i;
    for (i = 0; i < governor->scenario->tasks.count; ++i) {
        const ExecutionState *state = governor_execution_at(governor, i);
        if (state == NULL || printf("STATE task=%" PRIu32 " phase=%s remaining_ns=%" PRIu64
            " completed_ns=%" PRIu64 " delivered_value=%" PRIu32 "\n",
            governor->scenario->tasks.items[i].task_id, phase_name(state->phase),
            state->remaining_ns, state->completed_ns, state->delivered_value) < 0) return false;
    }
    return true;
}

static bool observe_cycle(const Governor *governor, const TelemetryFrame *frame,
    uint64_t now, GovernorCycleResult result, void *context)
{
    size_t *sequence = context;
    size_t i;
    const LogRecord *first = governor_log_record_at(governor, 0);
    const char *trigger = result == GOVERNOR_CYCLE_OK && first != NULL &&
        first->trigger == DECISION_TRIGGER_FAULT ? "FAULT" : "TICK";
    if (printf("CYCLE sequence=%zu sim_ns=%" PRIu64 " trigger=%s result=%s emergency=%d\n",
        (*sequence)++, now, trigger, result == GOVERNOR_CYCLE_OK ? "OK" : "OVERRUN",
        governor_emergency_safe_mode(governor) ? 1 : 0) < 0 || !print_frame(frame, now)) return false;
    if (result == GOVERNOR_CYCLE_OK) {
        for (i = 0; i < governor->scenario->tasks.count; ++i) {
            const LogRecord *record = governor_log_record_at(governor, i);
            if (record == NULL || !logger_write(stdout, record)) return false;
        }
    }
    for (i = 0; i < governor_commitment_count(governor); ++i) {
        const Commitment *entry = governor_commitment_at(governor, i);
        if (entry == NULL || printf("ASSUMPTION task=%" PRIu32 " commit_ns=%" PRIu64
            " soc=%" PRId64 " horizon_ns=%" PRIu64 " draw=%" PRId64 "\n",
            entry->task_id, entry->commit_time_ns, entry->assumption_snapshot.soc_permille,
            entry->assumption_snapshot.forecast_horizon_end_ns,
            entry->assumption_snapshot.current_draw_mA) < 0) return false;
    }
    return print_states(governor) && puts("CYCLE_END") >= 0;
}

int main(int argc, char **argv)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    FILE *input;
    char error[128];
    size_t sequence = 0;
    int subsystem;
    if (argc != 3) { fputs("usage: frontend_runner scenario.scn full|fixed|gomx|ranjbar\n", stderr); return 2; }
    if (strcmp(argv[2], "full") == 0) config.variant = GOVERNOR_VARIANT_SAGE_FULL;
    else if (strcmp(argv[2], "fixed") == 0) config.variant = GOVERNOR_VARIANT_FIXED_EDF;
    else if (strcmp(argv[2], "gomx") == 0) config.variant = GOVERNOR_VARIANT_GOMX_STYLE;
    else if (strcmp(argv[2], "ranjbar") == 0) config.variant = GOVERNOR_VARIANT_RANJBAR_STYLE;
    else return 2;
    input = fopen(argv[1], "r");
    if (input == NULL) { perror(argv[1]); return 1; }
    if (!scenario_load_file(input, &scenario, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); fclose(input); return 1;
    }
    fclose(input);
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem) config.thermal_limit_mC[subsystem] = 100000;
    if (!governor_init(&governor, &scenario, &config) ||
        !replay_run(&scenario, &governor, NULL, observe_cycle, &sequence)) {
        fputs("reference replay failed\n", stderr); return 1;
    }
    if (printf("FINAL sim_ns=%" PRIu64 "\n", scenario.duration_ns) < 0 ||
        !print_states(&governor) || puts("FINAL_END") < 0) return 1;
    return ferror(stdout) ? 1 : 0;
}
