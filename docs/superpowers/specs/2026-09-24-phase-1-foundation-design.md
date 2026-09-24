# SAGE-RT Phase 1 Foundation & Data Layer Design

## Purpose and scope

Phase 1 establishes the deterministic, allocation-free data and simulation boundary that later SAGE-RT phases will consume. It implements only the clock abstraction, telemetry and task contracts, scenario parser, in-process simulator, tests, golden harness, build rules, and required PRD updates.

It does not implement the Ledger, Gate, candidate or mode generation, Utility, Prediction, Revalidation, Governor, IPC, execution adapter behavior, or logging behavior. The empty `src/adapter` directory exists only to match the PRD layout.

## Resolved ambiguities

- `N_SUBSYSTEMS` is fixed at `4` for the MVP. Four indexed thermal channels give scenarios enough independent thermal state without making the fixed-capacity data structures large. Scenario files must provide indices `0` through `3`; any other index is invalid.
- The stub entry point is `src/main.c`, following the canonical tree in PRD §4.1. The phrase “root main.c stub” in the Phase 1 request is interpreted as the project’s main program stub, not a filesystem-root placement.
- All collections use compile-time capacities: `SAGE_MAX_TASKS = 32`, `SAGE_MAX_UPDATES = 256`, `SAGE_MAX_FAULTS = 64`, and `SAGE_SCENARIO_LINE_MAX = 512`. Exceeding a capacity is a parse error, never truncation.
- The simulator emits frames at the sorted union of time zero, regular sample times, explicit update times, fault times, orbit transition times, and the scenario end time. Duplicate times produce one frame after all changes at that time have been applied.
- These choices will also be recorded in PRD §18 during implementation.

## Repository layout

Phase 1 creates this tracked structure:

```text
src/
  platform/
    platform_clock.c
    platform_clock.h
  core/
    telemetry.c
    telemetry.h
    task_model.c
    task_model.h
  adapter/
    .gitkeep
  sim/
    scenario.c
    scenario.h
    simulator.c
    simulator.h
  main.c
scenarios/
  normal_orbit.scn
golden/
  normal_orbit.expected
test/
  test.h
  test_platform_clock.c
  test_telemetry.c
  test_task_model.c
  test_scenario.c
  test_simulator.c
  golden_runner.c
Makefile
PRD.md
```

No placeholder source files for later-phase modules are created.

## Platform clock

`platform_clock.h` exposes one caller-owned `PlatformClock` selected at startup with a `PlatformClockKind` value:

```c
typedef enum {
    PLATFORM_CLOCK_VIRTUAL = 0,
    PLATFORM_CLOCK_REAL = 1
} PlatformClockKind;

typedef struct {
    PlatformClockKind kind;
    uint64_t virtual_now_ns;
} PlatformClock;

bool platform_clock_init(PlatformClock *clock, PlatformClockKind kind,
                         uint64_t initial_ns);
bool platform_clock_now_ns(const PlatformClock *clock, uint64_t *now_ns);
bool platform_clock_advance_ns(PlatformClock *clock, uint64_t delta_ns);
```

Virtual time changes only through `platform_clock_advance_ns`. Advance rejects a real clock and integer overflow. Real time uses `clock_gettime(CLOCK_MONOTONIC, ...)`; `initial_ns` is ignored for the real backend. There is no wait, sleep, poll, or time-scale behavior in Phase 1.

## Telemetry contract

Every channel is represented by a value, its own source timestamp, and its own quality:

```c
typedef enum {
    TELEMETRY_QUALITY_OK = 0,
    TELEMETRY_QUALITY_STALE = 1,
    TELEMETRY_QUALITY_MISSING = 2
} TelemetryQuality;

typedef struct {
    int64_t value;
    uint64_t timestamp_ns;
    TelemetryQuality quality;
} TelemetryChannel;

typedef struct {
    TelemetryChannel battery_soc_mV_pct;
    TelemetryChannel charge_rate_mA;
    TelemetryChannel solar_input_mW;
    TelemetryChannel eclipse;
    TelemetryChannel temp_mC[N_SUBSYSTEMS];
    TelemetryChannel current_draw_mA;
    TelemetryChannel fault_flags;
    TelemetryChannel comm_window_open;
    TelemetryChannel cpu_avail_pct;
    TelemetryChannel mem_avail_KB;
    TelemetryChannel storage_avail_KB;
    TelemetryChannel sensor_avail_mask;
} TelemetryFrame;
```

An `int64_t` payload keeps the per-channel representation uniform while preserving signed thermal and electrical quantities. Boolean channels accept only `0` or `1`; percentage channels accept `0..100`; masks must fit `uint32_t`; storage and memory values must be non-negative. Validation belongs to the constructor/setter boundary rather than downstream policy.

The module exposes `telemetry_frame_init`, `telemetry_frame_set`, and `telemetry_frame_get`. A `TelemetryChannelId` identifies scalar fields; temperature access additionally takes a subsystem index. Callers own every frame and returned samples are copied, so no internal pointer lifetime crosses the API.

## Task contract

Only the three executing modes have resource profiles in Phase 1:

