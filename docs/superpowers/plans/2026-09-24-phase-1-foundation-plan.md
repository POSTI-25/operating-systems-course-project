# Phase 1 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the deterministic Phase 1 telemetry and task data layer, scenario generator, and verification harness.

**Architecture:** Fixed-capacity caller-owned structs connect parsing, clocks, and simulator iteration. A text formatter provides golden output. No decision logic is present.

**Tech Stack:** C11, POSIX libc, GNU Make, GCC, shell `diff`.

**Spec:** `docs/superpowers/specs/2026-09-24-phase-1-foundation-design.md`; PRD.md remains authoritative.

## Global Constraints

- Phase 1 only; no later-phase modules or behavior.
- `-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror`.
- No heap allocation in telemetry, task, or simulator paths.
- Scenario grammar uses line directives and libc parsing only.
- Complete initial frame, three profiles per task, deterministic iterator.
- TDD for each production module and fresh whole-suite verification.

## Review Focus

- Reject malformed and overflowing integer tokens without silent truncation.
- Preserve per-channel source timestamps as simulated time advances.
- Emit one frame at coincident tick, update, fault, or orbit events.
- Reject incomplete task profiles and incomplete initial telemetry.
- Keep output and replay deterministic for the same scenario.

---

### Task 1: Clock

**Files:** `src/platform/platform_clock.[ch]`, `test/test.h`, `test/test_platform_clock.c`.

**Interfaces:** Produces `platform_clock_init`, `platform_clock_now_ns`, `platform_clock_advance_ns` per spec.

- [ ] Write tests for virtual advance, overflow, and real-clock constraints.
- [ ] Compile/run tests; verify failure due to missing implementation.
- [ ] Implement clock functions, then rerun tests and verify pass.

### Task 2: Telemetry

**Files:** `src/core/telemetry.[ch]`, `test/test_telemetry.c`.

**Interfaces:** Produces frame init, set, and get with per-channel metadata.

- [ ] Write tests for channel independence, validation, and indexed temperatures.
- [ ] Compile/run tests; verify failure due to missing implementation.
- [ ] Implement telemetry and rerun all current tests.

### Task 3: Task table

**Files:** `src/core/task_model.[ch]`, `test/test_task_model.c`.

**Interfaces:** Produces fixed-capacity task table and read-only lookup; parser fills it in Task 4.

- [ ] Write tests for task/profile insertion and lookup, duplicates and bounds.
- [ ] Compile/run tests; verify failure due to missing implementation.
- [ ] Implement task table and rerun all current tests.

### Task 4: Scenario parser

**Files:** `src/sim/scenario.[ch]`, `test/test_scenario.c`, `PRD.md` §5 and §13.

**Interfaces:** Produces `Scenario` and `scenario_load_file` for the iterator.

- [ ] Write parser tests for complete scenario, invalid directives, incomplete input, and limits.
- [ ] Compile/run tests; verify failure due to missing implementation.
- [ ] Implement parser and rerun all current tests.
- [ ] Copy both grammar tables verbatim into PRD §13; add named subsystem enum in §5.

### Task 5: Simulator

**Files:** `src/sim/simulator.[ch]`, `test/test_simulator.c`.

**Interfaces:** Consumes `Scenario` and `PlatformClock`; produces iterator frames and times.

- [ ] Write tests for ticks, updates, orbit, faults, ordering, and replay.
- [ ] Compile/run tests; verify failure due to missing implementation.
- [ ] Implement iterator and rerun all current tests.

### Task 6: Golden harness and build

**Files:** `src/main.c`, `test/golden_runner.c`, `scenarios/normal_orbit.scn`, `golden/normal_orbit.expected`, `Makefile`, `src/adapter/.gitkeep`, `PRD.md` §16 and §18.

**Interfaces:** Consumes parser and iterator; emits a stable text line per frame.

- [ ] Write and verify one normal-orbit golden fixture against the iterator.
- [ ] Add formatter, runner, Makefile, and exact directory skeleton.
- [ ] Run sanitizer build and suite, plain build, then `make test`.
- [ ] Record §18 decisions and set Phase 1 status only after all checks pass.
