# Navigation reliability pass

Status on 2026-09-12: **implementation complete with portable, sanitizer, native engine, local multiplayer, signed iOS package and native iOS-on-Mac validation. The exact package installed successfully on AEON, and its physical menu is visually verified; the reported disappearance most likely came from the agent's forced post-install relaunch.**

## Final implementation

- `Navigation` replaces fog-grid routing with a portable cached graph built on 16-unit cells. Clearance-specific layers contain exact validated grid edges plus a sparse visibility overlay at obstacle boundaries. Exact segment checks attach starts and goals and validate path shortening.
- Geometry is normalized once at the start of each simulation movement step. Cached geometry and clearance layers survive when that normalized input is unchanged. A geometry change invalidates stale entity paths through its fingerprint and rebuilds the cached state. There is no incremental dirty-region update in this implementation.
- Gather, return and construction work use stable reachable perimeter slots. Reservations spread workers around a target, route cost selects reachable ore or depots, and temporary slot pressure retries without becoming a permanent route failure.
- New construction runs a reachability preflight against a speculative graph that includes the proposed foundation. The command does not spend ore when no selected Drudge can reach the site. Resume construction applies the same reachability rule.
- Local traffic queries 128-unit spatial buckets. Stable side choices, yielding and progress checks recover congested routes while preserving Hold orders and enemy collision. Routing is limited to 16 requests per step and 40,000 node expansions per request.
- A completed unreachable search sets explicit failure state. The selected-unit HUD reports `ROUTE FAILED` and `UNABLE TO FIND A ROUTE`; construction rejection reports that the site or foundation has no accessible route. Expansion-budget exhaustion remains retryable.
- Save format 5 adds validated navigation state and migrates format 4 saves by rebuilding navigation. Authoritative network protocol 2 serializes the route-failure flag so the server and client agree on status. Protocol 2 requires updated clients and servers to ship together.

## Captured portable evidence

The checked-in AppleClang 21 Release artifacts use the same 160-worker, 80-second crossing workload. Both binaries finished with 160 survivors and 160 arrivals.

| Build | Mean step | p95 step | Maximum step |
| --- | ---: | ---: | ---: |
| Baseline | 0.170 ms | 0.187 ms | 89.459 ms |
| Current final | 0.181 ms | 0.393 ms | 4.324 ms |

The final run took 289.752 ms of wall time and recorded 194 searches, 18,708 expanded nodes, no failed routes and 720 route-budget deferrals. This sample shows correctness parity and a much lower maximum step. It does not show a CPU improvement because current mean and p95 are higher. Exact commands and intermediate diagnostics are in [artifacts/navigation](../artifacts/navigation/README.md). Captured correctness evidence passed 10 of 10 cases, including narrow turning geometry, blocked work-site sides, reachable alternatives, inaccessible construction feedback, a busy single-file mining corridor, foundation invalidation, Hold and enemy collision, work-slot exhaustion, bounded impossible orders, and save validation with format 4 migration.

## Broader verification

- Release CTest passed 3 of 3 suites: 27 simulation tests, 10 navigation tests and 5 network tests.
- ASan and UBSan RelWithDebInfo CTest passed the same 3 of 3 suites.
- The Mac Editor target built successfully, and all 17 Unreal integration tests passed.
- All 6 JavaScript server integration tests passed.
- A two-client Unreal `Cinderline.Online.Transport` run passed without errors or warnings against the local server.
- The signed iOS build completed 50 actions in 160.92 seconds. Packaging passed in 93.70 seconds, followed by a successful private engine schema 3 check.
- Package policy checks verified strict native iOS SDK 27 with minimum iOS 15, signing, sustained-execution signature and profile authorization, and Game Mode. The app is 493,020,605 bytes, and its executable SHA-256 is `2bf6039a16a225aba6ce35d96e46a6e15aa4985b79cfe86834f35168e28531fa`. See [package-verification.json](../artifacts/navigation/package-verification.json) and [package-policy.json](../artifacts/navigation/package-policy.json).
- Fresh menu and live-battle processes passed on that exact executable in the native 2052 x 1536 iOS-on-Mac viewport. Neither run produced a new crash report, assertion or fatal error, and visual inspection passed for both screenshots.
- [source-manifest.json](../artifacts/navigation/source-manifest.json) records the current source hashes. No Git commit was requested.

The server and transport checks used a local server. They do not prove a deployment or production compatibility. Any release using protocol 2 must update the client and server together.

## Native iOS-on-Mac runtime

| Run | Measured sample | Result |
| --- | --- | --- |
| Menu | 120 frames after 30 warmup frames | 14.66 FPS, p95 75.198 ms, `world_rendering_disabled=1` |
| Unsaved live battle | 600 frames after 30 warmup frames, 20.562 seconds | 29.18 FPS, p95 42.448 ms |

The live fixture exercised ordinary movement, mining and combat. Its screenshot showed timer `00:22` and 12,324 ore. Evidence is in [native-ios-on-mac/menu-verification.json](../artifacts/navigation/native-ios-on-mac/menu-verification.json), [native-ios-on-mac/battle-verification.json](../artifacts/navigation/native-ios-on-mac/battle-verification.json) and the adjacent runtime logs and PNG captures. The app processes were closed after capture.

These are short Mac runs. They establish native packaged startup and live-battle behavior on iOS-on-Mac, but they do not establish phone touch behavior or sustained thermal performance.

## Pending validation

- Physical AEON launch: the app remained alive and its menu screenshot passed visual inspection. The user's reported disappearance most likely came from the agent's forced relaunch during an already active match; no contemporaneous crash or Jetsam report was found. Successful installation is recorded in [aeon/install.json](../artifacts/navigation/aeon/install.json).
- Physical routing, touch, boundary and sustained thermal checks: full acceptance remains pending. The user subsequently reported that the installed game worked well.

Do not use the successful installation as physical gameplay or heat evidence. The reported interruption did not reproduce an app crash; report the exact artifact values rather than inferring an improvement.

Physical delivery evidence: [AEON verification](../artifacts/navigation/aeon/verification.json). The previous match ran from 22:03:37 to 22:04:07 IST without logged errors; the agent then launched with `--terminate-existing`, starting a fresh log at 22:04:08. The timing strongly supports an agent-triggered restart. No crash fix or complete physical gameplay/thermal acceptance is claimed.
