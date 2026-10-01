# SAGE-RT: run and inspect the MVP

SAGE-RT is a host-PC demonstration of how a spacecraft can choose work without spending resources it needs to stay safe. It watches simulated battery, heat, faults, and other limits; chooses how much of each queued task to run; and can pause a choice when its safety assumptions stop holding. Essential attitude-control work gets protected resources. The point is to show safe, revocable scheduling when several constraints go bad together—not to control flight hardware.

## Build and check

Use a POSIX environment with a C11 compiler (`gcc` or `clang`), GNU `make`, and standard `diff`, `grep`, and `mktemp` commands. There are no third-party libraries. On Windows, use WSL Ubuntu, not native PowerShell, for the build and demo. From PowerShell, enter WSL:

```powershell
wsl -d Ubuntu
```

Then, in the POSIX shell, go to your checkout. For this workspace the path is:

```sh
cd '/mnt/d/jenevi/FALL SEM 26-27/OS/project'
make
make test
make sanitize
```

For a checkout elsewhere, replace only the `cd` path. `make` builds the executable and test tools with the PRD's warning-as-error C11 flags; an up-to-date build may say `Nothing to be done`. `make test` runs unit tests, all eight decision goldens, N1–N10 traceability, the prediction A/B, and a short live smoke run. Expect A/B `scenario=...` lines and a final `SUMMARY`, with no diff output or failing assertion. `make sanitize` repeats the relevant tests under ASan/UBSan; it should exit without sanitizer diagnostics. An exit status other than zero is a failure even if earlier lines looked normal.

## Watch the live governor

From the repository root in WSL:

```sh
./build/sage_rt scenarios/compound_stress.scn --tick-ms 100
```

This forks the simulator and runs the full governor over a pipe. `--tick-ms 100` maps each scenario step to about 100 ms of real time; omitting it uses about one second per step. The small `compound_stress` fixture combines eclipse, low SOC, near-limit heat, an ADCS fault, queued payload work, and a protected-capacity shortfall. It demonstrates shedding and Emergency Safe Mode, **not** critical-task completion: the class-0 task needs 20 simulated ns, but the scenario ends at 12 ns.

- `SELF_COST` shows the measured CPU maximum, enforced per-cycle cap, reserved CPU share, and modeled energy cost.
- `TICK` shows simulated time, wall-clock jitter, cycle time, CPU time, and whether the shadow plan was discarded for an overrun.
- `DECISION` shows one task's candidates (`PASS` or `ELIMINATED/GATE_*`), selected mode or fallback reason, and Ledger before/after. `trigger=FAULT` is an immediate cycle between regular ticks.
- `invalidation=1:<cause>:...` records a paused/revoked commitment; `restoration=1` means it re-passed the Gate and resumed its remaining work. `reason=EMERGENCY_SAFE_MODE` means no new unsafe task is dispatched. This compound fixture shows invalidation and emergency mode, but not restoration.
- `SUMMARY` aggregates ticks, faults, overruns, cycle timing, jitter, and self-cost; `COMMIT` lists stored commitments and forecast assumptions.

To watch restoration instead, run `./build/sage_rt scenarios/false_alarm_restoration.scn --tick-ms 100` and look for `PREDICTED_RISK`, `restoration=1`, then `reason=COMPLETED`. Live timings and calibrated self-cost vary by host; the deterministic golden gives the reference sequence.

## Read or regenerate the evidence

`docs/phase5_metrics.csv` is the checked-in 32-row comparison (eight scenarios × four variants). `docs/phase5_evidence.md` explains the claims and the accepted non-dominance result; it is a reviewed narrative, not an automatically generated file. Inspect or regenerate the CSV with:

```sh
grep '^compound_stress,' docs/phase5_metrics.csv
./build/report_runner
make report
```

`report_runner` prints a new CSV to the terminal; `make report` overwrites the checked-in CSV. CPU-time and cap figures are host-dependent, so review a regenerated diff before committing it. `make test` checks the golden decision logs in `golden/` against deterministic replay; the live demo need not match their wall-clock timing.

CSV columns, in file order:

