# Cinderline

An original touch-first sci-fi RTS for Unreal Engine 5, targeting iPhone and iPad. Its C++ simulation is also built as a standalone library so economy, combat and movement can be tested without launching the editor.

**Current status:** UE 5.8.2 builds and runs Cinderline on macOS, and the player has confirmed skirmishes are playable. Private online 1v1 rooms are implemented with an authoritative server, reconnects and server-confirmed results. The game also includes a nine-step guided practice mission and an in-game field guide for controls, economy, construction, army tactics, scouting, technology and troubleshooting. The reference covers all units and buildings using their current gameplay definitions. See [help and training](docs/HELP_TUTORIAL_PASS.md), [animation and terrain](docs/ANIMATION_TERRAIN_PASS.md) and [the task board](docs/TASKBOARD.md) for evidence and remaining work. iOS touch, lifecycle and safe-area preparation is saved; the ARM64 simulator build and the separate device-signing requirements are tracked in [iOS readiness](docs/IOS_READINESS.md). The native macOS runner is a development tool; it is not the iOS product.

## Learning to play

Choose **GUIDED TRAINING** from the Unreal main menu, or press **T** on desktop. Practice has no attacking AI and walks through mining, production, building, scouting and attack-move. The current objective offers **FIND** and **HELP**; the compact layout has **MORE** for extra context.

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
