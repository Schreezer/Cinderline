# Code quality review and refactors

Status: complete for the reviewed scope. Three independent review rounds and all checks below pass.

## Changes

- `Sim/SimulationRules.h` owns kind classification, producer sets, research queue sentinels, player counts and valid winner/elimination states. Commands, save loading and network validation consume the same rules. Enum values, protocol 7 and save version 10 are unchanged.
- `ACinderPlayerController` now stores one private destination mode instead of four mutually exclusive booleans. Move, attack-move, defend and production rally share switching and cancellation. HUD reads use getters; rendering and layout are unchanged. Tests drive actual public actions rather than constructing impossible combinations of modes.
- `Server/worker-bridge.js` owns child-process startup, framing, queues, backpressure, timeouts and shutdown. `server.js` retains room, player and result policy. `protocol.js` shares Node protocol constants with LAN advertisement. Docker packaging includes the new modules and a dependency-closure test checks that future imports cannot be omitted.
- Online integer fields share strict typed and integral validation. Fractional or numeric-string acknowledgements cannot clear another pending command. Valid integer acknowledgement behavior is unchanged.
- CMake has one simulation-test registration helper with unchanged test names, labels and timeouts. Native test builds default to two jobs with an explicit override.
- Build and automation scripts share verified engine discovery. Suite filters and exact expected paths live in one manifest, with a recursive source-registration check. The local runner covers 37 tests and the multiplayer runner requires both 1v1 and four-player transport. A separate live Bonjour test needs an advertisement and is explicitly excluded from the local run. The macOS Bash 3.2 build path uses an always-nonempty argument array and one effective parallel-job argument.

## Review decisions

Three review rounds with the same independent reviewer are complete. No important actionable issue remains in the refactored scope.

Accepted and resolved findings:

1. A fractional acknowledgement could resolve the wrong pending command. Strict parsing and malformed-ack regressions cover this.
2. The standard Unreal runner selected the current suites but expected obsolete test lists. The shared manifest and report validator now fail on missing, duplicate, unexpected, unfinished or failed tests.
3. The standard multiplayer runner omitted four-player transport. It now requires both formats.
4. One producer-kind list remained duplicated in save recording admission after the first extraction. It now calls the shared rule.
5. The extracted Node modules were initially omitted from the Docker runtime copy and allowlist. Both are fixed and checked through the runtime import graph.
6. The actual one-job Mac build exposed a Bash 3.2 empty-array failure. Argument assembly is corrected and fake build captures cover stock/prepared engines, spaces, defaults and overrides.

The isolated engine tests initially compared `/var` fixture paths against macOS's canonical `/private/var` paths. The fixture now resolves paths consistently; production engine selection still uses `pwd -P`.

## Verification

- Portable simulation suites: 8/8 passed, with exhaustive kind and winner/mask rule cases. CTest names, labels and timeouts match the captured baseline.
- Baseline comparison: 12/12 deterministic worker scenarios match across two/four players, all maps and two seeds. Commands, tick batches, fog, queues, float bit patterns, ordering, eliminations and terminal results are compared. Deliberately random opaque handles are normalized; raw packets are not expected to be byte-identical across processes.
- Node backend: 32/32 passed against the newly compiled worker, including worker framing/backpressure/lifecycle tests and Docker dependency closure.
- Verification tooling: 43/43 lightweight tests passed, including fake engine/build captures and malformed report/manifest cases.
- Mac Unreal build: succeeded with one compiler action at a time using the verified sibling `CinderlineEngineIOS27` engine.
- Unreal runtime: 39/39 final results pass. The maintained local runner passed 36/37 initially. The new terminal-state test fixture was corrected to derive its snapshot from an actual authoritative forfeit, then the one failed test passed its scoped rerun. Both real multiplayer transport tests passed through the maintained multiplayer runner. The initial report remains available and the aggregate replaces only that failed case.
- The exact documented `idevice_id` host-warning pair was explicitly permitted in two tests; no other warnings or test failures were accepted.
- The default server worker was refreshed from the current source. All owned test processes are closed and protected saves/preferences remained unchanged.

Evidence is in [the combined verification record](../artifacts/code-quality/verification.json) and [the Unreal aggregate](../artifacts/code-quality/unreal-verification.json). The source baseline is `Saved/QualityReview/baseline.json` and `baseline.tar.gz`; this pass is reviewed against that baseline rather than the much larger pre-existing dirty Git diff.

## Follow-ups and limits

The review covered the shared simulation, Unreal presentation and online client, Node backend and build/verification entrypoints. It did not audit every asset-generation tool or platform plugin.

HUD sheet branches intentionally differ in cancellation, pin clearing and page resets. They were not merged into a generic transition helper. A later split by screen responsibility should first capture those behaviors as explicit tests.

Caching repeated static rendering observations needs a separate measured performance pass with reset/load/reveal invalidation coverage. Manual/automatic production validation also needs careful preservation of rejection precedence and allocation ordering. Server configuration has intentionally different strict environment and fallback programmatic input semantics. These are follow-ups, not blockers to the current changes.

No new iOS package, physical-device test, public deployment, Docker image build or performance claim is included in this pass. Existing room persistence and deployment limitations remain as recorded in `FOUR_PLAYER_PASS.md`.
