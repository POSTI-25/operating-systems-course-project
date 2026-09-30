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

The requested dominance criterion is **not met**. On compound stress, SAGE-RT has fewer audited safety violations than Fixed/EDF and GomX style but ties Ranjbar style at zero. All four variants complete 0/1 critical tasks because the one class-0 task has a 20 ns execution time and the deliberately compact scenario lasts 12 ns; the t=3 ADCS fault pauses it and the t=11 CPU shortfall prevents safe restoration. The fixture and thresholds were not changed to make any variant win. Phase 5 completion remains open pending the user's decision about this unmet comparison criterion.

Traceability evidence is verified separately by `traceability_check` in Task 12. The repository must remain private under PRD §19.
