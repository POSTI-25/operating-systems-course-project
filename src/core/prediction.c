#include "prediction.h"

#include <stddef.h>
#include <limits.h>
#include <string.h>

bool prediction_filter_update(HoltFilter *filter, double sample,
                              double alpha, double beta,
                              bool eclipse_transition)
{
    double previous_level;
    double previous_trend;
    if (filter == NULL || alpha < 0.0 || alpha > 1.0 ||
        beta < 0.0 || beta > 1.0) return false;
    if (!filter->seeded) {
        filter->level = sample;
        filter->trend = 0.0;
        filter->seeded = true;
        return true;
    }
    previous_level = filter->level;
    previous_trend = filter->trend;
    filter->level = alpha * sample + (1.0 - alpha) *
        (previous_level + previous_trend);
    filter->trend = beta * (filter->level - previous_level) +
        (1.0 - beta) * previous_trend;
    if (eclipse_transition) filter->trend = 0.0;
    return true;
}

double prediction_filter_forecast(const HoltFilter *filter, double steps)
{
    if (filter == NULL || !filter->seeded || steps < 0.0) return 0.0;
    return filter->level + steps * filter->trend;
}

bool prediction_engine_init(PredictionEngine *engine, double alpha, double beta,
                            uint64_t step_ns, uint64_t lookahead_ns)
{
    if (engine == NULL || step_ns == 0 || alpha < 0.0 || alpha > 1.0 ||
        beta < 0.0 || beta > 1.0 || alpha != alpha || beta != beta)
        return false;
    memset(engine, 0, sizeof(*engine));
    engine->alpha = alpha;
    engine->beta = beta;
    engine->step_ns = step_ns;
    engine->lookahead_ns = lookahead_ns;
    return true;
}

bool prediction_engine_update(PredictionEngine *engine, const TelemetryFrame *frame)
{
    bool transition;
    int i;
    if (engine == NULL || frame == NULL) return false;
    transition = engine->has_sample && frame->eclipse.quality == TELEMETRY_QUALITY_OK &&
        (frame->eclipse.value != 0) != engine->last_eclipse;
    if (frame->battery_soc_mV_pct.quality == TELEMETRY_QUALITY_OK &&
        !prediction_filter_update(&engine->soc,
            (double)frame->battery_soc_mV_pct.value,
            engine->alpha, engine->beta, transition)) return false;
    for (i = 0; i < N_SUBSYSTEMS; ++i) {
        if (frame->temp_mC[i].quality == TELEMETRY_QUALITY_OK &&
            !prediction_filter_update(&engine->temp[i],
                (double)frame->temp_mC[i].value,
                engine->alpha, engine->beta, transition)) return false;
    }
    if (frame->current_draw_mA.quality == TELEMETRY_QUALITY_OK &&
        !prediction_filter_update(&engine->draw,
            (double)frame->current_draw_mA.value,
            engine->alpha, engine->beta, transition)) return false;
    engine->latest = *frame;
    engine->has_sample = true;
    if (frame->eclipse.quality == TELEMETRY_QUALITY_OK)
        engine->last_eclipse = frame->eclipse.value != 0;
    return true;
}

static int64_t ceil_i64(double value)
{
    int64_t result;
    if (value >= (double)INT64_MAX) return INT64_MAX;
    if (value <= (double)INT64_MIN) return INT64_MIN;
    result = (int64_t)value;
    if ((double)result < value) ++result;
    return result;
}

static int64_t conservative_soc(double value)
{
    int64_t result;
    if (value <= 0.0) return 0;
    if (value >= 1000.0) return 1000;
    result = (int64_t)value;
    if ((double)result > value) --result;
    return result;
}