```c
typedef enum {
    TASK_MODE_FULL = 0,
    TASK_MODE_THROTTLED = 1,
    TASK_MODE_DEGRADED = 2,
    TASK_EXECUTING_MODE_COUNT = 3
} TaskExecutingMode;

typedef struct {
    uint16_t cpu_pct;
    uint32_t mem_KB;
    uint32_t storage_KB;
    uint32_t sensor_mask;
    uint16_t comm_pct;
} ResourceDemand;

typedef struct {
    uint64_t energy_cost_mJ;
    int32_t thermal_effect_mC;
    uint16_t value_fraction_permille;
    ResourceDemand resource_demand;
} TaskModeProfile;

typedef struct {
    uint32_t task_id;
    uint64_t period_ns;
    uint64_t deadline_ns;
    uint64_t nominal_exec_ns;
    uint8_t criticality_class;
    uint16_t mission_value;
    TaskModeProfile modes[TASK_EXECUTING_MODE_COUNT];
} Task;

typedef struct {
    Task items[SAGE_MAX_TASKS];
    size_t count;
} TaskTable;
```

The task loader is the scenario parser: it fills a caller-owned `TaskTable` from `TASK` and `TASK_MODE` records. It rejects duplicate task IDs, duplicate modes, missing modes, zero periods/deadlines/execution times, values outside their declared ranges, and profiles that reference an unknown task. `task_table_find` provides read-only lookup by ID.

## Scenario grammar

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

### Completeness and ordering

A scenario is valid only when it has `SCENARIO`, `DURATION_NS`, `STEP_NS`, and exactly one initial record for every scalar and every indexed temperature channel. Tasks are optional, but each declared task must have all three executing-mode profiles.

Singletons, initial channels, updates, faults, and task declarations may appear in any order. Each `TASK_MODE` must reference a `TASK` declared earlier in the file. The parser preserves file order among updates at the same event time. The simulator applies simultaneous changes in this order:

1. Apply an orbit/eclipse transition when the configured phase changes; update the eclipse channel timestamp to the transition time and quality to `OK`.
2. Apply explicit `UPDATE` records in file order.
3. Apply `FAULT` records in file order.
4. Emit one completed frame.

The parser never silently sorts or normalizes the source arrays. The simulator finds the next output time using bounded scans over fixed arrays; Phase 1 scenario sizes make this deterministic approach preferable to adding a mutable sorting subsystem.

## Simulator boundary

The simulator is caller-owned and exposes an iterator-like API:

```c
bool simulator_init(Simulator *sim, const Scenario *scenario,
                    PlatformClock *clock);
SimulatorResult simulator_next(Simulator *sim, TelemetryFrame *frame,
                               uint64_t *frame_time_ns);
```

`SimulatorResult` distinguishes `SIMULATOR_FRAME`, `SIMULATOR_DONE`, and `SIMULATOR_ERROR`. Initialization copies the initial frame but retains a read-only pointer to the caller-owned parsed scenario, whose lifetime must exceed the simulator’s. It sets a virtual clock to zero. `simulator_next` advances only that injected clock, applies all state changes due at the next output time, and copies the resulting frame to the caller.

This boundary can later sit behind a fixed-size pipe message without changing scenario parsing or frame production. Phase 1 does not call `fork`, `pipe`, `poll`, or sleep functions.

## Text output and golden harness

The golden runner emits one line per frame, in stable field order:

```text
FRAME t=<ns> BATTERY_SOC_MV_PCT=<value>@<timestamp>:<quality> ... TEMP_MC[0]=... FAULT_FLAGS=0x........@<timestamp>:<quality> ...
```

Every channel includes value, source timestamp, and quality. Masks use eight lowercase hexadecimal digits; all other numbers use decimal. No locale-sensitive or floating-point formatting is used.

`scenarios/normal_orbit.scn` describes a deterministic sun/eclipse cycle with a complete initial frame and at least one task with all three mode profiles. `golden/normal_orbit.expected` contains the exact emitted frame sequence. The Makefile runs the golden runner into a temporary file and uses `diff -u`; any difference is a test failure. Temporary output is removed through a portable shell trap.

## Error handling

Public functions return `bool`, an enum result, or a null pointer for expected failure. Scenario parsing additionally fills a caller-provided fixed-size diagnostic buffer with the line number and concise cause. Null pointers, invalid enum values, invalid indices, overflow, incomplete scenarios, and out-of-range fields are rejected.

No module prints diagnostics except test executables, `src/main.c`, and `golden_runner.c`. No module exits the process. This keeps core and simulation code usable unchanged across an IPC boundary.

## Testing strategy

Tests use the approximately 30-line `test/test.h` macros required by PRD §17. Each behavior is introduced test-first and observed failing before implementation.

- Clock tests cover virtual initialization, deterministic advance, overflow rejection, real monotonic reads, and rejecting virtual advance on a real clock.
- Telemetry tests cover complete construction, independent timestamps/quality, scalar and temperature queries, and validation failures.
- Task tests cover lookup and complete mode profiles through scenario loading, plus duplicate/missing/out-of-range rejection.
- Scenario tests cover complete parsing, comments and masks, malformed/unknown/extra tokens, missing initial channels, capacity limits, conflicting orbit/eclipse updates, and line-numbered diagnostics.
- Simulator tests cover regular ticks, non-tick updates, orbit transitions, fault set/clear behavior, simultaneous-event ordering, completion, and deterministic replay.
- The golden test proves the full path from scenario file through parser, virtual clock, simulator, formatter, and expected output.

`make` builds with `-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror`. `make test` builds and runs every unit test and the golden diff. `make sanitize` rebuilds and runs the same suite with AddressSanitizer and UndefinedBehaviorSanitizer enabled.

## PRD changes at completion

Implementation replaces the §13 grammar placeholder with the grammar and directive tables above, changes only the Phase 1 status cell in §16 to `✅ Done`, and adds dated §18 decisions for `N_SUBSYSTEMS`, `src/main.c`, fixed capacities, event emission/order, and the finalized grammar. Phase 1 is marked done only after fresh successful runs of `make`, `make test`, and `make sanitize`.
