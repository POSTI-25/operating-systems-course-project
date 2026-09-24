# Phase 5 Execution and Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the SAGE-RT host-PC MVP with one-shot simulated execution, complete decision evidence, eight scenarios, four same-engine variants, a 32-run report, and N1–N10 traceability.

**Architecture:** Extend the existing Governor decision path with a shadow-copied execution state, fixed-size decision records, and immutable variant masks. Keep the execution adapter thin and the Logger formatting-only. A virtual-clock replay harness and independent full-policy auditor produce deterministic decision goldens and a fair cross-variant report without changing scripted telemetry.

**Tech Stack:** C11, POSIX libc, existing Makefile and `test/test.h`; `-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror`; ASan/UBSan.

**Spec:** `docs/superpowers/specs/2026-09-25-phase-5-execution-evidence-design.md` (approved). PRD.md remains the source of truth.

## Global Constraints

- One-shot tasks only; `period_ns` is metadata. Invalidation pauses remaining duration; restoration resumes it after a Gate re-pass.
- Restoration reserves the selected mode's **full** energy cost as a conservative bound; no partial-energy model.
- Keep the same decision path for all four variants; no new Gate predicate, Ledger dimension, utility term, scenario directive, heap allocation in decision steps, threads, RTOS work, GUI, or third-party library.
- Preserve `scenarios/normal_orbit.scn` and `golden/normal_orbit.expected` unchanged.
- Goldens contain deterministic decision data; CPU time, cap, jitter, and modeled energy remain outside exact diff files.
- Do not tune scenarios, fixtures, thresholds, or reporting to force SAGE-RT to win. If the measured exit comparison fails, report it and seek a decision; do not mark Phase 5 done.
- Keep the repository private; do not publish code or evidence.

## File map

| File | Responsibility |
|---|---|
| `src/adapter/execution_adapter.[ch]` | Fixed per-task progress state and explicit-time start/advance/pause/resume. |
| `src/adapter/logger.[ch]` | `LogRecord` contract, name tables, deterministic DECISION serialization. |
| `src/core/governor.[ch]` | Shared-cycle integration, shadow trace publication, variant dispatch and completion release. |
| `src/core/gate.[ch]`, `src/core/utility.[ch]`, `src/core/ledger.[ch]` | Mask existing predicates/terms/admission checks without adding new ones. |
| `src/main.c` | Variant CLI selection and live Logger output. |
| `test/replay.[ch]`, `test/decision_golden_runner.c`, `test/test_replay.c` | Virtual-clock frame/fault/tick replay, decision golden driver. |
| `test/phase5_report.[ch]`, `test/report_runner.c`, `test/test_phase5_report.c` | Independent invariant audit, 32-run metrics and CSV. |
| `test/traceability_check.c` | Assert named N1–N10 evidence exists and matches required events/metrics. |
| `scenarios/*.scn`, `golden/*.decisions.expected` | Seven small scenarios and eight reviewed decision traces. |
| `docs/phase5_metrics.csv`, `docs/phase5_evidence.md`, `PRD.md` | Report, honest interpretation, final status/decision log. |

## Review Focus

1. Completion and a fault at the same instant: Task 2 test asserts completion wins and no completed task is invalidated.
2. A paused task with a still-held ENERGY draw: Task 2 test asserts restoration waits for a fresh quality-OK SOC timestamp, then reserves the full selected-mode cost.
3. Unchecked baseline admission with over-capacity demand: Task 6 test asserts the baseline records the demand and the independent audit catches the violation without weakening full mode.
4. Missing relevant telemetry during safety audit: Task 7 test asserts a conservative violation, never an optimistic pass.
5. A predicted false alarm at its exact horizon: Task 10 golden and integration test assert restoration through the Gate, unchanged elapsed work, and remaining-time resume.

---

### Task 1: Thin execution adapter

**Files:** Create `src/adapter/execution_adapter.h`, `src/adapter/execution_adapter.c`, `test/test_execution_adapter.c`; modify `Makefile`.

**Interfaces:** Consumes `Task`, `Mode`, and explicit `uint64_t now_ns`. Produces `ExecutionPhase` (`PENDING`, `RUNNING`, `PAUSED`, `COMPLETE`, `REJECTED`) and `ExecutionState { phase, remaining_ns, last_resume_ns, completed_ns, mode, delivered_value }`. Exact calls are `bool execution_start(ExecutionState *, const Task *, Mode, uint64_t now_ns)`, `bool execution_advance(ExecutionState *, const Task *, uint64_t now_ns, bool *completed)`, `bool execution_pause(ExecutionState *, const Task *, uint64_t now_ns)`, and `bool execution_resume(ExecutionState *, Mode, uint64_t now_ns)`. `execution_advance` reports whether completion occurred and stores its exact timestamp in state. All functions reject time reversal and arithmetic overflow.

