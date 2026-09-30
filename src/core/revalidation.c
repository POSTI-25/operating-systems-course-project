#include "revalidation.h"

#include <limits.h>
#include <stddef.h>

static bool exceeds_upward(int64_t actual, int64_t baseline, int64_t tolerance)
{
    return baseline > INT64_MAX - tolerance ? false :
        actual > baseline + tolerance;
}

static bool exceeds_downward(int64_t actual, int64_t baseline, int64_t tolerance)
{
    return baseline < INT64_MIN + tolerance ? false :
        actual < baseline - tolerance;
}

static bool physical_soc_failure(const RevalidationInput *input)
{
    uint64_t capacity;
    uint16_t floor;
    uint32_t threshold;
    uint64_t delta;
    uint64_t available;
    const TaskModeProfile *profile = &input->task->modes[input->mode];
    if (!ledger_energy_config(input->ledger, &capacity, &floor)) return true;
    threshold = (uint32_t)floor + input->safety_margin_permille;
    if (threshold > 1000 ||
        input->actual->battery_soc_mV_pct.value < (int64_t)threshold ||
        input->actual->battery_soc_mV_pct.value > 1000)
        return true;
    delta = (uint64_t)(input->actual->battery_soc_mV_pct.value -
                       (int64_t)threshold);
    available = (capacity / 1000U) * delta +
        ((capacity % 1000U) * delta) / 1000U;
    return profile->energy_cost_mJ > available;
}

static bool physical_thermal_failure(const RevalidationInput *input)
{
    const TaskModeProfile *profile = &input->task->modes[input->mode];
    Subsystem subsystem = input->task->primary_subsystem;
    if (input->actual->temp_mC[subsystem].value >=
        input->thermal_limit_mC[subsystem]) return true;
    return profile->thermal_effect_mC > 0 &&
        input->actual->temp_mC[subsystem].value >
        input->thermal_limit_mC[subsystem] -
        (int64_t)profile->thermal_effect_mC;
}

static bool physical_failure(const RevalidationInput *input, uint8_t mask)
{
    return ((mask & GATE_MASK_SOC) != 0U && physical_soc_failure(input)) ||
        ((mask & GATE_MASK_THERMAL) != 0U && physical_thermal_failure(input));
}

static RevalidationTrigger observed_trigger(const RevalidationState *state,
    const RevalidationInput *input, uint8_t mask, uint8_t ledger_mask,
    bool fault_enabled)
{
    const TelemetryFrame *actual = input->actual;
    const AssumptionSnapshot *snapshot = input->assumption_snapshot;
    Subsystem subsystem = input->task->primary_subsystem;
    int id;
    if (fault_enabled && ((input->fault_set_mask & (UINT32_C(1) << (unsigned)subsystem)) != 0U ||
        actual->fault_flags.quality != TELEMETRY_QUALITY_OK ||
        ((uint32_t)actual->fault_flags.value &
         (UINT32_C(1) << (unsigned)subsystem)) != 0U))
        return REVALIDATION_TRIGGER_FAULT_FLAGS;
    if (input->now_ns < input->assumption_snapshot->forecast_horizon_end_ns &&
        state->has_observed_baseline)
        snapshot = &state->observed_baseline;
    if ((mask & GATE_MASK_SOC) != 0U &&
        (actual->battery_soc_mV_pct.quality != TELEMETRY_QUALITY_OK ||
        exceeds_downward(actual->battery_soc_mV_pct.value,
            snapshot->soc_permille, REVALIDATION_SOC_TOLERANCE_PERMILLE)))
        return REVALIDATION_TRIGGER_SOC;
    if ((mask & GATE_MASK_THERMAL) != 0U &&
        (actual->temp_mC[subsystem].quality != TELEMETRY_QUALITY_OK ||
        exceeds_upward(actual->temp_mC[subsystem].value,
            snapshot->temp_mC[subsystem], REVALIDATION_TEMP_TOLERANCE_MC)))
        return REVALIDATION_TRIGGER_THERMAL;
    if (mask == GATE_MASK_ALL &&
        (actual->current_draw_mA.quality != TELEMETRY_QUALITY_OK ||
        exceeds_upward(actual->current_draw_mA.value,
            snapshot->current_draw_mA, REVALIDATION_DRAW_TOLERANCE_MA)))
        return REVALIDATION_TRIGGER_CURRENT_DRAW;
    if ((mask & GATE_MASK_PROTECTED) != 0U &&
        (actual->cpu_avail_pct.quality != TELEMETRY_QUALITY_OK ||
        actual->mem_avail_KB.quality != TELEMETRY_QUALITY_OK ||
        actual->storage_avail_KB.quality != TELEMETRY_QUALITY_OK ||
        actual->sensor_avail_mask.quality != TELEMETRY_QUALITY_OK ||
        actual->comm_window_open.quality != TELEMETRY_QUALITY_OK))
        return REVALIDATION_TRIGGER_PROTECTED_RESIDUAL;
    for (id = 0; id < LEDGER_DIMENSION_COUNT; ++id) {
        LedgerDimension dimension;
        if (id == LEDGER_ENERGY ||
            (ledger_mask & (1U << (unsigned)id)) == 0U) continue;
        if (!ledger_query(input->ledger, (LedgerDimensionId)id, &dimension))
            return REVALIDATION_TRIGGER_PROTECTED_RESIDUAL;
        if (id == LEDGER_SENSOR ?
            (((uint32_t)dimension.committed &
              ~(uint32_t)dimension.capacity) != 0U) :
            dimension.committed > dimension.capacity)
            return REVALIDATION_TRIGGER_PROTECTED_RESIDUAL;
    }
    if ((mask & GATE_MASK_SOC) != 0U && physical_soc_failure(input))
        return REVALIDATION_TRIGGER_SOC;
    if ((mask & GATE_MASK_THERMAL) != 0U && physical_thermal_failure(input))
        return REVALIDATION_TRIGGER_THERMAL;
    return REVALIDATION_TRIGGER_NONE;
}