bool prediction_engine_forecast(const PredictionEngine *engine,
                                uint64_t nominal_exec_ns, uint64_t now_ns,
                                TelemetryFrame *out, uint64_t *horizon_end_ns)
{
    uint64_t horizon;
    double steps;
    int i;
    if (engine == NULL || out == NULL || horizon_end_ns == NULL ||
        !engine->has_sample || nominal_exec_ns > UINT64_MAX - engine->lookahead_ns)
        return false;
    horizon = nominal_exec_ns + engine->lookahead_ns;
    if (horizon > UINT64_MAX - now_ns) return false;
    *horizon_end_ns = now_ns + horizon;
    steps = (double)horizon / (double)engine->step_ns;
    *out = engine->latest;
    out->battery_soc_mV_pct.value = engine->latest.battery_soc_mV_pct.quality ==
        TELEMETRY_QUALITY_OK && engine->soc.seeded ?
        conservative_soc(prediction_filter_forecast(&engine->soc, steps)) : 0;
    out->battery_soc_mV_pct.timestamp_ns = *horizon_end_ns;
    for (i = 0; i < N_SUBSYSTEMS; ++i) {
        out->temp_mC[i].value = engine->latest.temp_mC[i].quality ==
            TELEMETRY_QUALITY_OK && engine->temp[i].seeded ?
            ceil_i64(prediction_filter_forecast(&engine->temp[i], steps)) : INT64_MAX;
        out->temp_mC[i].timestamp_ns = *horizon_end_ns;
    }
    out->current_draw_mA.value = engine->latest.current_draw_mA.quality ==
        TELEMETRY_QUALITY_OK && engine->draw.seeded ?
        ceil_i64(prediction_filter_forecast(&engine->draw, steps)) : INT64_MAX;
    out->current_draw_mA.timestamp_ns = *horizon_end_ns;
    return true;
}

static AssumptionSnapshot snapshot_from_frame(const TelemetryFrame *frame,
                                               uint64_t horizon_end_ns)
{
    AssumptionSnapshot snapshot = {0};
    int i;
    snapshot.soc_permille = frame->battery_soc_mV_pct.value;
    for (i = 0; i < N_SUBSYSTEMS; ++i)
        snapshot.temp_mC[i] = frame->temp_mC[i].value;
    snapshot.current_draw_mA = frame->current_draw_mA.value;
    snapshot.forecast_horizon_end_ns = horizon_end_ns;
    return snapshot;
}

bool prediction_tracker_add(PredictionTracker *tracker, uint32_t task_id,
                            uint64_t issued_ns, uint64_t horizon_end_ns,
                            const TelemetryFrame *predicted)
{
    PredictionPending *entry;
    if (tracker == NULL || predicted == NULL || horizon_end_ns < issued_ns ||
        tracker->count >= PREDICTION_MAX_PENDING) return false;
    entry = &tracker->pending[tracker->count++];
    entry->task_id = task_id;
    entry->issued_ns = issued_ns;
    entry->predicted = snapshot_from_frame(predicted, horizon_end_ns);
    return true;
}

static int64_t signed_error(int64_t actual, int64_t predicted)
{
    if (predicted > 0 && actual < INT64_MIN + predicted) return INT64_MIN;
    if (predicted < 0 && actual > INT64_MAX + predicted) return INT64_MAX;
    return actual - predicted;
}

bool prediction_tracker_observe(PredictionTracker *tracker, uint64_t now_ns,
                                const TelemetryFrame *actual,
                                PredictionAccuracyEvent *events,
                                size_t capacity, size_t *event_count)
{
    size_t i;
    size_t produced = 0;
    size_t kept = 0;
    if (tracker == NULL || actual == NULL || event_count == NULL ||
        (capacity > 0 && events == NULL)) return false;
    for (i = 0; i < tracker->count; ++i)
        if (tracker->pending[i].predicted.forecast_horizon_end_ns <= now_ns)
            ++produced;
    if (produced > capacity) return false;
    produced = 0;
    for (i = 0; i < tracker->count; ++i) {
        PredictionPending entry = tracker->pending[i];
        if (entry.predicted.forecast_horizon_end_ns <= now_ns) {
            PredictionAccuracyEvent *event = &events[produced++];
            int j;
            event->task_id = entry.task_id;
            event->issued_ns = entry.issued_ns;
            event->predicted = entry.predicted;
            event->actual = snapshot_from_frame(actual, now_ns);
            event->soc_error_permille = signed_error(event->actual.soc_permille,
                                                     event->predicted.soc_permille);
            for (j = 0; j < N_SUBSYSTEMS; ++j)
                event->temp_error_mC[j] = signed_error(event->actual.temp_mC[j],
                                                       event->predicted.temp_mC[j]);
            event->draw_error_mA = signed_error(event->actual.current_draw_mA,
                                                event->predicted.current_draw_mA);
        } else {
            tracker->pending[kept++] = entry;
        }
    }
    tracker->count = kept;
    *event_count = produced;
    return true;
}
