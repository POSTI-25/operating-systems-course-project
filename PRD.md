# SAGE-RT — Product/Project Requirements Document
**Safety-Assured, Gridless Energy-Aware Real-Time Scheduler for Spacecraft Flight Software**

This is the single source of truth for the MVP build. AI coding agents (GitHub Copilot / Luna, Antigravity) must read the relevant sections before touching code, implement only the phase they are asked for, and never introduce behavior that isn't specified here. If something is ambiguous, stop and flag it in §18 rather than guessing.

Status: **Phase 1 complete.**

---
 
## 1. Project Overview

SAGE-RT is a decision-governor loop, not a kernel scheduler. Each cycle it converts spacecraft telemetry into a safe, revocable execution plan for a set of pending tasks. The MVP is a host-PC simulation with synthetic telemetry (no real RTOS, no real hardware). Interfaces are shaped so a later RTOS backend (RTEMS/cFS/F') could replace the simulator without redesigning the core (explicitly out of scope for the MVP — see §2).

Core idea: **gate-then-rank**. Every candidate execution mode for every task is checked against hard safety predicates *before* any value-based ranking happens. Only survivors are ranked. A committed decision can later be invalidated as new telemetry arrives, and — uniquely — can be *restored* if the risk that caused the invalidation turns out not to have materialized.

## 2. Explicit Non-Goals (MVP)

Do **not** build any of the following unless a later phase explicitly says so:

- Machine-learning-based prediction.
- Integration with RTEMS, NASA cFS, or NASA/JPL F' (post-MVP Phase 7).
- Constellation / multi-spacecraft coordination (post-MVP Phase 8).
- A bounded joint-optimization / MILP mode generator.
- A kernel-level scheduler policy.
- Any GUI or web front end.
- Formal verification of the gate's predicates.
- Any third-party build system (CMake), test framework (Unity or otherwise), JSON library, or plotting library.
- Any Linux-only API (`epoll`, `timerfd`, `eventfd`). Use portable POSIX only (see §11, §17).
- Threads. Concurrency is via `fork()` + `pipe()` only (see §11).

## 3. Glossary (canonical names — do not rename or use synonyms)

| Term | Meaning |
|---|---|
| **Tick** | One iteration of the fixed-period decision cycle. |
| **Governor** | The main loop: telemetry → prediction → candidate generation → gate → ledger check → rank → commit → execute → revalidate. |
| **Mode** | One of: `FULL`, `THROTTLED`, `DEGRADED`, `DEFERRED`, `REJECTED`. |
| **Executing mode** | `FULL`, `THROTTLED`, `DEGRADED` — modes that consume resources and are ranked by utility. |
| **Fallback mode** | `DEFERRED`, `REJECTED` — never ranked; selected only when no executing mode survives the gate. |
| **Candidate** | A (task, mode) pair with a predicted resource cost and predicted delivered value. |
| **Gate** | The Safety-Invariant Gate: four predicates, evaluated per candidate, before ranking. |
| **Ledger** | The Mission Safety Resource Ledger: per-dimension committed / protected-reserved / free capacity. |
| **Class 0 task** | Safety-critical: attitude control or FDIR. May never be `DEFERRED` or `REJECTED`. May draw the protected pool. |
| **Commitment** | A selected candidate that has been reserved in the Ledger, plus the assumption snapshot it was chosen under. |
| **Assumption snapshot** | The predicted values (SOC, temp, etc.) a commitment was made under; compared against new telemetry to decide invalidation. |
| **Invalidation** | Marking a commitment invalid, releasing its Ledger reservation, and re-running candidate generation for that task. |
| **Restoration** | Returning an invalidated task's commitment to a prior mode, only after re-passing the Gate. |
| **Self-cost** | The Governor's own CPU/energy cost per tick; a protected Ledger line item. |

## 4. Architecture & Data Flow

```
Simulator (forked process)
   │  pipe: telemetry frames + fault-escalation events
   ▼
Telemetry Abstraction Layer  ──┐
                                │
Task & Mission Model  ─────────┤
                                ▼
                       Prediction Engine (Holt's method + orbit-phase correction)
                                │  predicted SOC / temp / draw, at horizon H
                                ▼
      per task, class-0 first, then criticality order:
                       Execution-Mode Generator
                    (FULL / THROTTLED / DEGRADED / DEFERRED / REJECTED)
                                ▼
                    Safety-Invariant Gate  ◄────  Ledger (query only)
                 (SOC floor, thermal ceiling,
                  protected-residual, deadline)
                                │ survivors only
                                ▼
          Executing-mode survivors → Utility & Selection (Option A, net-benefit)
          No executing survivor    → Fallback rule (§7.3)
                                ▼
                 Commit → Ledger reservation (mutate) → Execution Adapter → Logger
                                ▲
        Revalidation Loop ───────┘ (compare telemetry to each commitment's
        (every tick)               assumption snapshot; invalidate / release /
                                    re-run; restore through the Gate)
```

### 4.1 Directory layout

```
/src
  /platform      platform_clock.[ch]  platform_wait.[ch]  platform_ipc.[ch]
  /core          telemetry.[ch]  task_model.[ch]  ledger.[ch]  gate.[ch]
                 mode_generator.[ch]  utility.[ch]  prediction.[ch]
                 revalidation.[ch]  governor.[ch]
  /adapter       execution_adapter.[ch]  logger.[ch]
  /sim           simulator.[ch]          (forked telemetry/fault generator)
  main.c
/scenarios       *.scn  (line-oriented scenario files, §13)
/golden          expected logs/metrics per scenario, for golden-file tests
/test            test.h  test_*.c  (one file per module)
Makefile
PRD.md
```

Each module owns one `.c`/`.h` pair. No module reaches into another's internals — only through the header's declared API.

## 5. Data Contracts

All units are fixed-point integers in milli-units unless stated otherwise (no floats in the Ledger; floats are fine in Prediction).

**TelemetryFrame** (per channel: `battery_soc_mV_pct`, `charge_rate_mA`, `solar_input_mW`, `eclipse` (bool), `temp_mC[N_SUBSYSTEMS]`, `current_draw_mA`, `fault_flags`, `comm_window_open` (bool), `cpu_avail_pct`, `mem_avail_KB`, `storage_avail_KB`, `sensor_avail_mask`) plus, per channel, `timestamp_ns` and `quality` (`OK` / `STALE` / `MISSING`).

The named subsystem enum for `temp_mC` is:

```c
typedef enum {
    SUBSYSTEM_BATTERY = 0,
    SUBSYSTEM_BUS = 1,
    SUBSYSTEM_PAYLOAD = 2,
    SUBSYSTEM_ADCS = 3,
    N_SUBSYSTEMS = 4
} Subsystem;
```

**Task** — `task_id`, `period_ns`, `deadline_ns`, `nominal_exec_ns`, `criticality_class` (0 = safety-critical), `mission_value` (0–1000), per mode (`FULL/THROTTLED/DEGRADED`): `energy_cost_mJ`, `thermal_effect_mC`, `value_fraction_permille` (0–1000; e.g., full=1000, throttled=700, degraded=400 — calibrated per task type), `resource_demand` (cpu_pct, mem_KB, storage_KB, sensor_mask, comm_pct).

**Candidate** — `task_id`, `mode`, `predicted_cost` (per dimension), `delivered_value` = `mission_value × value_fraction_permille / 1000`.

**LedgerDimension** — one of `CPU`, `ENERGY`, `MEMORY`, `STORAGE`, `SENSOR`, `COMM`. Each has `capacity`, `protected_reserve`, `committed`, `free` (derived, never stored redundantly: `free = capacity − protected_reserve − committed`, and for rate dimensions `committed` is *derived* each tick as the sum of active commitments' demand rather than mutated in place — see §10).

**Commitment** — `task_id`, `mode`, `assumption_snapshot` (predicted SOC/temp/draw at commit time, plus `forecast_horizon_end`), `commit_time_ns`.

**InvalidationRecord** — `task_id`, `cause_type` (`OBSERVED_FAULT` / `OBSERVED_DEVIATION` / `PREDICTED_RISK`), `trigger_quantity`, `trigger_time_ns`, `original_mode`, `forecast_horizon_end` (predicted-cause only), `clear_count` / `K` (observed-cause only), `upgraded_from_predicted` (bool).

**LogRecord** (one per decision, JSONL-free — line-oriented, §13 format) — tick number, sim time, per-task: candidates considered, which the Gate eliminated and by which predicate, selected candidate + utility score (or fallback reason), Ledger state before/after, any invalidation/restoration event with full `InvalidationRecord`.

## 6. Decision Cycle (fixed order — do not reorder)

1. Receive/refresh telemetry frame (mark stale/missing channels).
2. Prediction Engine updates Holt state per channel; compute forecast at horizon H per pending task.
3. For each task, class-0 first then criticality order: generate up to 5 candidates.
4. Gate each candidate (§7.1). Record the failing predicate for eliminated candidates.
5. If any executing-mode candidate survives: rank survivors by utility (§7.2), select the max.
   If none survive: apply the fallback rule (§7.3).
6. Commit the selection: reserve in the Ledger, store the assumption snapshot.
7. Execution Adapter simulates execution; Logger records the full decision.
8. Revalidation Loop (§9) runs every tick against all open commitments, independent of the above per-task loop.

Steps 1–7 repeat at a fixed baseline tick (default 1 Hz simulated). A fault-escalation event (via the sim pipe) triggers an immediate out-of-band cycle for the affected task(s), independent of the next scheduled tick.

## 7. Formal Rules

### 7.1 Safety-Invariant Gate — four predicates, all must pass

1. **Battery-SOC floor**: `predicted_post_exec_SOC ≥ hard_floor + safety_margin`.
2. **Thermal-excursion ceiling**: `predicted_temp_rise[subsystem] ≤ remaining_thermal_margin[subsystem]`.
3. **Protected-residual check**: committing must not push any dimension's `free` below zero, i.e. must not draw the `protected_reserve` unless `criticality_class == 0`.
4. **Deadline validity**: `predicted_completion_time ≤ deadline`.

### 7.2 Utility function — Option A (net-benefit, no division, no epsilon)

```
U(candidate) = (1 + FaultUrgency_norm + DeadlineUrgency_norm) × delivered_value
               − (EnergyCost_norm + ThermalRisk_norm + ResourcePressure_norm)
```

All `_norm` terms are in `[0,1]`, normalized against their own ceiling as in the master reference doc §5.7/7.2. `delivered_value = mission_value × value_fraction_permille / 1000`. Only `FULL`, `THROTTLED`, `DEGRADED` candidates that survived the Gate are ranked. Ties: prefer the higher-fidelity mode (`FULL` > `THROTTLED` > `DEGRADED`).

### 7.3 Fallback rule (when no executing mode survives the Gate)

- If `criticality_class == 0`: **no fallback allowed.** Trigger the emergency safe mode (§12).
- Else, prefer `DEFERRED` over `REJECTED` **iff** both: (a) the deadline still permits a future execution window, and (b) the Ledger can hold a pending reservation for the task without violating any predicate.
- Otherwise select `REJECTED`.
- `REJECTED` outcomes are logged and counted separately from deadline misses (§14) — a reject-by-design is not a scheduling failure.

## 8. Prediction Spec (Holt's linear trend method)

Per channel (SOC, per-subsystem temp, current draw):

```
L_t = α·x_t + (1−α)·(L_{t−1} + T_{t−1})
T_t = β·(L_t − L_{t−1}) + (1−β)·T_{t−1}
forecast(n steps ahead) = L_t + n·T_t
```

- Default `α = 0.3`, `β = 0.1` (tune empirically in Phase 3; record the chosen values here once fixed).
- **Re-seed** `T_t = 0` at eclipse entry/exit (terminator crossing), since the orbit-phase correction supplies the known step change and the trend term shouldn't fight it.
- Horizon `H` = task's predicted execution duration + a fixed lookahead margin (value TBD in Phase 3, record here once fixed).
- O(1) update per sample, no matrix ops. This is the MVP-committed mechanism; do not substitute a physics-based propagator (that is a documented future embodiment, out of scope).
- Predicted-vs-actual error must be logged (store the prediction, compare when horizon elapses) to support the with/without-prediction A/B in Phase 3.

## 9. Revalidation Spec

Runs every tick against every open commitment.

- **Trigger**: new telemetry deviates from the commitment's assumption snapshot beyond a stated tolerance on any predicate-relevant quantity (SOC, thermal, protected-residual), **or** a fault-escalation event is received for that task's subsystem. Tolerances are per-channel constants (TBD in Phase 4, record here once fixed).
- On trigger: mark the commitment invalid, release its Ledger reservation, record an `InvalidationRecord` with `cause_type = OBSERVED_DEVIATION` or `OBSERVED_FAULT`, and re-run steps 3–7 of §6 for that task.
- **Restoration — observed cause**: restorable only after `K` consecutive ticks (default `K = 3`, tune in Phase 4) where the condition is confirmed clear, **and** the restored candidate must re-enter at step 3 and pass the Gate again. Never flip the mode back without a Gate re-pass.
- **Restoration — predicted cause**: restorable once `forecast_horizon_end` has elapsed without the predicate actually failing. If telemetry instead confirms the predicted risk before the horizon elapses, **upgrade** `cause_type` to `OBSERVED_DEVIATION`/`OBSERVED_FAULT` (set `upgraded_from_predicted = true`) and apply the observed-cause rule from then on.
- Add a minimum dwell time after any restoration before the same task can be invalidated again on the same cause, to prevent flapping (value TBD, record here once fixed).

## 10. Ledger Semantics

Three resource-type behaviors — implement each dimension according to its type, not uniformly:

| Type | Dimensions | Update rule |
|---|---|---|
| **Rate** | CPU, COMM | Renews every tick/window. `committed` is *derived* each tick as the sum of active commitments' demand (never mutated/reset by hand). `free = capacity − protected_reserve − committed`. Comms capacity = 0 during blackout, which naturally excludes comm-dependent tasks. |
| **Stock** | MEMORY, STORAGE | Persists across ticks until the task completes, its data is downlinked, or it's explicitly released. `committed` only changes on an explicit allocate/release event, never automatically per tick. |
| **Budget** | ENERGY | A horizon budget, recomputed each tick: `(SOC_pred − floor) × capacity + predicted_harvest(H) − planned_draws_within(H)`. Driven by the battery/solar model's own state update, not a commit/release pattern. |

**Protected-reserve access rule (hard rule, no exceptions):** only `criticality_class == 0` tasks (attitude control, FDIR) may draw against `protected_reserve`. Every other task is restricted to `free`, regardless of utility score. This is enforced in Gate predicate 3 (§7.1), not just documented.

**Self-cost**: a fixed, always-reserved line item on `CPU` and `ENERGY`, sized to the worst-case measured cost of one full decision cycle.
- CPU: **measured** via `CLOCK_PROCESS_CPUTIME_ID`, capped at p99.9 (or max) + margin.
- Energy: **modeled**, `E_self = measured_CPU_seconds × assumed_active_power_W` (joules), where `assumed_active_power_W` is a stated, documented constant (e.g. from a representative flight-processor datasheet) — never treated as measured. Label every energy figure downstream (§14 metrics, §15 risks) as modeled.
- Take a **provisional** measurement after Phase 2 (no prediction yet) — mark it non-binding. Take the **final** measurement and hard cap after Phase 3 (prediction included).

## 11. Real-Time / Governor Spec

- Single injectable clock (`platform_clock`): virtual clock (deterministic, fast — used for tests/metrics runs) and real clock with a configurable time-scale factor (for the live demo, since 1 real orbit ≈ 90 min).
- **Governor loop**: `poll()` on the pipe fd from the forked simulator, with a timeout computed from the next absolute tick time on `CLOCK_MONOTONIC` (recompute each iteration — do not use a fixed relative sleep, to avoid drift). A fault-escalation message arriving on the pipe triggers an immediate cycle, independent of the tick.
- **Concurrency model**: no threads. The simulator runs as a separate process (`fork()`), writing fixed-size messages ≤ `PIPE_BUF` to a `pipe()` (atomic writes, no locking needed). Message types: telemetry frame, fault-escalation event.
- **Run-to-completion with shadow state**: each cycle mutates a copy of the Ledger/plan and swaps in only at the end. If the cycle exceeds its self-cost budget (checked at per-task boundaries within the cycle), discard the shadow and keep the last known-safe plan (watchdog-style, not best-effort continuation).
- **No heap allocation anywhere in the decision path** (steps 1–7 of §6). Preallocate everything at startup.
- **Real-time metrics** emitted per tick: tick jitter (actual vs. intended wake time), cycle time, self-cost as % of period; summarized per run (p50/p99/max).

## 12. Edge Cases (must be handled, per module)

| Case | Handling |
|---|---|
| Compound resource exhaustion beyond protected reserves | Emergency safe mode: a static, minimal, known-safe schedule. Triggered when even the protected reservation for a class-0 task can't be met. Must not leave the system with "every candidate rejected and no decision." |
| Scheduler self-overrun | Watchdog fallback (§11): discard the in-progress cycle, keep the last known-safe committed plan. |
| Stale/missing telemetry | Prediction Engine and Gate fall back to a conservative worst-case assumption for that channel — never silently reuse a stale value indefinitely, never treat missing as zero-risk. |
| Deadline/clock sync | All deadline fields are defined relative to the same clock source the Telemetry layer uses for its tick (`platform_clock`). No independent clocks in the MVP (host-PC simulation has one clock by construction — flag this as a known simplification vs. real hardware, do not attempt to fix it in the MVP). |
| Restoration flapping | Prevented by hysteresis (§9) + minimum dwell time. |

## 13. Scenario Catalog

Line-oriented text format (no JSON/YAML library). One directive per line, `KEY value value ...`. Required scenarios (golden expected logs live in `/golden`, one per scenario):

### Lexical rules

- UTF-8/ASCII text, one directive per physical line.
- Tokens are separated by spaces or tabs.
- Blank lines and lines whose first non-whitespace character is `#` are ignored.
- Inline comments begin with `#` after a complete token.
- Keywords and enum tokens are uppercase and case-sensitive.
- Integers are base-10 except masks, which accept base-10 or a `0x` hexadecimal prefix.
- Signed values use an optional leading `-`; all time values are unsigned nanoseconds.
- Unknown keywords, missing tokens, extra tokens, malformed numbers, duplicate singleton directives, and capacity overflow are errors with a 1-based line number.

### Directive-keyword table

| Keyword | Arguments | Cardinality | Meaning and validation |
|---|---|---:|---|
| `SCENARIO` | `<name>` | Exactly 1 | Single token identifier used in diagnostics; `[A-Za-z0-9_-]+`. |
| `DURATION_NS` | `<duration_ns>` | Exactly 1 | Inclusive simulation end; must be greater than zero. |
| `STEP_NS` | `<step_ns>` | Exactly 1 | Regular frame interval; must be greater than zero. |
| `ORBIT` | `<epoch_ns> <period_ns> <eclipse_offset_ns> <eclipse_duration_ns>` | 0 or 1 | Repeating orbit model. `period_ns > 0`, offsets are within the period, eclipse duration is positive, and `offset + duration <= period`. At time `t`, eclipse is true when `(t - epoch) mod period` is in the configured eclipse interval; before epoch it retains initial state. |
| `INITIAL` | `<channel> [<index>] <value> <timestamp_ns> <quality>` | Once per channel | Defines every initial channel. `<index>` is present only for `TEMP_MC`. Because simulation begins at zero on the shared clock, the initial timestamp must be `0`. |
| `UPDATE` | `<event_time_ns> <channel> [<index>] <value> <sample_timestamp_ns> <quality>` | 0..256 | Replaces one channel at `event_time_ns`. Events must fall within duration. `<index>` is present only for `TEMP_MC`. An `ECLIPSE` update is invalid when `ORBIT` is present. |
| `FAULT` | `<event_time_ns> <set_mask> <clear_mask>` | 0..64 | Atomically updates fault flags as `(old | set_mask) & ~clear_mask`; masks must fit `uint32_t`, may not overlap, and event time must be within duration. The resulting fault channel timestamp is the event time with quality `OK`. |
| `TASK` | `<task_id> <period_ns> <deadline_ns> <nominal_exec_ns> <criticality_class> <mission_value>` | 0..32 | Declares task-level fields. IDs are unique; times are positive; mission value is `0..1000`. |
| `TASK_MODE` | `<task_id> <mode> <energy_cost_mJ> <thermal_effect_mC> <value_fraction_permille> <cpu_pct> <mem_KB> <storage_KB> <sensor_mask> <comm_pct>` | Exactly 3 per task | Declares `FULL`, `THROTTLED`, or `DEGRADED`; each appears once. Fractions are `0..1000`; CPU and comm are `0..100`; masks fit `uint32_t`; stock demands and energy are non-negative. |

The valid `<channel>` tokens are:

| Token | Index | Value domain |
|---|---:|---|
| `BATTERY_SOC_MV_PCT` | No | Non-negative signed 64-bit integer |
| `CHARGE_RATE_MA` | No | Signed 64-bit integer |
| `SOLAR_INPUT_MW` | No | Non-negative signed 64-bit integer |
| `ECLIPSE` | No | `0` or `1` |
| `TEMP_MC` | Yes, `0..3` | Signed 64-bit integer |
| `CURRENT_DRAW_MA` | No | Signed 64-bit integer |
| `FAULT_FLAGS` | No | `0..UINT32_MAX`, decimal or hexadecimal |
| `COMM_WINDOW_OPEN` | No | `0` or `1` |
| `CPU_AVAIL_PCT` | No | `0..100` |
| `MEM_AVAIL_KB` | No | `0..INT64_MAX` |
| `STORAGE_AVAIL_KB` | No | `0..INT64_MAX` |
| `SENSOR_AVAIL_MASK` | No | `0..UINT32_MAX`, decimal or hexadecimal |

Valid quality tokens are `OK`, `STALE`, and `MISSING`. A `MISSING` channel still carries a syntactically valid placeholder value and timestamp so the fixed frame representation remains total; later phases decide how conservative policy treats it.

A scenario is valid only when it has `SCENARIO`, `DURATION_NS`, `STEP_NS`, and exactly one initial record for every scalar and every indexed temperature channel. Tasks are optional, but each declared task must have all three executing-mode profiles. Each `TASK_MODE` must reference an earlier `TASK`. Directives otherwise may appear in any order. At a shared event time, the simulator applies orbit transitions, then updates in file order, then faults in file order, and emits one frame. Frames occur at time zero, regular steps, explicit event times, orbit transitions, and the inclusive end time.

1. Normal orbit
2. Low-SOC eclipse
3. High thermal load
4. Sudden fault
5. Communication blackout
6. Competing payload tasks
7. **Compound battery-thermal-fault stress** (primary demo scenario — reaction-wheel fault during eclipse, low battery, approaching thermal limit, queued payload tasks)
8. False-alarm / restoration scenario (a predicted risk that telemetry later shows did not materialize)

## 14. Metrics & Baseline Comparison

**Metrics** (per run): deadline misses, safety-invariant violations, protected-resource violations, rejected-by-design count (tracked **separately** from deadline misses — §7.3), useful mission work completed, energy survival time, recovery latency, task completion ratio, number of replans (invalidation/restoration events), scheduler overhead (measured CPU + modeled energy, vs. the self-cost cap), tick jitter, prediction error (predicted vs. actual, from §8).

**Baseline comparators** — implemented as feature-flag configurations of the *same* engine, not separate schedulers:

| Baseline | Flags |
|---|---|
| Fixed-priority/EDF | Gate off, single-mode (FULL only), no ledger cross-check |
| GomX-3-style | Binary modes only (FULL/DEFERRED), battery+solar/eclipse inputs only, no thermal/fault input, no event-triggered re-plan (tick-only) |
| Ranjbar-style | Fault+criticality+thermal inputs only, no battery/solar model |
| SAGE-RT (full) | Everything on |

Run all four against all 8 scenarios; report side by side, with particular attention to safety-violation rate and critical-task completion rate under scenario 7.

## 15. Novelty Traceability Matrix

| ID | Feature | Module | Test/Evidence |
|---|---|---|---|
| N1 | Gate-then-rank ordering | Gate → Utility | Unit test: high-utility unsafe candidate never selected |
| N2 | Cross-resource ledger, joint feasibility | Ledger | Test: passes on battery, fails on comms → rejected |
| N3 | Structural protected reservation (attitude/FDIR) | Ledger + Gate predicate 3 | Metric: protected-resource violations = 0 under scenario 7 |
| N4 | Revalidation with reversal (restoration) | Revalidation | Scenario 8, restore event in log |
| N5 | Five graduated modes | Mode Generator | Mode-distribution vs. GomX-3-style baseline |
| N6 | Self-cost as protected ledger line | Ledger | Overhead metric vs. cap |
| N7 | Fixed-period + event-interrupt cadence | Governor | Recovery-latency vs. GomX-3-style baseline |
| N8 | Live thermal + fault as scheduling inputs | Telemetry → Gate/Utility | Ablation: remove inputs, show violations rise |
| N9 | Short-horizon prediction feeding the gate | Prediction | With/without-prediction A/B |
| N10 | Emergency fallback mode | Governor | Scenario forcing protected-capacity breach |

**Rule for agents: never remove, weaken, or "simplify away" any N1–N10 behavior without flagging it in §18 first.**

## 16. Phase Plan

| Phase / Prompt | Scope | Exit Criteria | Status |
|---|---|---|---|
| 1 | Foundation & data layer | Deterministic scenario→telemetry/task generation; `test.h`; Makefile; golden-file harness; sanitizers clean | ✅ Done |
| 2 | Decision core, static | Gate + Ledger standalone-tested; Mode Generator + Utility (Option A); fallback rule; provisional self-cost | ⬜ Not started |
| 3 | Prediction + governor | Holt's method; poll-based governor; fork/pipe sim; shadow-state overrun handling; final self-cost cap; A/B result | ⬜ Not started |
| 4 | Revalidation + edge cases | Typed invalidation, hysteresis, gate-repassing restoration; emergency safe mode; stale-telemetry handling | ⬜ Not started |
| 5 | Execution, scenarios, baselines, evidence | All 8 scenarios; 4 baseline configs; metrics report; novelty traceability check | ⬜ Not started |

Agents update only the Status column (and the Decision Log, §18) — never rewrite scope or exit criteria without human approval.

## 17. Coding Standards

- **Language**: C11. `gcc`/`clang`, flags: `-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror`.
- **Build**: plain Makefile. No CMake, no external build system.
- **Dependencies**: libc only. No third-party libraries (no JSON, no test framework, no plotting).
- **Portability**: POSIX only. No Linux-only APIs (`epoll`/`timerfd`/`eventfd` are forbidden — use `poll()` + `pipe()` + `CLOCK_MONOTONIC`, per §11).
- **Memory**: no heap allocation in the decision path (§6 steps 1–7). Preallocate at startup.
- **Concurrency**: no threads. `fork()` + `pipe()` only.
- **Testing**: `/test/test.h` (~30 lines: `RUN_TEST`, `ASSERT_TRUE`, `ASSERT_EQ_INT`, non-zero exit on any failure) + golden-file tests (run scenario, diff log against `/golden/<scenario>.expected`). No Unity, no other test framework.
- **Sanitizers**: ASan/UBSan build target required; must be clean before a phase is marked done.
- **Optional hardening** (off by default, behind a build/run flag): `sched_setscheduler(SCHED_FIFO)`, `mlockall`. Must degrade gracefully without root.

## 18. Decision Log & Open Questions

- 2026-09-22: Utility function fixed to **Option A** (net-benefit, no division).
- 2026-09-22: Tech stack fixed to portable POSIX (`poll`, `fork`+`pipe`), no Unity, no CMake — see §17.
- 2026-09-24: Phase 1 uses the four named thermal subsystems in §5 (`BATTERY`, `BUS`, `PAYLOAD`, `ADCS`) and fixed capacities of 32 tasks, 256 updates, 64 faults, and 512 bytes per scenario line. Exceeding a capacity is a parse error; fixed storage keeps the simulator and future decision path heap-free.
- 2026-09-24: `src/main.c` is the stub entry point, following the canonical §4.1 tree. The Phase 1 request's “root main.c” wording is interpreted as the project entry point rather than a filesystem-root path.
- 2026-09-24: The §13 grammar is finalized. Initial source timestamps are zero because the shared simulated clock starts at zero; each `TASK_MODE` follows its `TASK`. Updates may carry their own source timestamp. Parser diagnostics include a 1-based line number.
- 2026-09-24: The in-process simulator emits one frame at the union of regular ticks, explicit updates and faults, orbit/eclipse transitions, time zero, and the inclusive end. Coincident changes apply in orbit → update → fault order before that single frame. This provides deterministic replay without adding policy behavior.
- 2026-09-24: The host-PC MVP uses one injected clock for telemetry and task deadlines, as assumed in §12; real hardware clock synchronization remains outside the MVP.
- **Open**: Holt's `α`, `β` values — finalize empirically in Phase 3, record here.
- **Open**: prediction horizon margin — finalize in Phase 3, record here.
- **Open**: revalidation deviation tolerances per channel, and minimum dwell time — finalize in Phase 4, record here.

## 19. Repository & Disclosure Notes

Repository must stay **private**. Per the master reference document's §14.5, public disclosure of the detailed architecture before any filing decision can affect novelty. Be aware that Copilot and Antigravity may send code/PRD content to cloud services — confirm your institution's policy before pasting the full architecture into any external tool, and do not publish the repo or a write-up describing it until this has been discussed with your instructor or a patent professional. This PRD is an engineering document, not legal advice.