static RevalidationTrigger predicted_trigger(const RevalidationInput *input,
                                              uint8_t mask)
{
    uint64_t capacity;
    uint16_t floor;
    uint32_t threshold;
    uint64_t available;
    const TaskModeProfile *profile = &input->task->modes[input->mode];
    Subsystem subsystem = input->task->primary_subsystem;
    int64_t soc = input->forecast->battery_soc_mV_pct.value;
    int64_t temp = input->forecast->temp_mC[subsystem].value;
    if ((mask & GATE_MASK_SOC) != 0U) {
    if (!ledger_energy_config(input->ledger, &capacity, &floor))
        return REVALIDATION_TRIGGER_SOC;
    threshold = (uint32_t)floor + input->safety_margin_permille;
    if (input->forecast->battery_soc_mV_pct.quality != TELEMETRY_QUALITY_OK ||
        threshold > 1000 || soc < (int64_t)threshold || soc > 1000)
        return REVALIDATION_TRIGGER_SOC;
    available = (capacity / 1000U) * (uint64_t)(soc - (int64_t)threshold) +
        ((capacity % 1000U) * (uint64_t)(soc - (int64_t)threshold)) / 1000U;
    if (profile->energy_cost_mJ > available)
        return REVALIDATION_TRIGGER_SOC;
    }
    if ((mask & GATE_MASK_THERMAL) != 0U &&
        (input->forecast->temp_mC[subsystem].quality != TELEMETRY_QUALITY_OK ||
        temp >= input->thermal_limit_mC[subsystem] ||
        (profile->thermal_effect_mC > 0 &&
         temp > input->thermal_limit_mC[subsystem] -
         (int64_t)profile->thermal_effect_mC)))
        return REVALIDATION_TRIGGER_THERMAL;
    return REVALIDATION_TRIGGER_NONE;
}

RevalidationEvent revalidation_step(RevalidationState *state,
                                    const RevalidationInput *input)
{
    return revalidation_step_masked(state, input, GATE_MASK_ALL,
                                     LEDGER_MASK_ALL, true);
}

