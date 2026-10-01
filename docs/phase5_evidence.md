# Phase 5 comparison evidence

This is a 32-run virtual replay of the same engine with four input masks. The exact rows are in `phase5_metrics.csv`. CPU caps were calibrated per run with 64 full decision passes and a 10 ms real tick equivalent; nanosecond values depend on the host. Modeled self energy assumes 2.0 W. The source telemetry is scripted: energy survival time is exogenous and must not be read as an effect of scheduling. Virtual replay has no wall clock tick jitter, so `tick_jitter_ns` is `NA`. Prediction error is `NA` for the three nonpredictive baselines. A zero class-0 task denominator gives `NA` critical completion.

| Scenario | SAGE-RT | Fixed/EDF | GomX style | Ranjbar style |
|---|---|---|---|---|
| Normal orbit | safety 0, work 900 | safety 0, work 900 | safety 0, work 900 | safety 0, work 900 |
| Low-SOC eclipse | safety 0, work 360 | safety 2, work 900 | safety 0, work 0 | safety 2, work 900 |
| High thermal load | safety 0, work 630 | safety 1, work 900 | safety 1, work 900 | safety 0, work 630 |
| Sudden fault | safety 0, work 0, replans 1 | safety 0, work 0, replans 0 | safety 0, work 0, replans 0 | safety 0, work 0, replans 1 |
| Communication blackout | safety 0, work 360 | safety 1, work 900 | safety 1, work 900 | safety 0, work 360 |
| Competing payload tasks | safety 0, work 900 | safety 2, work 1500 | safety 2, work 1500 | safety 0, work 900 |
| Compound stress | safety 0, critical 0/1, replans 3 | safety 9, critical 0/1, replans 0 | safety 9, critical 0/1, replans 0 | safety 0, critical 0/1, replans 3 |
| False alarm/restoration | safety 0, work 900, replans 2 | safety 4, work 900 | safety 1, work 0, replans 1 | safety 0, work 900 |

`safety` is the independently audited count of failing task-predicate observations. It uses all full invariants against actual frames for every variant, even when that variant skipped the corresponding Gate check. Higher completed work under Fixed/EDF or GomX sometimes coincides with a safety violation. `rejected_by_design` is a separate CSV field and is never counted as a deadline miss. Recovery latency is censored for fault runs without a corresponding transition. Completion ratio is task-based, not recurring jobs.

The false-alarm fixture now gives one additional SOC admission window before its rebound; calibration and every other scenario are unchanged. Eight consecutive calibrated matrix reruns delivered full value (900) and two replans in all eight, versus six full-value and two degraded-value runs before the margin change. Its decision golden shows predicted-risk invalidation at t=10, FULL restoration at t=24, and completion at t=34. A forced t=8 overrun regression also restores into FULL and completes after delayed admission. These host-timed observations establish the measured result, not a guarantee against arbitrary future CPU jitter.

The requested strict dominance is **not observed** on compound stress. The class-0 ADCS task's 20 ns nominal execution time and 40 ns deadline both exceed the deliberately compact 12 ns scenario, so 0/1 critical completion for every variant is expected; this fixture proves protected shedding and Emergency Safe Mode, not completion. SAGE-RT and Ranjbar also tie in that row on protected violations (0 each), recovery latency (0 ns each), and replans (3 each). SAGE-RT's protected-violation advantage there is over Fixed/EDF and GomX (0 versus 9), while N7's latency contrast comes from `sudden_fault` (full 0 ns versus tick-only GomX censored at 9 ns). The comparison is accepted as a genuine result; no compound fixture or threshold was changed to force a win.

Traceability evidence, checked by `build/traceability_check`:

| ID | Named end-to-end evidence |
|---|---|
| N1 | `test_utility::unsafe_maximum_value_never_reaches_ranking`; `low_soc_eclipse.decisions.expected` eliminates FULL on SOC and selects DEGRADED. |
| N2 | `test_gate::rejects_each_specific_predicate`; `communication_blackout.decisions.expected` eliminates the comm-dependent FULL mode while the battery passes. |
| N3 | `compound_stress` CSV: SAGE_FULL protected-resource violations 0 versus 9 for Fixed/EDF and GomX style. |
| N4 | `false_alarm_restoration.decisions.expected`: predicted-risk invalidation at t=10 and FULL restoration at t=24 with ten units remaining; the calibrated CSV records 900 completed value and two replans, and the forced-overrun replay test restores to FULL after delayed admission. |
| N5 | `test_mode_generator::profiles_generate_five_canonical_modes` and `test_governor::variant_policies_have_exact_rule_masks`; full selects THROTTLED under heat and DEGRADED under low SOC, modes the GomX mask disables. |
| N6 | `test_governor::calibration_sets_hard_cap_and_modeled_ledger_reserve`; `normal_orbit,SAGE_FULL` CSV records measured CPU time, calibrated cap, and modeled energy. The cap is per cycle; CSV CPU time is summed over the run. |
| N7 | `test_governor_run::variant_fault_cadence_runs_through_the_same_pipe`; `sudden_fault` CSV records full recovery latency 0 ns, while tick-only GomX is censored at 9 ns. |
| N8 | `high_thermal_load` CSV: full safety violations 0 versus GomX 1; `sudden_fault.decisions.expected` shows immediate fault-triggered invalidation; the `low_soc_eclipse` full 0 versus Ranjbar 2 comparison is a separate battery-model ablation, not proof of N8. |
| N9 | `prediction_ab` smooth-trend A/B in PRD §18 (Holt absolute SOC error 234 versus persistence 270); `low_soc_eclipse.decisions.expected` shows predicted SOC gating, and false-alarm trace stores predicted risk horizon. |
| N10 | `compound_stress.decisions.expected` at t=12 shows class-0 Gate protected-residual failure and `EMERGENCY_SAFE_MODE`; `test_replay::compound_and_false_alarm_have_required_end_to_end_evidence` verifies the event sequence. |

The repository must remain private under PRD §19.