- [ ] **Step 1: Write the failing test.** Assert `execution_start(task, FULL, 2)` sets remaining to `nominal_exec_ns`; advancing to 5 consumes 3; pausing at 5, advancing through 20 consumes 0; resuming at 20 and advancing to 27 completes at `20 + original_remaining`, once. Add tests for mode-change delivered value, exact boundary, backward time, and `period_ns` not triggering recurrence.

```c
ASSERT_TRUE(execution_start(&state, &task, MODE_FULL, 2));
ASSERT_TRUE(execution_advance(&state, &task, 5, &completed));
ASSERT_EQ_INT(0, completed);
ASSERT_EQ_INT(task.nominal_exec_ns - 3, state.remaining_ns);
ASSERT_TRUE(execution_pause(&state, &task, 5));
ASSERT_TRUE(execution_resume(&state, MODE_DEGRADED, 20));
```

- [ ] **Step 2: Verify RED.** Run `make build/test_execution_adapter`; expect an undefined header/API failure, not an unrelated compiler error.
- [ ] **Step 3: Implement the minimal adapter.** Use `elapsed = now_ns - last_resume_ns`, `used = min(elapsed, remaining_ns)`; on completion set `completed_ns = last_resume_ns + remaining_ns` before zeroing it; delivered value is `mission_value * value_fraction_permille / 1000` for the current mode. No clock call or allocation.

```c
if (state->phase == EXECUTION_RUNNING &&
    now_ns - state->last_resume_ns >= state->remaining_ns) {
    state->completed_ns = state->last_resume_ns + state->remaining_ns;
    state->remaining_ns = 0;
    state->phase = EXECUTION_COMPLETE;
    *completed = true;
}
```

- [ ] **Step 4: Verify GREEN and commit.** Run `make build/test_execution_adapter && ./build/test_execution_adapter` and `make test`; stage only Task 1 files and commit `feat: add one-shot execution adapter`.

### Task 2: Governor execution lifecycle and shadow rollback

**Files:** Modify `src/core/governor.[ch]`, `test/test_governor.c`, `test/test_governor_run.c`, `Makefile`.

**Interfaces:** Consumes Task 1's `ExecutionState` and existing `ledger_release_rate/stock`. Adds `ExecutionState execution[SAGE_MAX_TASKS]` inside `GovernorPlan` and read-only `governor_execution_at(const Governor *, size_t)`. No execution state is changed outside the cycle's shadow plan. The existing `Commitment` contract remains unchanged.

- [ ] **Step 1: Write failing integration tests.** Commit a task with `nominal_exec_ns=6`; cycle at 2 and assert `remaining_ns=4`; invalidate at 3, cycle at 8 while paused and assert remainder still 3; restore and finish after 3 more simulated ns. Assert rate/stock release on completion but ENERGY hold persists until a strictly newer quality-OK SOC timestamp. Assert a completion due exactly at fault time is complete and never invalidated. Inject an overrun and assert execution and logs remain at the prior committed plan. Update the old fork/pipe fault fixture to use a longer task so its t=3 fault precedes completion.

```c
ASSERT_EQ_INT(EXECUTION_PAUSED, governor_execution_at(&governor, 0)->phase);
ASSERT_EQ_INT(3, governor_execution_at(&governor, 0)->remaining_ns);
ASSERT_EQ_INT(GOVERNOR_CYCLE_OVERRUN, governor_cycle(&governor, 8, false));
ASSERT_EQ_INT(EXECUTION_PAUSED, governor_execution_at(&governor, 0)->phase);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_governor && ./build/test_governor`; expect the new lifecycle assertion/API to fail.
- [ ] **Step 3: Integrate in the existing cycle.** Copy `GovernorPlan` first; advance each RUNNING task to `now_ns` before task selection/revalidation; release rate/stock for newly COMPLETE tasks; call `execution_start` only after a successful Ledger commit; call `execution_pause` on invalidation and `execution_resume` only after `revalidation_complete_restore`. Skip COMPLETE/REJECTED tasks. At restoration create a local `Task task_view = *task; task_view.nominal_exec_ns = execution[i].remaining_ns;` for horizon, Gate completion, and deadline urgency. Keep full selected-mode energy cost in `ledger_commit`; never prorate it. Publish the shadow only at the existing end-of-cycle cap check.

```c
if (shadow.execution[i].phase == EXECUTION_RUNNING &&
    !execution_advance(&shadow.execution[i], task, now_ns, &completed))
    return GOVERNOR_CYCLE_ERROR;
if (completed && !ledger_release_rate(&shadow.ledger, task->task_id))
    return GOVERNOR_CYCLE_ERROR;
```

