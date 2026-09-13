# Navigation reliability plan

Status on 2026-09-12: **the portable navigation fix passes portable, sanitizer, native engine, local multiplayer, signed iOS package and native iOS-on-Mac validation. The exact package installed successfully on AEON, and its physical menu is visually verified; the reported disappearance most likely came from the agent's forced post-install relaunch.**

The player report was that Drudges and other units could become stuck between objects or fail to reach an order. The diagnosis below records the old implementation that motivated this pass. It is historical, not a description of the current code.

## Historical diagnosis

- Movement used the 64 x 64 fog grid for routing. At a 4800-unit world size, adjacent cell centers were 75 units apart while a Drudge's collision diameter is 32 units. A physically valid turning route could therefore lack a usable chain of grid centers.
- Path retries had no sustained-progress detector. A blocked unit could request the same ineffective route again.
- Construction had a narrow local avoidance rule, while most ground movement relied on all-pairs overlap separation after movement.
- Mining and depot selection used radial or straight-line choices rather than reachable work positions. Construction placement checked clearance, but did not prove that a selected worker could reach the proposed foundation before ore was charged.
- The earlier tests covered ordinary obstacle routes and rejected remote building progress, but did not prove narrow turns, blocked work-site sides or congested mining behavior.

These findings described mechanisms consistent with the report. They did not reconstruct the exact route from the player's match.

## Implemented fix

- The portable simulation now owns a dedicated navigation graph. It uses a cached 16-unit grid with clearance layers by unit size, exact point and segment clearance, and sparse visibility connections around obstacle boundaries. Start and goal positions attach only through exact clear segments, and final paths are shortened only when the replacement segment is clear.
- Unchanged normalized geometry keeps its cached clearance layers. Each movement step gathers and normalizes static blockers once. A changed geometry fingerprint discards the cached navigation state, and entities reject paths stamped with the old fingerprint. This pass does not implement incremental dirty-region updates.
- Workers choose stable reachable positions around ore, depots and foundations. Slot reservations reduce bunching, route cost chooses among valid targets, and temporary reservation exhaustion uses a short retry instead of declaring the target unreachable.
- Construction checks a speculative graph containing the proposed foundation before accepting the order or charging ore. Resuming construction also requires a selected Drudge with a valid route.
- Ground traffic uses 128-unit spatial buckets, stable passing choices and bounded yielding. Idle friendly units can yield, while Hold orders and enemy collision remain protected. Progress tracking detects stalls and triggers bounded retries or replanning.
- Route work is capped at 16 requests per simulation step and 40,000 expanded nodes per search. Complete failure sets `navigationExhausted`; an expansion cap remains retryable. The HUD shows `ROUTE FAILED` and `UNABLE TO FIND A ROUTE`, while rejected construction commands explain that no accessible route exists.
- Save format 5 persists and validates navigation state, and format 4 saves migrate by rebuilding it. Network protocol 2 carries the navigation failure state in authoritative snapshots. Protocol 2 requires updated clients and servers to ship together.

Implementation and evidence are summarized in [NAVIGATION_PASS.md](NAVIGATION_PASS.md) and [the captured navigation artifacts](../artifacts/navigation/README.md).

## Verified so far

- Release CTest passed all three suites: 27 simulation tests, 10 navigation tests and 5 network tests. The same three suites passed under ASan and UBSan in RelWithDebInfo.
- The Mac Editor build passed, all 17 Unreal integration tests passed, and all 6 JavaScript server integration tests passed.
- A two-client `Cinderline.Online.Transport` Unreal run passed without errors or warnings against the local server. This is local validation only. No server deployment occurred.
- The signed iOS build completed 50 actions in 160.92 seconds, and packaging passed in 93.70 seconds. Post-build private engine schema 3, strict native iOS SDK 27 with minimum iOS 15, signing, sustained-execution profile authorization and Game Mode checks passed. The 493,020,605-byte app contains executable SHA-256 `2bf6039a16a225aba6ce35d96e46a6e15aa4985b79cfe86834f35168e28531fa`.
- Fresh menu and live-battle processes passed on that executable in the native 2052 x 1536 iOS-on-Mac viewport. Neither run produced a new crash report, assertion or fatal error, and both screenshots passed visual inspection. The menu measured 14.66 FPS with 75.198 ms p95 frame time over 120 measured frames after 30 warmup frames; `world_rendering_disabled=1` while the menu was open. The unsaved live battle measured 29.18 FPS with 42.448 ms p95 over 600 measured frames after 30 warmup frames and 20.562 seconds. Its ordinary movement, mining and combat fixture reached timer `00:22` with 12,324 ore.
- [source-manifest.json](../artifacts/navigation/source-manifest.json) records the source hashes used for this evidence. No Git commit was requested.

## Remaining acceptance

- Resolve the fresh physical launch failure, then check routing, touch behavior, map boundaries and sustained heat on AEON. Installation alone does not establish any of those behaviors.

The captured Mac benchmark is a regression comparison, not evidence of lower CPU cost. Both Release binaries completed the same 160-worker workload with 160 survivors and 160 arrivals. Baseline timing was mean 0.170 ms, p95 0.187 ms and max 89.459 ms. The current final binary measured mean 0.181 ms, p95 0.393 ms and max 4.324 ms. The final run removed the large maximum spike in this sample, while mean and p95 were slightly higher.

Reference: [Epic's navigation and avoidance guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-avoidance-with-the-navigation-system-in-unreal-engine).

Physical delivery evidence: [AEON verification](../artifacts/navigation/aeon/verification.json). The previous match ran from 22:03:37 to 22:04:07 IST without logged errors; the agent then launched with `--terminate-existing`, starting a fresh log at 22:04:08. The timing strongly supports an agent-triggered restart. No crash fix or complete physical gameplay/thermal acceptance is claimed.
