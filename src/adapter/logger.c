#include "logger.h"

#include <inttypes.h>
#include <stddef.h>

static const char *mode_name(Mode mode)
{
    static const char *const names[] = {
        "FULL", "THROTTLED", "DEGRADED", "DEFERRED", "REJECTED"
    };
    return mode >= MODE_FULL && mode <= MODE_REJECTED ? names[mode] : NULL;
}

static const char *phase_name(ExecutionPhase phase)
{
    static const char *const names[] = {
        "PENDING", "RUNNING", "PAUSED", "COMPLETE", "REJECTED"
    };
    return phase >= EXECUTION_PENDING && phase <= EXECUTION_REJECTED ?
        names[phase] : NULL;
}

static const char *verdict_name(CandidateVerdict verdict)
{
    static const char *const names[] = {
        "PASS", "ELIMINATED", "FALLBACK_ONLY", "DISABLED_BY_VARIANT"
    };
    return verdict >= CANDIDATE_VERDICT_PASS &&
        verdict <= CANDIDATE_VERDICT_DISABLED_BY_VARIANT ? names[verdict] : NULL;
}

static const char *predicate_name(GatePredicate predicate)
{
    static const char *const names[] = {
        "NONE", "GATE_SOC_FLOOR", "GATE_THERMAL_CEILING",
        "GATE_PROTECTED_RESIDUAL", "GATE_DEADLINE", "GATE_INVALID_INPUT"
    };
    return predicate >= GATE_PREDICATE_NONE &&
        predicate <= GATE_INVALID_INPUT ? names[predicate] : NULL;
}

static const char *reason_name(DecisionReason reason)
{
    static const char *const names[] = {
        "SELECTED", "DEFERRED", "REJECTED", "EMERGENCY_SAFE_MODE",
        "RESTORE_BLOCKED_GATE", "RESTORE_BLOCKED_BUDGET",
        "ALREADY_RUNNING", "PAUSED", "COMPLETED"
    };
    return reason >= DECISION_REASON_SELECTED &&
        reason <= DECISION_REASON_COMPLETED ? names[reason] : NULL;
}

static const char *trigger_name(DecisionTrigger trigger)
{
    return trigger == DECISION_TRIGGER_TICK ? "TICK" :
        trigger == DECISION_TRIGGER_FAULT ? "FAULT" : NULL;
}

static const char *cause_name(InvalidationCause cause)
{
    static const char *const names[] = {
        "OBSERVED_FAULT", "OBSERVED_DEVIATION", "PREDICTED_RISK"
    };
    return cause >= INVALIDATION_OBSERVED_FAULT &&
        cause <= INVALIDATION_PREDICTED_RISK ? names[cause] : NULL;
}

static const char *quantity_name(RevalidationTrigger quantity)
{
    static const char *const names[] = {
        "NONE", "SOC", "THERMAL", "CURRENT_DRAW",
        "PROTECTED_RESIDUAL", "FAULT_FLAGS"
    };
    return quantity >= REVALIDATION_TRIGGER_NONE &&
        quantity <= REVALIDATION_TRIGGER_FAULT_FLAGS ? names[quantity] : NULL;
}

static bool valid_record(const LogRecord *record)
{
    size_t i;
    if (record == NULL || trigger_name(record->trigger) == NULL ||
        phase_name(record->state_before) == NULL ||
        phase_name(record->state_after) == NULL ||
        reason_name(record->reason) == NULL ||
        (record->has_selected && mode_name(record->selected_mode) == NULL))
        return false;
    if (record->has_candidates) {
        for (i = 0; i < SAGE_MAX_CANDIDATES; ++i) {
            const CandidateLogEntry *entry = &record->candidates[i];
            if (entry->mode != (Mode)i || verdict_name(entry->verdict) == NULL ||
                predicate_name(entry->failed_predicate) == NULL) return false;
        }
    }
    if (record->has_invalidation &&
        (cause_name(record->invalidation.cause_type) == NULL ||
         quantity_name(record->invalidation.trigger_quantity) == NULL ||
         mode_name(record->invalidation.original_mode) == NULL)) return false;
    for (i = 0; i < 2; ++i) {
        const LedgerDimension *sensor = i == 0 ?
            &record->ledger_before[LEDGER_SENSOR] :
            &record->ledger_after[LEDGER_SENSOR];
        if (sensor->capacity < 0 || sensor->capacity > UINT32_MAX ||
            sensor->protected_reserve < 0 ||
            sensor->protected_reserve > UINT32_MAX ||
            sensor->committed < 0 || sensor->committed > UINT32_MAX ||
            sensor->free < 0 || sensor->free > UINT32_MAX) return false;
    }
    return true;
}