RevalidationEvent revalidation_step_masked(RevalidationState *state,
    const RevalidationInput *input, uint8_t gate_mask, uint8_t ledger_mask,
    bool fault_enabled)
{
    RevalidationTrigger observed;
    RevalidationTrigger predicted;
    InvalidationCause cause;
    bool fault_active;
    uint64_t observation_epoch;
    if (state == NULL || input == NULL || input->task == NULL ||
        input->assumption_snapshot == NULL || input->actual == NULL ||
        input->forecast == NULL || input->ledger == NULL || input->step_ns == 0 ||
        input->task->primary_subsystem < SUBSYSTEM_BATTERY ||
        input->task->primary_subsystem >= N_SUBSYSTEMS ||
        input->mode < MODE_FULL || input->mode > MODE_DEGRADED)
        return REVALIDATION_ERROR;
    observation_epoch = input->observation_epoch == 0 ?
        input->now_ns : input->observation_epoch;
    observed = observed_trigger(state, input, gate_mask, ledger_mask, fault_enabled);
    predicted = predicted_trigger(input, gate_mask);
    fault_active = fault_enabled && (input->actual->fault_flags.quality != TELEMETRY_QUALITY_OK ||
        ((uint32_t)input->actual->fault_flags.value &
         (UINT32_C(1) << (unsigned)input->task->primary_subsystem)) != 0U);
    if (state->invalid) {
        if (state->record.cause_type == INVALIDATION_PREDICTED_RISK) {
            if (observed != REVALIDATION_TRIGGER_NONE) {
                state->record.cause_type = observed == REVALIDATION_TRIGGER_FAULT_FLAGS ?
                    INVALIDATION_OBSERVED_FAULT : INVALIDATION_OBSERVED_DEVIATION;
                state->record.trigger_quantity = observed;
                state->record.upgraded_from_predicted =
                    input->now_ns <= state->record.forecast_horizon_end;
                state->record.K = REVALIDATION_CLEAR_TICKS;
                state->record.clear_count = 0;
                return REVALIDATION_UPGRADE;
            }
            return input->now_ns >= state->record.forecast_horizon_end &&
                observed == REVALIDATION_TRIGGER_NONE ?
                REVALIDATION_RESTORE_READY : REVALIDATION_NONE;
        }
        if (observed != REVALIDATION_TRIGGER_NONE || fault_active) {
            state->record.clear_count = 0;
            state->last_clear_tick_ns = input->now_ns;
            state->last_clear_observation_epoch = observation_epoch;
            return REVALIDATION_NONE;
        }
        if (!input->is_out_of_band &&
            observation_epoch != state->last_clear_observation_epoch &&
            input->now_ns > state->last_clear_tick_ns) {
            if (input->now_ns - state->last_clear_tick_ns != input->step_ns)
                state->record.clear_count = 0;
            state->last_clear_tick_ns = input->now_ns;
            state->last_clear_observation_epoch = observation_epoch;
            if (state->record.clear_count < REVALIDATION_CLEAR_TICKS)
                ++state->record.clear_count;
        }
        return state->record.clear_count >= REVALIDATION_CLEAR_TICKS ?
            REVALIDATION_RESTORE_READY : REVALIDATION_NONE;
    }
    cause = observed == REVALIDATION_TRIGGER_FAULT_FLAGS ?
        INVALIDATION_OBSERVED_FAULT :
        observed != REVALIDATION_TRIGGER_NONE ?
        INVALIDATION_OBSERVED_DEVIATION : INVALIDATION_PREDICTED_RISK;
    if (observed == REVALIDATION_TRIGGER_NONE &&
        predicted == REVALIDATION_TRIGGER_NONE) return REVALIDATION_NONE;
    if (cause == state->last_restored_cause &&
        input->now_ns < state->dwell_until_ns && !physical_failure(input, gate_mask) &&
        observed != REVALIDATION_TRIGGER_PROTECTED_RESIDUAL)
        return REVALIDATION_NONE;
    state->invalid = true;
    state->record = (InvalidationRecord){
        .task_id = input->task->task_id,
        .cause_type = cause,
        .trigger_quantity = observed != REVALIDATION_TRIGGER_NONE ?
            observed : predicted,
        .trigger_time_ns = input->now_ns,
        .original_mode = input->mode,
        .forecast_horizon_end = cause == INVALIDATION_PREDICTED_RISK ?
            input->forecast_horizon_end_ns : 0,
        .K = cause == INVALIDATION_PREDICTED_RISK ? 0U :
            REVALIDATION_CLEAR_TICKS
    };
    state->last_clear_tick_ns = input->now_ns;
    state->last_clear_observation_epoch = observation_epoch;
    return REVALIDATION_INVALIDATE;
}

bool revalidation_complete_restore(RevalidationState *state, uint64_t now_ns,
                                   uint64_t step_ns)
{
    uint64_t dwell;
    if (state == NULL || !state->invalid || step_ns == 0 ||
        step_ns > UINT64_MAX / REVALIDATION_DWELL_TICKS) return false;
    dwell = step_ns * REVALIDATION_DWELL_TICKS;
    if (now_ns > UINT64_MAX - dwell) return false;
    state->last_restored_cause = state->record.cause_type;
    state->dwell_until_ns = now_ns + dwell;
    state->invalid = false;
    return true;
}

bool revalidation_force_capacity_invalidation(RevalidationState *state,
    const Task *task, Mode mode, uint64_t now_ns)
{
    if (state == NULL || task == NULL || state->invalid) return false;
    state->invalid = true;
    state->record = (InvalidationRecord){
        .task_id = task->task_id,
        .cause_type = INVALIDATION_OBSERVED_DEVIATION,
        .trigger_quantity = REVALIDATION_TRIGGER_PROTECTED_RESIDUAL,
        .trigger_time_ns = now_ns,
        .original_mode = mode,
        .K = REVALIDATION_CLEAR_TICKS
    };
    state->last_clear_tick_ns = now_ns;
    return true;
}
