# SAGE-RT Decision Workstation

The first frontend release runs reference replays through the existing C engine and displays their actual decision records. It adds no scheduling policy. It supports all four variant masks, the scenario catalog, task-state timelines, Gate verdicts, selected utility, Ledger changes, observed cycle telemetry, stored commitment assumptions, final execution settlement, and capture export. The comparison workspace reads the checked-in Phase 5 CSV.

## Compiler-style interactive walkthrough

The application now opens **Walkthrough**, adapted from the AEGIS frontend in the sibling Compiler Design project. Choose a situation and policy, then click **Run & explore**. After the first capture, changing a situation or policy generates a new capture automatically. The lab has the same journey/workspace/source arrangement: the stage journey is on the left, the selected artifact and its controls are in the center, and highlighted scenario source is on the right.

Eight teaching stages cover scenario inputs, observed conditions, configured execution profiles, candidate safety verdicts, selection/resource changes, chronological execution, revalidation/recovery, and final state. **Back**, **Next**, **Play**, restart and the per-stage scrubber reveal one source line, sensor frame, profile, verdict, resource field or execution record at a time. Play continues automatically through subsequent stages, skips stages without events, and stops after the final result. Play at the end restarts the whole walkthrough. Back and Next also cross stage boundaries; selecting a stage, artifact or scrubber position pauses playback. Click a profile or event to focus it; the corresponding source line scrolls into view. The source is read-only because the current bridge accepts checked-in fixtures. **Raw evidence** shows the actual object behind the current selection; download saves the complete capture.

The spacecraft state below the lab keeps the same job elements mounted while their recorded phase changes, so jobs move between waiting, working, paused, finished and rejected lanes. Resource reservoirs update from captured Ledger snapshots. **First pause**, **First restoration** and **First completion** jump to actual recorded events. A paused job's completed-work bar is preserved on restoration. Before execution is shown, the view is labeled as a scenario preview or the previous published state. Final settlement does not invent a final Ledger snapshot.

The teaching stages reorganize captured evidence; they are not additional scheduler phases or instrumentation of every C function call. Configured profiles are not claimed to be newly generated runtime events. Candidate verdicts are revealed in emitted order; resource fields reveal before/after snapshots, not unpublished intermediate plans. Source tokens are scenario fields, not compiler tokens. No JavaScript scheduling policy is added.

**Inspect** also defaults to the interactive lab for an existing capture. **Evidence & prior views** opens the earlier whiteboard, technical diagrams, space analogy and original controls. **Configure** retains the detailed fixture catalog, policies and lesson shortcuts. **Compare & verify** continues to read the saved report, separately from the interactive reference trace.

## Earlier evidence views

Under **Evidence & prior views**, **Whiteboard lesson** is a plain drawing that follows one job through one C-engine decision. The six explanations show the job's requirements and observed limits, risk and remaining work, mode comparisons and actual Gate verdicts, the selected option, before/after resource reservations, and the waiting/working/paused/finished state of every job. Each drawing asks one concrete question. **1 · Fit a smaller mode** in Configure selects Low-SOC eclipse: FULL requests 1,000 mJ, THROTTLED 700 mJ and DEGRADED 400 mJ against 500 mJ of captured free energy.

Mode comparisons focus on the constraint: energy, CPU, communication or observed thermal margin. All three requests use the same scale, with a red marker for the available amount and an explicit request/headroom comparison. The displayed Gate verdict is always the C result; observed comparisons do not reconstruct unexported candidate forecasts. The reference runner's thermal ceiling is 100°C, as configured in `tools/frontend_runner.c`. **Draw the allocation** animates exact Ledger before/after values without executing C again. CPU and energy appear first; a button reveals memory, storage, communication and sensor masks. Pausing can release some holds while retaining energy until a fresh SOC-budget update.

**2 · Share CPU between jobs** selects the competing-payload fixture: T1 reserves 60% CPU, leaving 40%, while T2 also asks for 60% in each mode. Select T2 in the task drawing to see why it waits. **3 · Pause and resume safely** selects False-alarm restoration: the risk drawing preserves 2 sim ns of completed work and 10 remaining, then restoration resumes that same unfinished portion. Per-job moment buttons jump to state changes and retain equal-time tick/fault distinctions. These shortcuts select fixture inputs and the full policy; they do not generate a run automatically.

**Mission map** opens with the basic jobs → constraints → scheduling-outcomes diagram. Expand **Data flow and OS details** for the complete program path, source references, analogy/system pairs and the illustrative pipe lab. Browser replay and concurrent CLI remain separate selectable paths: reference replay uses one C process and virtual time; the CLI uses `fork`, a kernel pipe, `poll`, fixed-size message assembly and `waitpid`. Neither view implies a real spacecraft radio link. The pipe lab demonstrates partial-read assembly; it is not measured IPC activity.

From Configure, **Explore the mission map** opens this guide. **Choose the recommended demo**, or **Set up the guided demo** in the map, selects False-alarm restoration and the full policy without running it automatically. The starting-condition briefing translates the fixture into battery, CPU, communication and fault inputs. Generate a reference run to get real C evidence.

The alternative **Technical view** retains the original six detailed diagrams. **Space analogy view** retains the illustrated mission scene with the Story/Systems audience control. All views synchronize to the selected task, cycle and explanation step. **Whole-run timeline and plots** starts collapsed in the whiteboard lesson; expand it after understanding an individual decision. **What do the clock, charts and tables mean?** gives a short reading guide for every evidence view.

**Play walkthrough** advances through six explanations before moving to the next cycle: observed inputs, forecast/risk evidence, Gate filtering, mode selection, resource accounting, and task state. Default pacing is **Read along (8s)** per explanation. The resource drawing automatically reveals the after state during playback; manual playback-off use offers an explicit draw/rewind button. Click a step directly, use **Next explanation**, or use the cycle arrows and event bookmarks. Manual cycle/task/step navigation pauses playback. Playback speed changes the presentation pace, not simulated execution.

In Technical view, input tiles highlight changes since the preceding observed cycle, including source timestamps and quality. Gate cards show actual candidate verdicts and first failures. Selection shows only the emitted mode and selected utility. Resource bars compare exact before/after Ledger snapshots; sensor squares represent mask bits. Task lanes group all tasks by their recorded state, with execution progress preserved across pauses. The task selector synchronizes the explanation, remaining-work chart, and resource view.

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

Open **Mission map** first. Follow **Browser reference replay** through the chapters, then switch to **Concurrent CLI simulator** to explain the two-process transport. In the pipe lab, read the example fragments: dispatch occurs only once the example message is complete. Use **Story + systems** to show both levels together. Return through **Set up the guided demo**, generate the False-alarm restoration capture, then use its risk/restoration/completion bookmarks. No backend policy is implemented in the visual layer.

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
