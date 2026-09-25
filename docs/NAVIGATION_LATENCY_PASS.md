# Navigation latency pass

Status: portable endpoint-check optimization verified; current engine gates deferred while Android support initializes. P0 remains open. This is the next bounded P0.2 pass in [the gameplay roadmap](SC2_GAMEPLAY_ROADMAP.md).

## Evidence and target

The verified production/traffic revision reaches every assigned destination in the synthetic and paid workloads, but its measured worst simulation steps are 182.99 ms for two players and 307.75 ms for four. The phase counters identify movement/economy as the expensive phase, without proving which internal operation causes the longest step. The previous pass and immutable core are indexed in [PRODUCTION_EXIT_PASS.md](PRODUCTION_EXIT_PASS.md) and `artifacts/production-exit/verified-core-sha256.json`.

The full diagnostic trace reproduces the verified four-player state, trajectory and recording hashes. Its worst captured step was preparation tick 6416 at 297.41 ms. A subsequent 10,000-tick capture uses exact `simulation.update` signpost intervals; its matching step measures 318.55 ms with Instruments attached. The first trace used a category excluded by the template, so its approximate wall-clock alignment is historical diagnostic evidence; the corrected trace is the source for exact interval attribution.

The exact interval join in `artifacts/latency/sample-analysis-prefix-before.json` finds 316 samples in tick 6416 and 274 in tick 8827, with no PID/TID mismatches or missing stacks. These 1 ms samples identify CPU work; they are not precise CPU duration or utilization measurements.

The hot call chain is `updateEconomy -> chooseWorkTarget -> cachedGoalAttachments -> attachments -> segmentClear`. Repeated endpoint checks account for substantial work. The candidate adds a shared obstacle-only segment helper and uses it only where both endpoints have already passed the same clearance/ignore validation. Public segment checks, collision math, candidate order, path costs, cache keys and route budgets stay unchanged.

A native 10,000-tick diagnostic before/after comparison measures the same peak at 308.61 / 208.04 ms; preparation p99 is 19.16 / 14.81 ms. All three prefix hashes match. Prefix runs are deliberately marked incomplete and cannot replace complete workload acceptance. Wall times are desktop observations, subject to OS scheduling and the concurrently initializing Launcher; no CPU/GPU or thermal improvement is inferred for a phone.

The proposed distant-box broad phase is deferred to a separate change. It would introduce a new floating-point rejection predicate; the endpoint-only change can preserve the existing geometric decisions directly.

## Acceptance

- [x] Retain a trace or equivalent precise timing/call-chain evidence for slow steps.
- [x] Identify the expensive operation and preserve a reproducible before case.
- [x] Implement a bounded change that preserves exact route/command behavior, or explicitly document and test any necessary behavior change.
- [x] Re-run the relevant portable regression, sanitizer and server gates on the final source.
- [ ] Run the current Unreal build and automation after Android initialization finishes.
- [x] Compare matching complete paid workloads with exact state/trajectory/recording checks, original destinations, and retained synthetic route cases.
- [x] Record p50/p95/p99 and worst-step results with profiling overhead and environmental limits stated.

Physical input, CPU/GPU rendering, frame pacing, thermals, battery and supported-device acceptance remain separate P0 gates. No new iOS package or deployment is part of this pass.

## Complete workload results

The unchanged paid workloads completed their normal preparation, 2,400-tick march and 2,400-tick combat, then repeated deterministically. The before results come from the immediately preceding verified production/traffic pass; after results use its same workload source with this navigation change. All state, trajectory and recording hashes match the baseline.

| Workload / phase | Before p50 / p95 / p99 / max (ms) | After p50 / p95 / p99 / max (ms) |
|---|---|---|
| Two-player preparation | 0.566 / 1.429 / 1.877 / 182.985 | 0.524 / 1.239 / 1.596 / 136.574 |
| Two-player march | 0.285 / 0.433 / 0.674 / 5.163 | 0.286 / 0.462 / 0.746 / 3.893 |
| Two-player combat | 0.099 / 0.624 / 2.713 / 6.702 | 0.099 / 0.620 / 2.184 / 5.067 |
| Four-player preparation | 1.992 / 4.800 / 6.638 / 307.754 | 1.760 / 3.982 / 5.762 / 220.532 |
| Four-player march | 0.976 / 1.676 / 2.270 / 15.434 | 1.024 / 1.527 / 2.239 / 13.342 |
| Four-player combat | 0.273 / 2.009 / 5.341 / 11.518 | 0.283 / 1.756 / 4.061 / 8.052 |

Both workloads reach all 80 army destinations per seat, with no changed goals. Preparation peaks decrease about 25% / 28%; individual small timings fluctuate, including slightly higher march p50/p99 in some samples. This is not a supported-device, sustained FPS or thermal result. The remaining ~221 ms peak is still unacceptable for the full responsiveness gate.

## Trace confirmation and verification boundaries

The corrected after capture (`sample-analysis-prefix-after.json`) measures tick 6416 at 265.85 ms and tick 8827 at 187.18 ms, with 196 / 180 main-thread samples in those intervals. The before capture measures 318.55 / 274.48 ms with 316 / 274 samples. Repeated point validation disappears from the sampled attachment hot path; the remaining dominant stack runs through the unchanged segment/box distance predicates. The difference between interval duration and sample counts is another reason not to equate sampled CPU work with wall time or quote a phone utilization reduction.

Unreal builds/automation are paused while the user initializes Android support. The prior pass's Unreal results belong to the prior source revision; this pass does not reuse them as current-source proof. No Android implementation, iOS package, device installation or deployment is included.

## Portable acceptance receipt

[Verification](../artifacts/latency/verification.json) is reproduced by `python3 artifacts/latency/verify_endpoint_pass.py`. It checks the frozen gameplay/test inputs, 16 Release suites, 16 AddressSanitizer/UndefinedBehaviorSanitizer suites, 32 server tests, all synthetic arrivals, complete paid workloads, every non-timing benchmark field, and exact before/after predicate and route reports.

The new suite covers 5,760 fixed-seed predicate comparisons plus explicit tangent, ULP, world-boundary, invalid and extreme inputs. Its 21-case route corpus includes 17 reached graph searches, cached/fresh paths, multi-source routes and both ignored-start/ignored-goal grid recovery. The result hash `0x0a097241ddad648f` was generated by the immutable preceding core before being frozen as a regression assertion. Review found and corrected missing ignored-grid coverage and a self-comparison-only oracle; no important production issue remained.

The new core and matching headers are preserved in `Saved/P0NavigationEndpointVerified`; `artifacts/latency/verified-core-sha256.json` records their hashes. The previous core remains intact. Current source is local; no commit, push, platform package or deployment was performed in this pass.
