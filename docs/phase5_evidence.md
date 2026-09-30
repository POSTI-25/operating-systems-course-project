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
| False alarm/restoration | safety 0, work 0, replans 0 | safety 4, work 900 | safety 1, work 0, replans 1 | safety 0, work 900 |

`safety` is the independently audited count of failing task-predicate observations. It uses all full invariants against actual frames for every variant, even when that variant skipped the corresponding Gate check. Higher completed work under Fixed/EDF or GomX sometimes coincides with a safety violation. `rejected_by_design` is a separate CSV field and is never counted as a deadline miss. Recovery latency is censored for fault runs without a corresponding transition. Completion ratio is task-based, not recurring jobs.

The refreshed host-timed matrix also records zero full-variant work on false alarm/restoration, whereas its deterministic decision golden demonstrates a restoration and completion. The per-pair startup calibration samples the initial decision state, not every later cycle; the measured cap and consequent replay outcome can vary across runs. This limitation is reported, not hidden by changing a scenario or cap threshold to obtain a favorable comparison.

The requested dominance criterion is **not met**. On compound stress, SAGE-RT has fewer audited safety violations than Fixed/EDF and GomX style but ties Ranjbar style at zero. All four variants complete 0/1 critical tasks because the one class-0 task has a 20 ns execution time and the deliberately compact scenario lasts 12 ns; the t=3 ADCS fault pauses it and the t=11 CPU shortfall prevents safe restoration. The fixture and thresholds were not changed to make any variant win. Phase 5 completion remains open pending the user's decision about this unmet comparison criterion.

Traceability evidence, checked by `build/traceability_check`:

| ID | Named end-to-end evidence |
|---|---|
| N1 | `test_utility::unsafe_maximum_value_never_reaches_ranking`; `low_soc_eclipse.decisions.expected` eliminates FULL on SOC and selects DEGRADED. |
| N2 | `test_gate::rejects_each_specific_predicate`; `communication_blackout.decisions.expected` eliminates the comm-dependent FULL mode while the battery passes. |
| N3 | `compound_stress` CSV: SAGE_FULL protected-resource violations 0 versus 9 for Fixed/EDF and GomX style. |
| N4 | `false_alarm_restoration.decisions.expected`: predicted-risk invalidation at t=10 and restoration at t=24 with ten units of execution remaining. |
| N5 | `test_mode_generator::profiles_generate_five_canonical_modes` and `test_governor::variant_policies_have_exact_rule_masks`; full selects THROTTLED under heat and DEGRADED under low SOC, modes the GomX mask disables. |
| N6 | `test_governor::calibration_sets_hard_cap_and_modeled_ledger_reserve`; `normal_orbit,SAGE_FULL` CSV records measured CPU time, calibrated cap, and modeled energy. The cap is per cycle; CSV CPU time is summed over the run. |
| N7 | `test_governor_run::variant_fault_cadence_runs_through_the_same_pipe`; `sudden_fault` CSV records full recovery latency 0 ns, while tick-only GomX is censored at 9 ns. |
| N8 | `high_thermal_load` CSV: full safety violations 0 versus GomX 1; `low_soc_eclipse` full 0 versus Ranjbar 2. The sudden-fault trace also shows immediate full invalidation. |
| N9 | `prediction_ab` smooth-trend A/B in PRD §18 (Holt absolute SOC error 234 versus persistence 270); `low_soc_eclipse.decisions.expected` shows predicted SOC gating, and false-alarm trace stores predicted risk horizon. |
| N10 | `compound_stress.decisions.expected` at t=12 shows class-0 Gate protected-residual failure and `EMERGENCY_SAFE_MODE`; `test_replay::compound_and_false_alarm_have_required_end_to_end_evidence` verifies the event sequence. |

The repository must remain private under PRD §19.
