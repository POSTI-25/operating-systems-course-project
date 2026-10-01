#ifndef SAGE_REVALIDATION_H
#define SAGE_REVALIDATION_H

#include "gate.h"
#include "prediction.h"

#define REVALIDATION_SOC_TOLERANCE_PERMILLE 5
#define REVALIDATION_TEMP_TOLERANCE_MC 500
#define REVALIDATION_DRAW_TOLERANCE_MA 50
#define REVALIDATION_CLEAR_TICKS 3U
#define REVALIDATION_DWELL_TICKS 3U

typedef enum {
    INVALIDATION_OBSERVED_FAULT = 0,
    INVALIDATION_OBSERVED_DEVIATION = 1,
    INVALIDATION_PREDICTED_RISK = 2
} InvalidationCause;

typedef enum {
    REVALIDATION_TRIGGER_NONE = 0,
    REVALIDATION_TRIGGER_SOC = 1,
    REVALIDATION_TRIGGER_THERMAL = 2,
    REVALIDATION_TRIGGER_CURRENT_DRAW = 3,
    REVALIDATION_TRIGGER_PROTECTED_RESIDUAL = 4,
    REVALIDATION_TRIGGER_FAULT_FLAGS = 5
} RevalidationTrigger;

typedef struct {
    uint32_t task_id;
    InvalidationCause cause_type;
    RevalidationTrigger trigger_quantity;
    uint64_t trigger_time_ns;
    Mode original_mode;
    uint64_t forecast_horizon_end;
    uint32_t clear_count;
    uint32_t K;
    bool upgraded_from_predicted;
} InvalidationRecord;

typedef struct {
    bool invalid;
    InvalidationRecord record;
    InvalidationCause last_restored_cause;
    uint64_t dwell_until_ns;
    uint64_t last_clear_tick_ns;
    uint64_t last_clear_observation_epoch;
    AssumptionSnapshot observed_baseline;
    bool has_observed_baseline;
} RevalidationState;

typedef struct {
    const Task *task;
    Mode mode;
    const AssumptionSnapshot *assumption_snapshot;
    const TelemetryFrame *actual;
    const TelemetryFrame *forecast;
    const Ledger *ledger;
    int64_t thermal_limit_mC[N_SUBSYSTEMS];
    uint16_t safety_margin_permille;
    uint64_t now_ns;
    uint64_t forecast_horizon_end_ns;
    uint64_t step_ns;
    uint64_t observation_epoch;
    uint32_t fault_set_mask;
    bool is_out_of_band;
} RevalidationInput;

typedef enum {
    REVALIDATION_NONE = 0,
    REVALIDATION_INVALIDATE = 1,
    REVALIDATION_UPGRADE = 2,
    REVALIDATION_RESTORE_READY = 3,
    REVALIDATION_ERROR = 4
} RevalidationEvent;

RevalidationEvent revalidation_step(RevalidationState *state,
                                    const RevalidationInput *input);
RevalidationEvent revalidation_step_masked(RevalidationState *state,
    const RevalidationInput *input, uint8_t gate_mask, uint8_t ledger_mask,
    bool fault_enabled);
bool revalidation_complete_restore(RevalidationState *state, uint64_t now_ns,
                                   uint64_t step_ns);
bool revalidation_force_capacity_invalidation(RevalidationState *state,
    const Task *task, Mode mode, uint64_t now_ns);

#endif
