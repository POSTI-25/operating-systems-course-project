#ifndef SAGE_LOGGER_H
#define SAGE_LOGGER_H

#include "core/revalidation.h"
#include "execution_adapter.h"

#include <stdbool.h>
#include <stdio.h>

typedef enum {
    CANDIDATE_VERDICT_PASS = 0,
    CANDIDATE_VERDICT_ELIMINATED = 1,
    CANDIDATE_VERDICT_FALLBACK_ONLY = 2,
    CANDIDATE_VERDICT_DISABLED_BY_VARIANT = 3
} CandidateVerdict;

typedef struct {
    Mode mode;
    CandidateVerdict verdict;
    GatePredicate failed_predicate;
    uint8_t skip_mask;
} CandidateLogEntry;

typedef enum {
    DECISION_REASON_SELECTED = 0,
    DECISION_REASON_DEFERRED = 1,
    DECISION_REASON_REJECTED = 2,
    DECISION_REASON_EMERGENCY_SAFE_MODE = 3,
    DECISION_REASON_RESTORE_BLOCKED_GATE = 4,
    DECISION_REASON_RESTORE_BLOCKED_BUDGET = 5,
    DECISION_REASON_ALREADY_RUNNING = 6,
    DECISION_REASON_PAUSED = 7,
    DECISION_REASON_COMPLETED = 8
} DecisionReason;

typedef enum {
    DECISION_TRIGGER_TICK = 0,
    DECISION_TRIGGER_FAULT = 1
} DecisionTrigger;

typedef struct {
    uint64_t tick;
    uint64_t sim_time_ns;
    DecisionTrigger trigger;
    uint32_t task_id;
    ExecutionPhase state_before;
    ExecutionPhase state_after;
    bool has_candidates;
    CandidateLogEntry candidates[SAGE_MAX_CANDIDATES];
    bool has_selected;
    Mode selected_mode;
    bool has_score;
    double score;
    DecisionReason reason;
    LedgerDimension ledger_before[LEDGER_DIMENSION_COUNT];
    LedgerDimension ledger_after[LEDGER_DIMENSION_COUNT];
    bool has_invalidation;
    InvalidationRecord invalidation;
    bool restoration;
    uint64_t remaining_ns;
    bool has_completed_ns;
    uint64_t completed_ns;
    uint32_t delivered_value;
} LogRecord;

bool logger_write(FILE *output, const LogRecord *record);

#endif