- [ ] **Step 4: Verify GREEN and commit.** Run `make build/test_governor build/test_governor_run && ./build/test_governor && ./build/test_governor_run`, then `make test`; stage Task 2 files and commit `feat: settle and pause governor execution`.

### Task 3: Complete LogRecord serializer

**Files:** Create `src/adapter/logger.[ch]`, `test/test_logger.c`; modify `Makefile`.

**Interfaces:** Define `CandidateVerdict`, `CandidateLogEntry { mode, verdict, failed_predicate, skip_mask }`, `DecisionReason`, `DecisionTrigger`, and `LogRecord` with the exact spec fields: tick/time/trigger/task, state before/after, five candidate entries, selected mode/score, reason, six Ledger dimensions before/after, optional full `InvalidationRecord`, restoration flag, remaining/completed time, delivered value. Produce `bool logger_write(FILE *, const LogRecord *)`. It has no Governor dependency and no allocation.

- [ ] **Step 1: Write failing serialization tests.** Build one record containing `PREDICTED_RISK` upgraded to `OBSERVED_FAULT`, all five candidate verdicts, a SENSOR mask, and negative Ledger free. Write to `tmpfile()`, rewind, and compare the entire line with an exact literal; assert all nine invalidation fields and six dimension tuples occur in fixed order. Test `NULL` output and invalid enum handling.

```c
LogRecord record = {0};
record.task_id = 7;
record.has_invalidation = true;
record.invalidation.upgraded_from_predicted = true;
ASSERT_TRUE(logger_write(stream, &record));
ASSERT_TRUE(strstr(line, "invalidation=7:") != NULL);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_logger`; expect missing logger API.
- [ ] **Step 3: Implement formatting only.** Use named enum-to-token functions and `fprintf`/`fputc`, check every return; no `sprintf` into an unbounded buffer. Emit `DECISION` fields and candidate tuples exactly as the spec states. For unchanged tasks use `candidates=-`; for a baseline-disabled predicate put its bit in `skip_mask` rather than mislabeling a pass.

```c
if (fprintf(output, "DECISION tick=%" PRIu64 " sim_ns=%" PRIu64,
            record->tick, record->sim_time_ns) < 0) return false;
```

- [ ] **Step 4: Verify GREEN and commit.** Run `make build/test_logger && ./build/test_logger`, then `make test`; commit `feat: serialize complete decision records`.

### Task 4: Governor decision-trace capture and publication

**Files:** Modify `src/core/governor.[ch]`, `test/test_governor.c`, `test/test_governor_run.c`, `src/main.c`.

**Interfaces:** Governor owns `LogRecord last_records[SAGE_MAX_TASKS]` and `size_t last_record_count`, published only when a shadow cycle commits. Expose `governor_log_record_at(const Governor *, size_t)`. Existing `select_task` fills candidate verdicts directly from `GateFilterResult`, selection from `SelectionResult`, and Ledger tuples from `ledger_query` before/after. `governor_run_pipe_logged` prints these through Task 3's Logger after each successful cycle.

- [ ] **Step 1: Write failing trace tests.** A high-value FULL candidate eliminated by thermal Gate must appear as `ELIMINATED/GATE_THERMAL_CEILING` while DEGRADED is selected with its actual utility score. A revalidation cycle must carry the complete `InvalidationRecord`; a restoration cycle must have `restoration=1`. An injected overrun must leave `last_record_count`/records unchanged and print only an `OVERRUN` line. The existing `TICK`/`PRED`/`SUMMARY` live metrics remain readable.

```c
const LogRecord *record = governor_log_record_at(&governor, 0);
ASSERT_TRUE(record != NULL);
ASSERT_EQ_INT(GATE_THERMAL_CEILING,
              record->candidates[MODE_FULL].failed_predicate);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_governor && ./build/test_governor`; expect missing trace API or failing verdict assertion.
- [ ] **Step 3: Capture records in the shared path.** Initialize one record per scenario task at cycle start, snapshot six Ledger dimensions before each task's decision and after it, attach evaluations and selection in `select_task`, attach revalidation events in the existing loop, and finalize execution state. Store records in a local fixed array; copy to Governor only after `governor->plan = shadow`. The live pipe path calls `logger_write` after `GOVERNOR_CYCLE_OK`, never before.

```c
if (result == GOVERNOR_CYCLE_OK)
    for (size_t i = 0; i < governor->last_record_count; ++i)
        if (!logger_write(output, &governor->last_records[i])) return false;
```

- [ ] **Step 4: Verify GREEN and commit.** Run targeted tests and `make test`; commit `feat: capture governor decision evidence`.

### Task 5: Immutable variant masks for existing Gate and Utility rules