static bool write_ledger(FILE *output, const LedgerDimension *dimensions)
{
    static const char *const names[LEDGER_DIMENSION_COUNT] = {
        "CPU", "ENERGY", "MEMORY", "STORAGE", "SENSOR", "COMM"
    };
    size_t i;
    for (i = 0; i < LEDGER_DIMENSION_COUNT; ++i) {
        const LedgerDimension *dimension = &dimensions[i];
        if (i != 0 && fputc('|', output) == EOF) return false;
        if (i == LEDGER_SENSOR) {
            if (fprintf(output,
                "%s:0x%08" PRIx32 ":0x%08" PRIx32 ":0x%08" PRIx32
                ":0x%08" PRIx32, names[i],
                (uint32_t)dimension->capacity,
                (uint32_t)dimension->protected_reserve,
                (uint32_t)dimension->committed,
                (uint32_t)dimension->free) < 0) return false;
        } else if (fprintf(output,
            "%s:%" PRId64 ":%" PRId64 ":%" PRId64 ":%" PRId64,
            names[i], dimension->capacity, dimension->protected_reserve,
            dimension->committed, dimension->free) < 0) return false;
    }
    return true;
}

bool logger_write(FILE *output, const LogRecord *record)
{
    size_t i;
    if (output == NULL || !valid_record(record)) return false;
    if (fprintf(output, "DECISION tick=%" PRIu64 " sim_ns=%" PRIu64
        " trigger=%s task=%" PRIu32 " state_before=%s candidates=",
        record->tick, record->sim_time_ns, trigger_name(record->trigger),
        record->task_id, phase_name(record->state_before)) < 0) return false;
    if (!record->has_candidates) {
        if (fputc('-', output) == EOF) return false;
    } else {
        for (i = 0; i < SAGE_MAX_CANDIDATES; ++i) {
            const CandidateLogEntry *entry = &record->candidates[i];
            if (i != 0 && fputc('|', output) == EOF) return false;
            if (fprintf(output, "%s/%s/%s/0x%02x", mode_name(entry->mode),
                verdict_name(entry->verdict),
                predicate_name(entry->failed_predicate),
                (unsigned)entry->skip_mask) < 0) return false;
        }
    }
    if (fprintf(output, " selected=%s score=",
        record->has_selected ? mode_name(record->selected_mode) : "NONE") < 0)
        return false;
    if (record->has_score) {
        if (fprintf(output, "%.6f", record->score) < 0) return false;
    } else if (fprintf(output, "NA") < 0) return false;
    if (fprintf(output, " reason=%s ledger_before=",
        reason_name(record->reason)) < 0 ||
        !write_ledger(output, record->ledger_before) ||
        fprintf(output, " ledger_after=") < 0 ||
        !write_ledger(output, record->ledger_after) ||
        fprintf(output, " state_after=%s invalidation=",
            phase_name(record->state_after)) < 0) return false;
    if (record->has_invalidation) {
        const InvalidationRecord *invalidation = &record->invalidation;
        if (fprintf(output, "%" PRIu32 ":%s:%s:%" PRIu64 ":%s:%" PRIu64
            ":%" PRIu32 ":%" PRIu32 ":%d",
            invalidation->task_id, cause_name(invalidation->cause_type),
            quantity_name(invalidation->trigger_quantity),
            invalidation->trigger_time_ns,
            mode_name(invalidation->original_mode),
            invalidation->forecast_horizon_end,
            invalidation->clear_count, invalidation->K,
            invalidation->upgraded_from_predicted ? 1 : 0) < 0) return false;
    } else if (fprintf(output, "NONE") < 0) return false;
    if (fprintf(output, " restoration=%d remaining_ns=%" PRIu64
        " completed_ns=", record->restoration ? 1 : 0,
        record->remaining_ns) < 0) return false;
    if (record->has_completed_ns) {
        if (fprintf(output, "%" PRIu64, record->completed_ns) < 0) return false;
    } else if (fprintf(output, "NA") < 0) return false;
    return fprintf(output, " delivered_value=%" PRIu32 "\n",
        record->delivered_value) >= 0;
}
