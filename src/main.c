#include "core/governor.h"
#include "platform/platform_ipc.h"
#include "sim/scenario.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEMO_REAL_TICK_NS UINT64_C(1000000000)
#define DEMO_THERMAL_LIMIT_MC INT64_C(100000)
#define DEMO_ASSUMED_ACTIVE_POWER_W 2.0
#define SELF_COST_CALIBRATION_RUNS 2000U

int main(int argc, char **argv)
{
    static Scenario scenario;
    static Governor governor;
    static GovernorRunStats stats;
    const char *path = argc > 1 ? argv[1] : "scenarios/normal_orbit.scn";
    uint64_t real_tick_ns = DEMO_REAL_TICK_NS;
    FILE *input;
    char error[128] = {0};
    GovernorConfig config = {0};
    GovernorSelfCost self_cost;
    SimulatorPipe stream;
    size_t i;
    bool ran;
    bool child_ok;
    if (argc != 1 && argc != 2 && argc != 4) {
        fprintf(stderr, "usage: sage_rt [scenario.scn [--tick-ms positive_integer]]\n");
        return 2;
    }
    if (argc == 4) {
        char *end;
        unsigned long long millis;
        if (strcmp(argv[2], "--tick-ms") != 0 || argv[3][0] == '-') return 2;
        errno = 0;
        millis = strtoull(argv[3], &end, 10);
        if (errno != 0 || end == argv[3] || *end != '\0' || millis == 0 ||
            millis > UINT64_MAX / UINT64_C(1000000)) return 2;
        real_tick_ns = (uint64_t)millis * UINT64_C(1000000);
    }
    input = fopen(path, "r");
    if (input == NULL) {
        perror(path);
        return 1;
    }
    if (!scenario_load_file(input, &scenario, error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", path, error);
        (void)fclose(input);
        return 1;
    }
    (void)fclose(input);
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    for (i = 0; i < N_SUBSYSTEMS; ++i)
        config.thermal_limit_mC[i] = DEMO_THERMAL_LIMIT_MC;
    if (!governor_init(&governor, &scenario, &config) ||
        !governor_observe_at(&governor, &scenario.initial, 0) ||
        !governor_calibrate_self_cost(&governor, SELF_COST_CALIBRATION_RUNS,
            real_tick_ns, DEMO_ASSUMED_ACTIVE_POWER_W, &self_cost)) {
        fprintf(stderr, "governor setup or self-cost calibration failed\n");
        return 1;
    }
    printf("SELF_COST measured_max_cpu_ns=%" PRIu64
           " cap_cpu_ns=%" PRIu64 " reserved_cpu_pct=%u"
           " modeled_energy_mJ=%" PRIu64 " assumed_power_W=%.1f\n",
           self_cost.measured_max_cpu_ns, self_cost.cap_cpu_ns,
           (unsigned)self_cost.reserved_cpu_pct,
           self_cost.modeled_energy_mJ, self_cost.assumed_active_power_W);
    if (!platform_ipc_spawn(&stream, &scenario, real_tick_ns)) {
        fprintf(stderr, "fork/pipe simulator startup failed\n");
        return 1;
    }
    ran = governor_run_pipe_logged(&governor, &stream, &stats, stdout);
    child_ok = platform_ipc_close(&stream);
    if (!ran || !child_ok) {
        fprintf(stderr, "governor run failed\n");
        return 1;
    }
    for (i = 0; i < governor_commitment_count(&governor); ++i) {
        const Commitment *commitment = governor_commitment_at(&governor, i);
        if (commitment == NULL) return 1;
        printf("COMMIT task=%" PRIu32 " mode=%d at_ns=%" PRIu64
               " soc_pred=%" PRId64 " horizon_ns=%" PRIu64 "\n",
               commitment->task_id, (int)commitment->mode,
               commitment->commit_time_ns,
               commitment->assumption_snapshot.soc_permille,
               commitment->assumption_snapshot.forecast_horizon_end_ns);
    }
    return 0;
}
