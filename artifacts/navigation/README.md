# Navigation regression evidence

`Tests/NavigationTests.cpp` owns the portable correctness fixtures and the `--benchmark` workload. `CMakeLists.txt` registers the default correctness run as `navigation_regressions`; the benchmark remains an explicit CLI mode so machine timing does not make CTest flaky.

The benchmark uses 160 same-team workers in two crossing groups for 80 simulated seconds. Same-team units prevent combat from changing survivor and arrival counts. Timings come from `Simulation::lastStepMilliseconds()` and report mean, p95 and maximum step time without a pass threshold.

`benchmark-baseline.txt` and `benchmark-final.txt` use AppleClang 21 Release builds with the same fixture geometry and movement commands. `historical-intermediate.txt` records non-final Debug measurements and the earlier traffic failure separately.

`correctness-final.txt` records the Release correctness run, including bounded impossible-route search and save migration coverage.

The evidence files record source revision, compiler, configuration and exact commands. Generated executables remain in the listed temporary build directories and are not stored as repository artifacts.

Final Release/sanitizer, Unreal, local server and native iOS-on-Mac acceptance is indexed in [verification.json](verification.json). Source hashes are in [source-manifest.json](source-manifest.json); the signed package identity is in [package-verification.json](package-verification.json). Native screenshots and runtime records are under `native-ios-on-mac/`. AEON installation, physical routing/touch and sustained iPhone heat remain pending.

Subsequent physical delivery: [aeon/verification.json](aeon/verification.json) records installation, a verified menu screenshot and the reported disappearance. The previous match log continued normally until the agent forced a post-install relaunch. No contemporaneous crash or Jetsam report was found. Full physical gameplay and sustained thermals remain unverified.
