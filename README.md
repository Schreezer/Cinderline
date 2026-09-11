# Cinderline

An original touch-first sci-fi RTS for Unreal Engine 5, targeting iPhone and iPad. Its C++ simulation is also built as a standalone library so economy, combat and movement can be tested without launching the editor.

**Current status:** UE 5.8.2 builds and runs the battlefield on macOS. Menu start, economy, camera navigation, pause/resume and compact layouts have runtime evidence. The first mechanics pass is verified. Fifteen original models, larger HUD text and initial sound feedback are integrated; the refreshed game is ready for hands-on playtesting. Work proceeds through mechanics, assets and visual polish, full playtesting, then iOS. Checkpoint completion and verified behavior are tracked in [the task board](docs/TASKBOARD.md). The native macOS runner is a development tool; it is not the iOS product.

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
- `Tools/Native` contains the macOS development runner.
- `Tests` exercises gameplay outcomes and regression cases.
- `Config` contains Unreal and mobile settings.
- `docs` contains design choices, checkpoint evidence and the original brief.

All gameplay changes belong in the shared simulation. The Unreal and native presentation layers read state and submit commands. They do not award resources, spawn opponent reinforcements or decide combat outcomes.

## Design and scope

The [design notes](docs/DESIGN.md) explain the faction, controls and architecture. The [original product brief](docs/PRODUCT_BRIEF.md) defines the full roadmap. A 20–30 minute competitive match is a design target, not an achieved balance result. Multiplayer, accounts, ranking and physical iOS validation require later checkpoints.

The [foundation report](docs/CHECKPOINT_00.md), [mechanics report](docs/MECHANICS_PASS.md) and [presentation report](docs/PRESENTATION_PASS.md) record what was actually exercised.
Exact test output, source hashes, performance samples and match durations are in
[the verification record](artifacts/verification-summary.md).
