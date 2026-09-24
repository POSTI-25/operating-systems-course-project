#include "core/prediction.h"
#include "platform/platform_clock.h"
#include "sim/scenario.h"
#include "sim/simulator.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t absolute_error(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

int main(int argc, char **argv)
{
    static Scenario scenario;
    const char *path = argc > 1 ? argv[1] : "scenarios/normal_orbit.scn";
    FILE *input = fopen(path, "r");
    char error[128];
    PlatformClock clock;
    Simulator simulator;
    PredictionEngine engine;
    PredictionTracker predicted = {0};
    PredictionTracker persistence = {0};
    PredictionAccuracyEvent events[PREDICTION_MAX_PENDING];
    TelemetryFrame actual;
    TelemetryFrame forecast;
    uint64_t now_ns;
    uint64_t horizon_end;
    uint64_t predicted_abs = 0;
    uint64_t persistence_abs = 0;
    uint64_t samples = 0;
    size_t count;
    size_t i;
    SimulatorResult result;
    if (input == NULL || !scenario_load_file(input, &scenario, error, sizeof(error))) {
        fprintf(stderr, "A/B scenario load failed\n");
        return 1;
    }
    (void)fclose(input);
    if (!platform_clock_init(&clock, PLATFORM_CLOCK_VIRTUAL, 0) ||
        !simulator_init(&simulator, &scenario, &clock) ||
        !prediction_engine_init(&engine, PREDICTION_DEFAULT_ALPHA,
            PREDICTION_DEFAULT_BETA, scenario.step_ns, scenario.step_ns)) return 1;
    while ((result = simulator_next(&simulator, &actual, &now_ns)) == SIMULATOR_FRAME) {
        if (!prediction_tracker_observe(&predicted, now_ns, &actual, events,
                                         PREDICTION_MAX_PENDING, &count)) return 1;
        for (i = 0; i < count; ++i) {
            predicted_abs += absolute_error(events[i].soc_error_permille);
            ++samples;
        }
        if (!prediction_tracker_observe(&persistence, now_ns, &actual, events,
                                         PREDICTION_MAX_PENDING, &count)) return 1;
        for (i = 0; i < count; ++i)
            persistence_abs += absolute_error(events[i].soc_error_permille);
        if (!prediction_engine_update(&engine, &actual) ||
            !prediction_engine_forecast(&engine,
                scenario.tasks.items[0].nominal_exec_ns, now_ns,
                &forecast, &horizon_end)) return 1;
        if (horizon_end <= scenario.duration_ns &&
            (!prediction_tracker_add(&predicted, 1, now_ns, horizon_end, &forecast) ||
             !prediction_tracker_add(&persistence, 1, now_ns, horizon_end,
                                     &actual))) return 1;
    }
    if (result != SIMULATOR_DONE || samples == 0 ||
        predicted_abs == persistence_abs ||
        (strcmp(scenario.name, "prediction_trend") == 0 &&
         predicted_abs >= persistence_abs)) return 1;
    printf("scenario=%s samples=%" PRIu64
           " predicted_soc_abs_error=%" PRIu64
           " persistence_soc_abs_error=%" PRIu64 "\n",
           scenario.name, samples, predicted_abs, persistence_abs);
    return 0;
}