**Files:** Modify `src/core/governor.[ch]`, `src/core/gate.[ch]`, `src/core/utility.[ch]`, `test/test_gate.c`, `test/test_utility.c`, `test/test_governor.c`.

**Interfaces:** Add `GovernorVariant` (`SAGE_FULL`, `FIXED_EDF`, `GOMX_STYLE`, `RANJBAR_STYLE`) and an immutable `GovernorVariantPolicy` returned by `bool governor_variant_policy(GovernorVariant, GovernorVariantPolicy *)`. Policy contains `mode_mask`, `gate_mask` (four existing predicates), `utility_term_mask` (the five existing terms), `ledger_mask`, `prediction_enabled`, and `immediate_fault_cycle`. Add `const SurvivorSet *gate_filter_masked(const Candidate *, size_t, const Task *, const GateContext *, uint8_t gate_mask, GateFilterResult *)` and `bool utility_select_masked(const SurvivorSet *, const Task *, const UtilityContext *, uint8_t term_mask, SelectionResult *)`; existing public functions remain all-on wrappers, preserving Phase 2/3 tests.

- [ ] **Step 1: Write failing policy tests.** Assert exact masks for all four variants. Verify a FULL candidate with low SOC/high temp is eliminated in full, passes fixed's zero Gate mask, fails GomX only on SOC, and fails Ranjbar only on thermal. Verify disabling an existing utility term changes only that term's contribution, never the delivered-value base. Verify class-0 task order and earliest deadline/ID within fixed baseline.

```c
ASSERT_TRUE(governor_variant_policy(GOVERNOR_VARIANT_GOMX_STYLE, &policy));
ASSERT_EQ_INT(GOVERNOR_GATE_SOC | GOVERNOR_GATE_DEADLINE,
              policy.gate_mask);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_gate build/test_utility && ./build/test_gate && ./build/test_utility`; expect absent masked APIs/assertion failure.
- [ ] **Step 3: Add masks without new rules.** Gate executes the same predicate bodies in the same order only when enabled; Utility multiplies disabled normalized terms by zero (value remains); Governor filters generated modes and chooses fixed's deadline tie-break order. Full policy calls the original all-on behavior. Masked-out modes are logged `DISABLED_BY_VARIANT` and skipped predicates are marked in the record.

```c
if ((gate_mask & GOVERNOR_GATE_SOC) != 0U &&
    context->telemetry->battery_soc_mV_pct.quality != TELEMETRY_QUALITY_OK)
    return failed(GATE_SOC_FLOOR);
```

- [ ] **Step 4: Verify GREEN and commit.** Run the three targeted binaries and `make test`; commit `feat: add same-engine variant rule masks`.

### Task 6: Masked Ledger admission and baseline cycle behavior

**Files:** Modify `src/core/ledger.[ch]`, `src/core/governor.c`, `test/test_ledger.c`, `test/test_governor.c`, `src/main.c`.

**Interfaces:** Add `ledger_commit_masked(Ledger *, uint32_t task_id, uint8_t criticality_class, ResourceDemand, uint64_t energy_cost_mJ, uint8_t dimension_mask)`; `ledger_commit` remains the all-dimension wrapper. A zero mask records unchecked baseline demand without vetoing admission; masks select existing dimensions only. The Governor variant selects full/all, fixed/none, GomX/ENERGY, or Ranjbar/non-ENERGY checks. Revalidation and aggregate shedding use only enabled inputs; the full variant is unchanged. `main` accepts `--variant full|fixed|gomx|ranjbar` without changing the existing `--tick-ms` meaning.

- [ ] **Step 1: Write failing tests.** With CPU capacity 10 and demand 20, full commit must fail while fixed masked commit succeeds and `ledger_query(CPU).free` is negative. GomX must reject an ENERGY shortfall but ignore thermal/fault/comm admission; Ranjbar must ignore SOC shortfall but honor thermal and non-energy residual. A t=3 fault triggers immediate full/Ranjbar replan but not GomX/fixed. Assert overflow and duplicate task IDs still fail under zero mask.

```c
ASSERT_TRUE(ledger_commit_masked(&ledger, 9, 1, demand, 100, 0));
ASSERT_TRUE(ledger_query(&ledger, LEDGER_CPU, &cpu));
ASSERT_TRUE(cpu.free < 0);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_ledger build/test_governor && ./build/test_ledger && ./build/test_governor`; expect the masked API or baseline assertion to fail.
- [ ] **Step 3: Implement checked bookkeeping and policy routing.** Validate all numeric ranges and unique IDs before recording even when the mask is zero; use the existing `LedgerReservation`. In Governor, skip disabled revalidation causes and disabled overcommit dimensions, and suppress immediate fault calls for tick-only policies. For GomX only, compute `harvest_mJ = solar_input_mW * H / 1000000000` outside eclipse using quotient/remainder or checked multiplication; pass it through existing `ledger_update_energy`, with current SOC (no Holt) so harvest is not counted twice. Never apply this harvest path to full mode.

