#include "replay.h"

int main(int argc, char **argv)
{
    static Scenario scenario;
    static Governor governor;
    GovernorConfig config = {0};
    FILE *input;
    char error[128];
    int subsystem;
    if (argc != 2) return 2;
    input = fopen(argv[1], "r");
    if (input == NULL) return 1;
    if (!scenario_load_file(input, &scenario, error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", argv[1], error);
        fclose(input);
        return 1;
    }
    fclose(input);
    config.alpha = PREDICTION_DEFAULT_ALPHA;
    config.beta = PREDICTION_DEFAULT_BETA;
    config.lookahead_ns = scenario.step_ns;
    config.prediction_enabled = true;
    config.self_cost_cap_ns = UINT64_MAX;
    for (subsystem = 0; subsystem < N_SUBSYSTEMS; ++subsystem)
        config.thermal_limit_mC[subsystem] = 100000;
    if (!governor_init(&governor, &scenario, &config) ||
        !replay_run(&scenario, &governor, stdout, NULL, NULL)) return 1;
    return 0;
}
