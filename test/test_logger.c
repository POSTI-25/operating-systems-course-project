#include "test.h"
#include "adapter/logger.h"

#include <string.h>

static void complete_decision_record_has_fixed_field_order(void)
{
    LogRecord record = {0};
    FILE *stream = tmpfile();
    char line[2048];
    const char *expected =
        "DECISION tick=6 sim_ns=12 trigger=FAULT task=7 state_before=PAUSED "
        "candidates=FULL/ELIMINATED/GATE_THERMAL_CEILING/0x02|"
        "THROTTLED/PASS/NONE/0x00|DEGRADED/PASS/NONE/0x00|"
        "DEFERRED/FALLBACK_ONLY/NONE/0x00|REJECTED/FALLBACK_ONLY/NONE/0x00 "
        "selected=THROTTLED score=123.500000 reason=SELECTED "
        "ledger_before=CPU:100:20:80:0|ENERGY:1000:100:1100:-200|"
        "MEMORY:500:0:50:450|STORAGE:600:0:60:540|"
        "SENSOR:0x0000000f:0x00000001:0x00000004:0x0000000a|"
        "COMM:100:20:0:80 "
        "ledger_after=CPU:100:20:80:0|ENERGY:1000:100:1100:-200|"
        "MEMORY:500:0:50:450|STORAGE:600:0:60:540|"
        "SENSOR:0x0000000f:0x00000001:0x00000004:0x0000000a|"
        "COMM:100:20:0:80 "
        "state_after=RUNNING "
        "invalidation=7:OBSERVED_FAULT:FAULT_FLAGS:12:FULL:20:3:3:1 "
        "restoration=1 remaining_ns=3 completed_ns=NA delivered_value=0\n";
    size_t i;
    ASSERT_TRUE(stream != NULL);
    record.tick = 6;
    record.sim_time_ns = 12;
    record.trigger = DECISION_TRIGGER_FAULT;
    record.task_id = 7;
    record.state_before = EXECUTION_PAUSED;
    record.state_after = EXECUTION_RUNNING;
    record.has_candidates = true;
    for (i = 0; i < SAGE_MAX_CANDIDATES; ++i) {
        record.candidates[i].mode = (Mode)i;
        record.candidates[i].verdict = i < 3 ?
            CANDIDATE_VERDICT_PASS : CANDIDATE_VERDICT_FALLBACK_ONLY;
    }
    record.candidates[MODE_FULL].verdict = CANDIDATE_VERDICT_ELIMINATED;
    record.candidates[MODE_FULL].failed_predicate = GATE_THERMAL_CEILING;
    record.candidates[MODE_FULL].skip_mask = 2;
    record.has_selected = true;
    record.selected_mode = MODE_THROTTLED;
    record.has_score = true;
    record.score = 123.5;
    record.reason = DECISION_REASON_SELECTED;
    record.ledger_before[LEDGER_CPU] = (LedgerDimension){100, 20, 80, 0};
    record.ledger_before[LEDGER_ENERGY] = (LedgerDimension){1000, 100, 1100, -200};
    record.ledger_before[LEDGER_MEMORY] = (LedgerDimension){500, 0, 50, 450};
    record.ledger_before[LEDGER_STORAGE] = (LedgerDimension){600, 0, 60, 540};
    record.ledger_before[LEDGER_SENSOR] = (LedgerDimension){15, 1, 4, 10};
    record.ledger_before[LEDGER_COMM] = (LedgerDimension){100, 20, 0, 80};
    memcpy(record.ledger_after, record.ledger_before,
           sizeof(record.ledger_after));
    record.has_invalidation = true;
    record.invalidation = (InvalidationRecord){7,
        INVALIDATION_OBSERVED_FAULT, REVALIDATION_TRIGGER_FAULT_FLAGS,
        12, MODE_FULL, 20, 3, 3, true};
    record.restoration = true;
    record.remaining_ns = 3;
    ASSERT_TRUE(logger_write(stream, &record));
    rewind(stream);
    ASSERT_TRUE(fgets(line, sizeof(line), stream) != NULL);
    ASSERT_TRUE(strcmp(expected, line) == 0);
    ASSERT_TRUE(fgets(line, sizeof(line), stream) == NULL);
    fclose(stream);
}

static void unchanged_record_and_invalid_inputs_are_handled(void)
{
    LogRecord record = {0};
    FILE *stream = tmpfile();
    char line[1024];
    ASSERT_TRUE(stream != NULL);
    record.trigger = DECISION_TRIGGER_TICK;
    record.reason = DECISION_REASON_ALREADY_RUNNING;
    record.state_before = EXECUTION_RUNNING;
    record.state_after = EXECUTION_RUNNING;
    ASSERT_TRUE(!logger_write(NULL, &record));
    ASSERT_TRUE(!logger_write(stream, NULL));
    record.trigger = (DecisionTrigger)99;
    ASSERT_TRUE(!logger_write(stream, &record));
    record.trigger = DECISION_TRIGGER_TICK;
    ASSERT_TRUE(logger_write(stream, &record));
    rewind(stream);
    ASSERT_TRUE(fgets(line, sizeof(line), stream) != NULL);
    ASSERT_TRUE(strstr(line, "candidates=- selected=NONE score=NA") != NULL);
    ASSERT_TRUE(strstr(line, "invalidation=NONE restoration=0") != NULL);
    fclose(stream);
}

int main(void)
{
    RUN_TEST(complete_decision_record_has_fixed_field_order);
    RUN_TEST(unchanged_record_and_invalid_inputs_are_handled);
    return test_failures != 0;
}