```c
if ((dimension_mask & (UINT8_C(1) << (unsigned)id)) != 0U &&
    requested[id] > allowed) return false;
```

- [ ] **Step 4: Verify GREEN and commit.** Run targeted tests and `make test`; commit `feat: route baseline admission through shared ledger`.

### Task 7: Independent metrics auditor

**Files:** Create `test/phase5_report.h`, `test/phase5_report.c`, `test/test_phase5_report.c`; modify `Makefile`.

**Interfaces:** Define `Phase5Metrics` with the approved CSV fields and `phase5_metrics_init`, `phase5_audit_instant`, `phase5_note_fault`, `phase5_note_cycle`, `phase5_metrics_finish`, `phase5_metrics_write_csv`. This module reads the scenario, actual frame, and Governor state but never changes any of them. It does not consult variant masks when checking SOC, thermal, deadline, aggregate physical capacity, or protected access.

- [ ] **Step 1: Write failing tests.** Feed an over-capacity fixed admission and assert safety/protected violations rise; feed the identical full-safe decision and assert zero. Feed `MISSING` SOC or primary temperature and assert a conservative failure. Assert a rejected task increments `rejected_by_design` but not `deadline_misses`; a still-paused task past deadline does. Assert `NA` for zero class-0 denominator, absent predictions, and virtual jitter. Test `UINT64_MAX` accumulation returns failure instead of wrapping.

```c
ASSERT_TRUE(phase5_audit_instant(&metrics, &scenario, &governor,
                                &actual, 4));
ASSERT_TRUE(metrics.safety_violations > 0);
ASSERT_TRUE(metrics.protected_resource_violations > 0);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_phase5_report`; expect missing report API.
- [ ] **Step 3: Implement the observer.** At each distinct cycle instant, count active task-instants and evaluate all four full Gate concepts from actual telemetry and aggregate active demand, regardless of variant. Attribute resource breaches in deterministic criticality/deadline/ID order; count failing task-predicate pairs, with protected violations as a subset. Compute completed work from ExecutionState once, deadline misses from absolute deadlines, energy survival from first observed SOC below floor (censored at duration), recovery latency from a fault to first associated plan/safe-mode transition (censored if absent), prediction absolute error only from matched events, and CPU/modelled energy from actual run stats. Use explicit overflow checks before additions.

```c
if (UINT64_MAX - metrics->safety_violations < failures)
    return false;
metrics->safety_violations += failures;
```

- [ ] **Step 4: Verify GREEN and commit.** Run the report unit binary and `make test`; commit `feat: audit all variants against full invariants`.

### Task 8: Virtual replay and event cadence

**Files:** Create `test/replay.h`, `test/replay.c`, `test/test_replay.c`, `test/decision_golden_runner.c`; modify `Makefile`.

**Interfaces:** Define `typedef bool (*ReplayCycleHook)(const Governor *, const TelemetryFrame *, uint64_t now_ns, void *context)` and `bool replay_run(const Scenario *, Governor *, FILE *decision_output, ReplayCycleHook on_cycle, void *context)`. It uses `Simulator`, virtual `PlatformClock`, and existing `ScenarioFault` records. After every frame, it observes telemetry, runs all due regular ticks, then sends each same-time nonzero fault set as an immediate cycle only if `governor_variant_policy(governor->config.variant, &policy)` yields `policy.immediate_fault_cycle`. A local `static bool replay_cycle(Governor *, const TelemetryFrame *, uint64_t, uint32_t fault_mask, FILE *, ReplayCycleHook, void *)` calls Governor, Logger, and `on_cycle` in that order. `decision_golden_runner` loads one scenario, initializes full Governor with deterministic zero self-cost reservation and a nonbinding cap for stable goldens, and prints Logger records; it does not print CPU/jitter/cap. The report runner in Task 11 calibrates and reports a real cap separately; its decisions may differ from goldens if the measured reserve changes admission.

- [ ] **Step 1: Write failing replay tests.** Use a hand-crafted scenario with `STEP_NS=2`, a fault at 3, and a frame update at 4. Assert full sees cycles at 0,2,3,4; GomX sees 0,2,4 only; coincident fault at 4 is processed after the regular tick, matching `platform_ipc.c`. Assert repeated event time does not emit a duplicate regular tick and that completion at fault time wins.

