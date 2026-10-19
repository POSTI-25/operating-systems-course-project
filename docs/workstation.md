# SAGE-RT Decision Workstation

The first frontend release runs reference replays through the existing C engine and displays their actual decision records. It adds no scheduling policy. It supports all four variant masks, the scenario catalog, task-state timelines, Gate verdicts, selected utility, Ledger changes, observed cycle telemetry, stored commitment assumptions, final execution settlement, and capture export. The comparison workspace reads the checked-in Phase 5 CSV.

## Visual decision walkthrough

Inspect opens a guided explanation of each captured cycle. **Play walkthrough** advances through six explanations before moving to the next cycle: observed inputs, forecast/risk evidence, Gate filtering, mode selection, resource accounting, and task state. Click a step directly, use **Next explanation**, or use the cycle arrows and event bookmarks. Manual navigation pauses playback. Playback speed changes the presentation pace, not simulated execution.

Input tiles highlight changes since the preceding observed cycle, including source timestamps and quality. Gate cards show actual candidate verdicts and first failures. Selection shows only the emitted mode and selected utility. Resource bars compare exact before/after Ledger snapshots; sensor squares represent mask bits. Task lanes group all tasks by their recorded state, with execution progress preserved across pauses. The task selector synchronizes the explanation, remaining-work chart, and resource view.

An event navigator links input changes and task transitions. The battery plot shows recorded observations and the protected SOC floor; amber identifies the selected task's recorded predicted-risk window. The remaining-work plot stays flat while work is paused and slopes down during execution. Faint lines show later results in the same capture, not forecasts. The scheduling map uses elapsed simulation time for interval widths; the original per-cycle lifecycle and raw tables remain under **Detailed decision evidence**.

These are explanatory views of published records, not instrumentation of individual C calls. They do not invent per-candidate forecast curves, missing utility scores, execution-thread assignments, or unpublished intermediate plans. Forecast assumptions are labeled with their commitment time and can be older than the selected cycle. Equal-time fault/tick cycles remain separate in the event navigator. Final settlement has no fabricated admission, forecast, or Ledger snapshot.

## Build and launch on this Windows workspace

In PowerShell at the project root, build the new observation executable in WSL Ubuntu:

```powershell
wsl -d Ubuntu -- bash -lc "cd '/mnt/d/jenevi/FALL SEM 26-27/OS/project' && make workstation"
npm.cmd install
npm.cmd run build
npm.cmd start
```

Open **http://127.0.0.1:4317** in your browser. Keep the terminal running; Ctrl+C stops the local server. Node.js 22.19 or a compatible newer LTS release is required for this frontend toolchain. On Windows the bridge launches the executable through WSL Ubuntu using argument arrays, without a shell command assembled from browser input. On Linux, build with `make workstation` and use `npm install`, `npm run build`, and `npm start` directly.

For frontend development, `npm.cmd run dev` runs the local API and Vite together. Open **http://127.0.0.1:5173**. The C build continues to use GNU Make; npm builds only the workstation.

## Professor demonstration

1. In **Configure**, choose **Low-SOC eclipse**, keep **SAGE-RT full**, and generate a reference run. Select **Safety Gate** to see FULL/THROTTLED eliminated on SOC and DEGRADED survive. **Choose a mode** shows the selection; **Account resources** shows its reservations. The Completion bookmark and **Task state** show 360 delivered value.
2. Choose **False-alarm restoration** and run again. Use **Predicted risk**, **Restoration**, and **Completion** bookmarks: t=10 pauses with 10 ns remaining, t=24 restores FULL, and t=34 completes with 900 value. Follow the flat remaining-work plot through the pause and the resumed slope afterward. **Play walkthrough** explains each cycle before advancing; pausing affects only presentation. Expand **Detailed decision evidence** for the original tables and raw records.
3. Choose **Compound stress**. Inspect the fault cycle at t=3 and CPU shedding at t=12. This fixture demonstrates emergency behavior, not critical completion; it ends before the 20 ns critical task can finish.
4. Open **Compare & verify**. Discuss the safety/work tradeoff and the accepted lack of strict dominance. Censored recovery is displayed as a lower bound; the sudden-fault bound for tick-only baselines is 1 ns, not 9 ns.
5. Export the JSON capture or raw trace for review. Every capture includes the exact scenario source and SHA-256 hashes of both the scenario and observation executable. A local copy is also written under ignored `output/ui-runs/`.

## Observation contract

`tools/frontend_runner.c` calls the existing C parser, `replay_run`, Governor accessors and Logger. It emits grouped cycle records, the actual telemetry frame supplied to each cycle, decision records, stored commitment assumptions, and execution snapshots. After replay settles execution at the scenario end, it emits a separate final snapshot; no artificial decision or admission cycle is introduced. Frontend and bridge preserve 64-bit time/resource integers as strings; the browser uses BigInt where needed.

Reference replay uses zero governor self-cost, a nonbinding CPU cap, and zero configured task protected reserves. These settings are visible in the application. It is a reproducible trace, not a calibrated timing measurement. The same engine supports all four variants; approximate baselines are feature ablations. Candidate records expose the first failing Gate predicate and selected utility score, not all predicate evaluations or scores for every candidate.

The table reads `docs/phase5_metrics.csv`, whose runs used calibrated self-cost. It is deliberately separate from reference traces. Telemetry is scripted, and execution does not alter battery SOC. Source quality and timestamps are shown as emitted; the workstation does not invent sensor freshness thresholds.

## Verification

```powershell
npm.cmd run test:ui
npm.cmd run test:engine
npm.cmd run build
npm.cmd run test:e2e
```

Browser tests require Playwright Chromium (`npx.cmd playwright install chromium`). Existing core checks remain `make test` and `make sanitize` in WSL. The observation executable also has a `make build/san_frontend_runner` target for ASan/UBSan. The comparison view does not claim these checks ran in the browser session.

## Remaining roadmap

Live SSE streaming and process cancellation, validated edits to separate experiment copies, saved-capture import, fresh report generation, browser-triggered verification and richer prediction exports remain subsequent milestones. This release does not offer runtime fault injection, engine pause/resume, a scheduling implementation in JavaScript, or simulated live metrics.