| Column | Meaning |
|---|---|
| `scenario` | Scenario fixture name. |
| `variant` | Engine configuration: `SAGE_FULL`, `FIXED_EDF`, `GOMX_STYLE`, or `RANJBAR_STYLE`. |
| `task_count` | One-shot tasks in the scenario. |
| `critical_task_count` | Class-0 tasks in that total. |
| `completed_count` | Tasks completed by scenario end. |
| `critical_completed_count` | Completed class-0 tasks. |
| `deadline_misses` | Due-by-end tasks not completed on time, excluding design rejections. |
| `rejected_by_design` | Tasks deliberately left `REJECTED`, counted separately from misses. |
| `mission_work_completed` | Sum of delivered value from completed tasks. |
| `completion_ratio` | `completed_count / task_count`; `NA` if there are no tasks. |
| `critical_completion_ratio` | `critical_completed_count / critical_task_count`; `NA` if no class-0 task exists. |
| `safety_violations` | Failed full-safety predicates observed while tasks run; several can occur at one task-instant. |
| `safety_task_instants` | Running-task observations audited for safety. |
| `safety_violation_rate` | Violations per audited task-instant, not a probability; it may exceed 1. |
| `protected_resource_violations` | Protected-residual failures, a subset of safety violations. |
| `energy_survival_time_ns` | Simulated time of the first SOC-floor breach, or scenario end if none occurs. |
| `energy_survival_censored` | `1` means no breach was observed by scenario end; `0` means one was observed. |
| `recovery_latency_ns` | Longest fault-to-plan-transition delay, or its end-of-run censoring bound. |
| `recovery_latency_censored` | `1` means at least one fault had no matching transition before the run ended. |
| `replan_count` | Invalidation, restoration, and Emergency Safe Mode entry transitions. |
| `cpu_time_ns` | Governor CPU time summed over the run. |
| `self_cost_cap_ns` | Calibrated CPU cap for **one** decision cycle, not the whole run. |
| `modeled_self_energy_mJ` | Run CPU time converted using the assumed 2.0 W active power. |
| `tick_jitter_ns` | `NA` in this virtual-replay CSV; the live `TICK` lines carry measured jitter. |
| `prediction_abs_error` | Sum of absolute matched SOC, draw, and temperature errors (mixed units); `NA` without predictions. |

The scripted SOC is exogenous: `energy_survival_time_ns` is not proof that one scheduler saved battery. A censored recovery latency is a bound, not a completed recovery measurement.

## Show N1–N10 quickly

Run these from the repository root after `make`. The golden files are compact, deterministic decision traces; the CSV rows are the calibrated comparison. `make test` also runs `build/traceability_check` across all ten items.

| Claim | Fastest check and what to point out |
|---|---|
| N1 — Gate before rank | `grep 'FULL/ELIMINATED/GATE_SOC_FLOOR' golden/low_soc_eclipse.decisions.expected` — the same line selects safe DEGRADED, not unsafe FULL. |
| N2 — Joint Ledger feasibility | `grep 'GATE_PROTECTED_RESIDUAL' golden/communication_blackout.decisions.expected` — comm demand eliminates FULL despite sufficient battery. |
| N3 — Protected reserve | `grep '^compound_stress,' docs/phase5_metrics.csv` — `protected_resource_violations` is 0 for full versus 9 for Fixed/EDF and GomX; Ranjbar also has 0. |
| N4 — Reversible revalidation | `./build/decision_golden_runner scenarios/false_alarm_restoration.scn | grep -E 'PREDICTED_RISK|restoration=1|reason=COMPLETED'` — pause, Gate-approved FULL restoration, completion. |
| N5 — Graduated modes | `grep -h -oE 'selected=(THROTTLED|DEGRADED)' golden/high_thermal_load.decisions.expected golden/low_soc_eclipse.decisions.expected` — heat throttles, low SOC degrades. |
| N6 — Governor self-cost | `./build/sage_rt scenarios/normal_orbit.scn --tick-ms 100 | grep '^SELF_COST'` — measured maximum, cap, and protected reservation. |
| N7 — Event-interrupt cadence | `./build/sage_rt scenarios/sudden_fault.scn --tick-ms 100 | grep 'trigger=FAULT'` — the fault cycle occurs between ticks; the CSV's GomX latency is **censored** at 9 ns, versus full 0 ns. |
| N8 — Thermal and fault inputs | `grep -h -oE 'GATE_THERMAL_CEILING|trigger=FAULT' golden/high_thermal_load.decisions.expected golden/sudden_fault.decisions.expected` — heat gating and immediate fault response. The low-SOC/Ranjbar contrast is battery-model evidence, not N8 proof. |
| N9 — Forecast feeding Gate | `./build/prediction_ab scenarios/prediction_trend.scn` — Holt SOC absolute error 234 versus persistence 270; see `low_soc_eclipse` golden for SOC gating. |
| N10 — Emergency fallback | `grep 'sim_ns=12.*EMERGENCY_SAFE_MODE' golden/compound_stress.decisions.expected` — class-0 executing modes fail protected-residual checks, so dispatch stops. |

## Find the files

- `src/platform/`: clock, wait, and fork/pipe interfaces.
- `src/core/`: telemetry/task contracts and the prediction, Ledger, Gate, revalidation, and Governor logic.
- `src/sim/`: scenario parser and deterministic telemetry/fault generator.
- `src/adapter/`: simulated execution progress and decision-log formatting; `src/main.c` is the live CLI.
- `scenarios/`: editable `.scn` inputs; `golden/`: expected deterministic telemetry and decision traces.
- `test/`: unit, replay, report, and traceability checks; `Makefile`: build/test/report targets.
- `docs/`: comparison report and design history; `PRD.md`: authoritative contracts and scope.

Keep this repository private; do not publish or push it publicly before the disclosure decision described in PRD §19.