```c
ASSERT_TRUE(replay_run(&scenario, &governor, output, capture_cycle, &seen));
ASSERT_EQ_INT(4, seen.count);
ASSERT_EQ_INT(3, seen.times[2]);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_replay`; expect absent replay API.
- [ ] **Step 3: Implement replay using the existing iterator.** Initialize virtual clock and simulator; for each yielded frame call `governor_observe_at`; execute `while (next_tick <= frame_time)` exactly as the pipe loop does; then route same-time fault-set messages. Run `logger_write` only for successfully committed cycles. A cycle overrun emits `OVERRUN` but does not publish its tentative decisions; an error fails the runner. Keep one reusable static/fixed Governor, never heap-allocate in a decision cycle.

```c
while (next_tick <= frame_time && next_tick <= scenario->duration_ns) {
    if (!replay_cycle(governor, next_tick, 0, output, on_cycle, context))
        return false;
    next_tick += scenario->step_ns;
}
```

- [ ] **Step 4: Verify GREEN and commit.** Run `make build/test_replay && ./build/test_replay`, then `make test`; commit `test: replay decisions on a virtual clock`.

### Task 9: Compact environmental and competition scenarios

**Files:** Create `scenarios/low_soc_eclipse.scn`, `high_thermal_load.scn`, `sudden_fault.scn`, `communication_blackout.scn`, `competing_payload_tasks.scn` and matching `golden/*.decisions.expected`; modify `test/test_scenario.c` and `Makefile`.

**Interfaces:** All files obey existing §13 directives exactly. Each has all 15 `INITIAL` channel entries (11 scalar channels plus four indexed temperatures) and three `TASK_MODE` profiles per task. Use no new parser behavior. `decision_golden_runner` from Task 8 produces the deterministic full-variant trace. In `test/test_scenario.c`, define a local `ScenarioCapture` struct with booleans for the five purpose assertions, then add `static bool load_and_replay(const char *path, ScenarioCapture *capture)` that opens/parses the file, runs replay, and fills the capture via a local `ReplayCycleHook`.

- [ ] **Step 1: Write failing scenario-purpose tests before files.** Each named fixture must load, run, and expose its intended evidence: low SOC/eclipse has `ECLIPSE=1` and a SOC Gate elimination; high thermal has `GATE_THERMAL_CEILING` and a surviving lower executing mode; sudden fault has a `trigger=FAULT` decision; blackout has `GATE_PROTECTED_RESIDUAL` for comm; competing tasks has at least one non-energy Ledger elimination. Count tasks/events to reject accidental fixture growth.

```c
ASSERT_TRUE(load_and_replay("scenarios/high_thermal_load.scn", &capture));
ASSERT_TRUE(capture.saw_thermal_elimination);
ASSERT_TRUE(capture.saw_lower_mode_selection);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_scenario && ./build/test_scenario`; expect the missing fixture assertion, not a parser crash.
- [ ] **Step 3: Add only required directives.** Clone the 16 valid initial channel shapes from `normal_orbit.scn`, change values/events for each purpose, and keep each task/event count minimal. Use `UPDATE`/`FAULT`/`ORBIT` only as already documented. Run `build/decision_golden_runner <file>` and inspect every elimination/selection; add the expected text with `apply_patch`, not automatic golden acceptance.

```text
SCENARIO communication_blackout
STEP_NS 2
UPDATE 3 COMM_WINDOW_OPEN 0 3 OK
TASK 2 10 9 4 1 600 PAYLOAD
```

- [ ] **Step 4: Verify GREEN and commit.** Run targeted scenario tests and diff each new golden with `./build/decision_golden_runner scenarios/<name>.scn | diff -u golden/<name>.decisions.expected -`; commit `test: add five focused decision scenarios`.

### Task 10: Minimal compound stress and false-alarm restoration

**Files:** Create `scenarios/compound_stress.scn`, `scenarios/false_alarm_restoration.scn`, their `golden/*.decisions.expected`, and `golden/normal_orbit.decisions.expected`; modify `test/test_replay.c`, `test/test_governor.c`, `Makefile`.

**Interfaces:** Uses Task 8 replay and Task 3 Logger. No new directive or policy. `compound_stress` contains exactly three tasks (one class-0 ADCS, two queued payloads), one `ORBIT`, two `FAULT` directives (one ADCS fault episode: set then clear), and one CPU-capacity `UPDATE`. Low SOC and near-ceiling ADCS temperature are initial values. The fault occurs inside eclipse; the later CPU drop sheds an active lower-priority payload and leaves capacity too small for class 0, so the existing Emergency Safe Mode remains necessary. Compound does not need a restoration; the separate false-alarm scenario proves N4. These few elements each serve a named requirement; do not add decorative events.

