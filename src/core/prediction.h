#ifndef SAGE_PREDICTION_H
#define SAGE_PREDICTION_H

#include "telemetry.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PREDICTION_MAX_PENDING 256

#define PREDICTION_DEFAULT_ALPHA 0.3
#define PREDICTION_DEFAULT_BETA 0.1

typedef struct {
    double level;
    double trend;
    bool seeded;
} HoltFilter;

typedef struct {
    HoltFilter soc;
    HoltFilter temp[N_SUBSYSTEMS];
    HoltFilter draw;
    TelemetryFrame latest;
    double alpha;
    double beta;
    uint64_t step_ns;
    uint64_t lookahead_ns;
    bool has_sample;
    bool last_eclipse;
} PredictionEngine;

typedef struct {
    int64_t soc_permille;
    int64_t temp_mC[N_SUBSYSTEMS];
    int64_t current_draw_mA;
    uint64_t forecast_horizon_end_ns;
} AssumptionSnapshot;

typedef struct {
    uint32_t task_id;
    uint64_t issued_ns;
    AssumptionSnapshot predicted;
} PredictionPending;

typedef struct {
    PredictionPending pending[PREDICTION_MAX_PENDING];
    size_t count;
} PredictionTracker;

typedef struct {
    uint32_t task_id;
    uint64_t issued_ns;
    AssumptionSnapshot predicted;
    AssumptionSnapshot actual;
    int64_t soc_error_permille;
    int64_t temp_error_mC[N_SUBSYSTEMS];
    int64_t draw_error_mA;
} PredictionAccuracyEvent;

bool prediction_filter_update(HoltFilter *filter, double sample,
                              double alpha, double beta,
                              bool eclipse_transition);
double prediction_filter_forecast(const HoltFilter *filter, double steps);
bool prediction_engine_init(PredictionEngine *engine, double alpha, double beta,
                            uint64_t step_ns, uint64_t lookahead_ns);
bool prediction_engine_update(PredictionEngine *engine, const TelemetryFrame *frame);
bool prediction_engine_forecast(const PredictionEngine *engine,
                                uint64_t nominal_exec_ns, uint64_t now_ns,
                                TelemetryFrame *out, uint64_t *horizon_end_ns);
bool prediction_tracker_add(PredictionTracker *tracker, uint32_t task_id,
                            uint64_t issued_ns, uint64_t horizon_end_ns,
                            const TelemetryFrame *predicted);
bool prediction_tracker_observe(PredictionTracker *tracker, uint64_t now_ns,
                                const TelemetryFrame *actual,
                                PredictionAccuracyEvent *events,
                                size_t capacity, size_t *event_count);

#endif
