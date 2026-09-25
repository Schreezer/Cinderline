# Cinderline

An original touch-first sci-fi RTS for Unreal Engine 5, targeting iPhone and iPad. Its C++ simulation is also built as a standalone library so economy, combat and movement can be tested without launching the editor.

**Current status:** The [gameplay roadmap](docs/SC2_GAMEPLAY_ROADMAP.md) tracks implemented capabilities, tested builds, device evidence and remaining work. Cinderline includes solo play, a six-mission campaign, quick training, global production controls, private 1v1 and four-player FFA, and LAN support. The [task board](docs/TASKBOARD.md) links the implementation and verification reports; [iOS readiness](docs/IOS_READINESS.md) tracks device packaging separately.

## Learning to play

Choose **CAMPAIGN** from the Unreal main menu. The six-mission [Emberline campaign](docs/CAMPAIGN_DESIGN.md) teaches worker commands, expansion, scouting, rallying and defense, research and army composition, then asks you to win a normal AI match. Early lessons point to one action at a time; later missions give a goal with optional **HINT** and **SHOW** controls. Every mission is available for practice. **CONTINUE RECOMMENDED** picks the first unfinished mission, and **RESUME CHECKPOINT** restores the last saved objective boundary.

The original **QUICK TRAINING** match remains available from Campaign, or press **T** on desktop. Open the field guide for additional explanations. Campaign progress and checkpoints are separate from the skirmish save; using a hint records an assisted attempt without blocking completion.

For queued movement, hold **Shift** when issuing desktop Move/Attack-move destinations. On touch, choose **MOVE** or **ATTACK**, open **ORDERS**, then choose **QUEUE MOVE** or **QUEUE ATTACK** and tap a waypoint. **CLEAR QUEUED** keeps the current order; **STOP** clears all. See [tactical orders](docs/TACTICAL_ORDERS_PASS.md) for limits, worker behavior and verification.

For a single builder's plan, select one **Drudge**. On desktop, hold **Shift** when placing buildings or right-clicking ore/an unfinished friendly building. On touch, open **ORDERS → QUEUE WORK**, then **BUILD WITH THIS DRUDGE** for repeated sites, or tap ore/a foundation. Numbered sites spend ore when construction starts; blocked or unaffordable jobs are skipped with feedback. See [construction queues](docs/BUILDER_QUEUE_PASS.md) for behavior and validation status.

Open **FIELD GUIDE** from the menu or pause screen, or press **F1** during play. It pauses offline matches while you read. Online matches keep running on the server. Choose desktop or touch instructions, then close the guide and explicitly resume. Training does not overwrite your saved skirmish.

## Online 1v1

Choose **ONLINE 1V1** in the Unreal menu, or press **O**. Enter the same server endpoint on both clients. One player creates a room and shares its six-character code; the other joins. Both choose **READY** to start.

The dedicated server runs the existing rules and sends each player only their own perspective. Online menus and the field guide do not pause the match. Surrender waits for a server-confirmed result; leaving forfeits. One disconnected player has 60 seconds to reconnect while the other stays connected. Online matches never overwrite the offline save.

For development, run the [game server](Server/README.md) and use `ws://127.0.0.1:8787/play`. Friends on separate networks need a reachable `wss://` endpoint. The server README includes Docker, DNS and Caddy deployment steps. Persistent public hosting is not configured in this repository. See [multiplayer evidence and remaining checks](docs/MULTIPLAYER_PASS.md).

Run the native/Node server tests with `npm --prefix Server test` after building the worker. `./scripts/test-multiplayer.sh` builds and runs the Unreal two-client test against an isolated local server.

## Development

Check the environment:

```sh
./scripts/doctor.sh
```

Set `UE_ROOT` if the engine is installed outside `/Users/Shared/Epic Games`. Unreal build and launch instructions live in [UNREAL.md](docs/UNREAL.md).

Build and run simulation checks:

```sh
./scripts/test.sh
./scripts/test-sanitize.sh
```

Launch the native macOS playtest runner:

```sh
./scripts/run-native.sh
```

Create a double-clickable development app with `./scripts/package-native.sh`.
It writes `artifacts/Cinderline Playtest.app`. This app uses the same gameplay
simulation as the Unreal project. `--load` restores the local playtest save;
`--render-stress 3000` runs an offscreen drawing regression without opening a window.

## Layout

- `Source/Cinderline/Public/Sim` defines state and the command interface.
- `Source/Cinderline/Private/Sim` implements simulation and opponent behavior.
- `Source/Cinderline/Private/Presentation` adapts the simulation to Unreal.
- `Server` hosts private rooms and the authoritative match worker.
- `Tools/Native` contains the macOS development runner.
- `Tests` exercises gameplay outcomes and regression cases.
- `Config` contains Unreal and mobile settings.
- `docs` contains design choices, checkpoint evidence and the original brief.

All gameplay changes belong in the shared simulation. The Unreal and native presentation layers read state and submit commands. They do not award resources, spawn opponent reinforcements or decide combat outcomes.

## Design and scope

The [design notes](docs/DESIGN.md) explain the faction, controls and architecture. The [original product brief](docs/PRODUCT_BRIEF.md) defines the full roadmap. A 20–30 minute competitive match is a design target, not an achieved balance result. Accounts, public matchmaking, ranking and physical iOS validation require later checkpoints. Private online 1v1 is implemented; persistent hosting is a separate deployment step.

The [foundation report](docs/CHECKPOINT_00.md), [mechanics report](docs/MECHANICS_PASS.md) and [presentation report](docs/PRESENTATION_PASS.md) record what was actually exercised.
Exact test output, source hashes, performance samples and match durations are in
[the verification record](artifacts/verification-summary.md).