- [ ] **Step 1: Write failing end-to-end tests.** Compound capture must see ADCS fault during eclipse, low initial SOC, primary ADCS temperature below but near the configured ceiling, two payload task IDs, observed-fault invalidation, a later protected-residual invalidation ordered lowest priority/newest first, and `EMERGENCY_SAFE_MODE`. False-alarm capture must show `PREDICTED_RISK`, no observed upgrade, a restore event at/after the stored horizon, and unchanged remaining execution across the pause. Assert `normal_orbit` input and old telemetry golden are untouched.

```c
ASSERT_EQ_INT(3, compound.tasks.count);
ASSERT_EQ_INT(2, compound.fault_count);
ASSERT_EQ_INT(1, compound.update_count);
ASSERT_TRUE(capture.saw_emergency_safe_mode);
ASSERT_TRUE(capture.saw_protected_shedding);
```

- [ ] **Step 2: Verify RED.** Run `make build/test_replay && ./build/test_replay`; expect missing scenario/evidence failures.
- [ ] **Step 3: Build precise small fixtures and inspect traces.** For compound, use `STEP_NS 2`, `DURATION_NS 12`, `SOC_FLOOR_PERMILLE 200`, initial SOC `260`, initial ADCS temperature `99900 mC` with `100000 mC` limit, and initial CPU capacity `55`. Give class-0 ADCS CPU demand `25`, one long-running payload CPU demand `25`, and the second queued payload a minimum CPU demand above `30`; give ADCS and active payload durations/deadlines that keep them open at time 11. Use `ORBIT 0 12 3 4`, the fault set/clear pair below, and the single CPU update below. The fault clear does not force restoration: the nonzero ENERGY hold may still block it until a new SOC source sample, as designed. At time 11, CPU capacity 10 sheds the payload before class 0; no fourth task or extra stress event is permitted. For false alarm, use one payload task with `nominal_exec_ns=12`, deadline `40`, energy cost `1000` mJ in FULL, battery capacity `100000` mJ, floor `800`, `STEP_NS=2`, and duration `36`. Keep CPU unavailable until `UPDATE 8 CPU_AVAIL_PCT 100 8 OK`, so the task commits after the safe forecast at time 8. Start SOC at `850`; set source-timestamped SOC to `840,830,820,815,815,840` at times `2,4,6,8,10,12` respectively; hold 840 thereafter. With default `α=0.3`, `β=0.1`, and horizon 14 ns, Holt forecasts approximately 813 permille at t=8 (Gate-safe) and 806 at t=10 (predicted risk below the 810-permille energy threshold), while actual SOC is 815. The risk horizon is t=24; forecast has recovered by then, so restoration resumes 10 remaining ns and completes at t=34. Review the generated trace and add exact goldens with `apply_patch`.

```text
ORBIT 0 12 3 4
FAULT 3 0x8 0x0
FAULT 4 0x0 0x8
UPDATE 11 CPU_AVAIL_PCT 10 11 OK
```

- [ ] **Step 4: Verify GREEN and commit.** Run `make build/test_replay && ./build/test_replay`; diff all three new goldens; run `make test`; commit `test: prove compound stress and restoration end to end`.

### Task 11: Thirty-two-run CSV and honest comparison

**Files:** Create `test/report_runner.c`, `docs/phase5_metrics.csv`, `docs/phase5_evidence.md`; modify `test/test_phase5_report.c`, `Makefile`, `src/main.c` if CLI variant parsing remains incomplete.

**Interfaces:** `report_runner` loops over exactly eight scenario paths × four `GovernorVariant` values and writes the fixed CSV header/32 rows via `phase5_metrics_write_csv`. A local `static bool run_report_pair(const char *path, GovernorVariant variant, FILE *output)` loads the scenario, initializes a fresh Governor and metrics object, calibrates self-cost with the existing `governor_calibrate_self_cost` using the chosen report tick interval and 2.0 W assumption, then runs Task 8 replay with Task 7 callbacks. It uses no separate scheduler implementation. In `test/test_phase5_report.c`, add local CSV helpers `report_row_count(FILE *)` and `report_has_pair(FILE *, const char *, const char *)`; they rewind and scan exact comma-delimited cells. `docs/phase5_evidence.md` summarizes each category and compound stress against all three comparators and labels exogenous/censored/NA fields.

- [ ] **Step 1: Write failing report tests.** Assert exactly 32 distinct `(scenario,variant)` rows, header matches the spec verbatim, every numeric field parses, `NA` appears where required, and no scenario/variant crashes. Assert a report with no class-0 task does not claim 100% critical completion. Add a test that intentionally constructs a comparator better than full and confirms the summarizer reports the loss rather than rewriting data.

```c
ASSERT_EQ_INT(32, report_row_count(stream));
ASSERT_TRUE(report_has_pair(stream, "compound_stress", "SAGE_FULL"));
ASSERT_TRUE(report_has_pair(stream, "compound_stress", "GOMX_STYLE"));
```

