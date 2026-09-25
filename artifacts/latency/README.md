# Match latency probe

`MatchLatencyProbe.cpp` is a diagnostic copy of `Tools/MatchBaseline.cpp`. It retains the baseline roster, maps, seeds, commands, phase lengths, preparation rules, and measured-run validation criteria.

The probe adds one Apple signpost log, using subsystem `com.cinderline.simulation` and the literal `OS_LOG_CATEGORY_POINTS_OF_INTEREST` category required by the selected Instruments template. Every `simulation.update(Simulation::Step)` in `stepStage` is wrapped by a `simulation.update` interval whose begin and end events share a generated signpost ID and include the intended tick and phase name. Non-Apple builds compile the same workload without signpost calls. `MatchLatencyProbeInitial.cpp` preserves the first successful custom-category probe for comparison; its `MatchLatency` category may not appear in a template filtered to `PointsOfInterest`.

Each update is also timed with `std::chrono::steady_clock`. Updates lasting at least 20 ms emit one `match-latency slow_step` record to stderr containing the tick, phase, measured wall duration, monotonic offset from probe startup, wall-clock Unix epoch milliseconds, and every simulation phase profile field. Simulation profiling is forced on for this diagnostic probe; `--profile` remains accepted for command-line compatibility.

Run exactly one measured diagnostic invocation with the same baseline arguments needed for the investigation, for example:

```sh
./MatchLatencyProbe --players4
```

The JSON output declares `diagnostic_only: true`, `full_acceptance: false`, and `repeat_run_performed: false`. It reports whether the single measured run passed its workload criteria, but it never reports baseline success or deterministic-repeat acceptance. Use the unchanged `Tools/MatchBaseline.cpp` workflow for full acceptance.

For a short preparation trace, stop after an exact number of completed simulation ticks:

```sh
./MatchLatencyProbe --players4 --diagnostic-stop-after-ticks 10000
```

This option does not alter commands or tick work before the boundary. The probe exits immediately after validating tick `N`, before any later workload operation. Its JSON reports `completed: false`, `diagnostic_interrupted_prefix: true`, `measured_run_valid: false`, and `full_acceptance: false`; the prefix cannot satisfy the full benchmark criteria.

When profiling on macOS, record the process with Instruments using the Points of Interest/signpost track. Correlate long `simulation.update` intervals with the matching stderr `slow_step` line and its phase breakdown.