- [ ] **Step 2: Verify RED.** Run `make build/test_phase5_report && ./build/test_phase5_report`; expect the new matrix/header assertion to fail.
- [ ] **Step 3: Implement the matrix runner and generate evidence.** Iterate a literal eight-path array and four-value enum array, initialize a fresh Governor and metrics record per pair, run virtual replay, and serialize one CSV row. Populate `docs/phase5_metrics.csv` from the verified runner output and write `docs/phase5_evidence.md` using observed rows only. Include deadline misses, safety/protected violations, rejected-by-design, mission value, survival/censoring, recovery latency, completion ratios, replans, CPU/cap/modeled energy, jitter, and prediction error. State explicitly if the requested SAGE dominance is absent.

```c
for (size_t s = 0; s < 8; ++s)
    for (size_t v = 0; v < 4; ++v)
        if (!run_report_pair(paths[s], variants[v], output)) return 1;
```

- [ ] **Step 4: Verify GREEN and commit.** Run targeted tests, `build/report_runner` with output inspected, and `make test`. Commit report code and evidence as `feat: report all eight scenarios across four variants` only after confirming rows are not cherry-picked or altered.

### Task 12: N1–N10 traceability, final regression, and status

**Files:** Create `test/traceability_check.c`; modify `docs/phase5_evidence.md`, `Makefile`, `PRD.md`; update any regression tests affected by the approved execution lifecycle.

**Interfaces:** The checker reads named test outcomes, eight golden decision logs, and report rows, failing if any N1–N10 evidence item is absent. A local `static bool evidence_has(const char *id, const char *scenario, const char *token)` scans the named golden/report row and prints a precise diagnostic if missing; it is called by a `RUN_TEST` function, not from `main` directly. It verifies evidence existence and expected event/metric semantics; it does not create new scheduling mechanisms. Makefile `test` and `sanitize` run all unit tests, old normal-orbit telemetry golden, eight decision-golden diffs, report-matrix check, and traceability check.

- [ ] **Step 1: Write the failing traceability check.** Require: N1 unsafe-high-utility Gate test/trace; N2 battery-pass/comm-fail trace; N3 scenario-7 protected metric; N4 scenario-8 restore log; N5 mode distribution vs GomX; N6 measured overhead/cap line; N7 full vs tick-only recovery latency; N8 thermal/fault ablation rows; N9 A/B and low-SOC trace; N10 compound emergency record. Each check names its source file/test in the error message.

```c
ASSERT_TRUE(evidence_has("N4", "false_alarm_restoration", "restoration=1"));
ASSERT_TRUE(evidence_has("N10", "compound_stress", "EMERGENCY_SAFE_MODE"));
```

- [ ] **Step 2: Verify RED.** Run `make build/traceability_check && ./build/traceability_check`; expect failures identifying incomplete evidence, not a false green.
- [ ] **Step 3: Wire the checks and inspect the claims.** Add `traceability_check` and all eight golden diffs to `make test` and `make sanitize`; document one named test/log/metric for every N item. If a novelty item lacks end-to-end evidence, add the narrowest relevant test or report row and rerun; do not add new scheduler policy. Update PRD §18 with measured comparison, limitations, and final notes. Only if all exit criteria are met—including the user's comparison criterion—set Phase 5 §16 to `✅ Done` and top-level status to MVP complete; otherwise leave Phase 5 open and report the finding for direction.

```make
test: all
	@./build/traceability_check
	@set -e; for scenario in $(SCENARIO_NAMES); do ./build/decision_golden_runner scenarios/$$scenario.scn | diff -u golden/$$scenario.decisions.expected -; done
```

- [ ] **Step 4: Verify GREEN and commit.** Run fresh `make`, `make test`, `make sanitize`, `git diff --check`, and inspect `docs/phase5_metrics.csv` row count and the compound comparison. Record exact outcomes in the handoff. Commit `test: verify phase 5 traceability and MVP exit` only after the status accurately reflects those outcomes.

## Self-review and handoff

This integrated plan is one sequence because execution state, Governor trace, variant flags, replay, and audit interfaces depend on one another; splitting it into independent plans would duplicate interface design. Each task has a specific red/green test and a reviewable deliverable. The five Review Focus cases are assigned to Tasks 2, 2, 6, 7, and 10. The plan covers every approved spec section without a new PRD directive, predicate, dimension, utility term, or recurring-job mechanism.

Before implementation, the user reviews this plan. The recommended execution method is **Native** (`superpowers:executing-plans`): the tasks are tightly sequenced through shared C interfaces, so one implementer can maintain the contracts and obtain a whole-branch review at the end. No implementation starts from plan approval alone unless the user confirms the plan and execution method.
