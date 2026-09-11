# Unreal runtime

The real game project is `Cinderline.uproject`. Its C++ game mode owns a camera,
controller and HUD; the battlefield actor adapts the shared portable simulation.
Gameplay rules are not duplicated in this presentation layer.

## First run

UE 5.8.2 and its Mac toolchain are installed. The project uses the 5.8 association. Supply the installed engine explicitly when more than
one version exists:

```sh
export UE_ROOT="/Users/Shared/Epic Games/UE_5.8" # replace if installed elsewhere
./scripts/unreal.sh doctor
./scripts/unreal.sh setup
./scripts/unreal.sh play
```

Unreal needs Apple's Metal compiler even for desktop rendering. If Xcode reports
a missing Metal Toolchain, install it and refresh its SDK-specific lookup cache:

```sh
xcodebuild -downloadComponent MetalToolchain
xcrun --kill-cache --sdk macosx metal --version
```

These commands repaired the active Xcode 27 installation during the desktop
presentation pass. The `doctor` command now checks the same macOS compiler lookup.

`setup` builds the Editor target, then runs the editor Python bootstrap to import
the original terrain and menu artwork, Blender models and audio, create materials and generate
`/Game/Maps/Frontier`. Generated PNG sources and exact prompts are recorded in
[ART_ASSETS.md](ART_ASSETS.md). The bootstrap configures a 1024 by 1024 texture build with mipmaps while preserving
the original source. Cooked texture dimensions have not yet been inspected.
The model importer validates centimeter dimensions, bottom-centered pivots, all five
material slots and triangle counts. Missing or invalid runtime models use the earlier
multi-part silhouettes. Asset provenance is in [MODEL_ASSETS.md](MODEL_ASSETS.md)
and [AUDIO_ASSETS.md](AUDIO_ASSETS.md). Existing meshes are preserved unless
`CINDER_REIMPORT_MODELS=1` is supplied; material and texture settings are refreshed.
`CINDER_REBUILD_CONTENT=1 ./scripts/unreal.sh bootstrap` recreates the map explicitly.

The Python plugin is Editor-only. It is not needed inside the packaged game.
`./scripts/unreal.sh editor` opens the project for editing. The map should be loaded
before pressing Play. On a fresh checkout run `setup` before packaging.

## Play

- Tap a friendly unit or structure to select it; tap terrain, enemy or ore to issue
  a contextual command. Tap terrain with production structures selected to rally.
- Touch: drag to pan; use two fingers to pan and pinch; hold still for 0.42 seconds,
  then drag to select. SELECT BOX arms selection without needing a hold. Double-tap
  a unit to select its visible type. The camera remains bounded to the battlefield.
- Mouse: left-drag selects; middle-drag pans; wheel zooms; right-click commands.
  Arrow keys pan, including short taps. Enter starts, resumes or rematches.
  A attack-move, S stop, H hold, B build, F focus, Space home. Esc cancels an active
  command mode before pausing; the PAUSE button always pauses immediately.
- Build with a Drudge selected. The placement ring reports collision/vision validity;
  the simulation authoritatively checks resources, workers and prerequisites.
- Select a structure to train units or queue research. Queue buttons cancel entries.
  An unfinished building exposes cancellation. Selected-army type buttons isolate
  a subgroup; NEXT TYPES pages through large mixed compositions.
- The pause menu saves/loads a local match and exposes performance counters.
  Destroy the opposing Anchor to show results, rematch or return to the main menu.

## Implementation boundaries

Simulation runs at its own fixed step. The actor updates batched mesh transforms
at 20 Hz; simulation logic remains outside actors and UI. Resource observations are
cached while unseen, so offscreen depletion does not leak. This presentation cache
is rebuilt from current vision on load. Visibility hides enemy
entities, and the battlefield and minimap draw a 64×64 vision grid. UI feedback
and weapon traces are projected from simulation state. Phones and tablets use a separate compact HUD with 44-unit controls at the
667x375 design baseline, contextual sheets, and conservative cutout/home-indicator
insets. This layout still needs physical-device and actual safe-area verification.
Native APIs handle all
mouse/touch input; this build does not rely on a touch joystick overlay.

The Editor C++ build and content bootstrap passed on UE 5.8.2. The actual Unreal
window displayed the menu and lit battlefield, gathered ore, moved the camera
with arrows and Home, zoomed with the wheel, paused/resumed and displayed natural
defeat. Desktop and compact HUDs were inspected. The user confirmed the menu
button works with their own mouse. Synthetic absolute clicks do not update
Unreal's cached cursor location on this Mac, so automated mouse targeting remains
unreliable. These checks do not establish touch quality, a packaged build or iOS
performance. See [the verification record](../artifacts/verification-summary.md).

The presentation uses static Blender models and Canvas UI with interface, order
and production audio hooks. It still needs character animation, combat audio,
haptics, smooth fog edges, player-facing last-seen enemy building markers,
networking or a second asymmetric faction. The opponent's scouting memory and
objective selection are tracked in [AI_STRATEGY_PASS.md](AI_STRATEGY_PASS.md). Camera smoothing
is implemented; momentum/gesture tuning still requires physical-device testing.
Device-specific safe area and suspend/resume handling need a later iOS product pass.

`./scripts/test-unreal.sh` runs three strict transient-world integration tests for
world/controller lifecycle, paid economy and worker construction, and AI
observation/persistence. The AI fixture uses a starting Drudge's ordinary travel
into and out of opponent vision, then verifies a temporary save and continued
actor ticks. The runner requires every expected path to succeed with zero errors
or warnings. These tests open no gameplay viewport and do not modify player saves.

## iOS

iOS work is deferred until the mechanics, assets and full-playtest passes are ready.
The preflight findings and remaining steps are in [IOS_READINESS.md](IOS_READINESS.md).
The project configures landscape iPhone/iPad rendering and modest mobile effects.
Set your own signing team and bundle identifier in Project Settings → iOS; the
included identifier is a placeholder. `./scripts/unreal.sh package-ios` invokes UAT
after native build/bootstrap, with signing supplied through your Unreal settings.
It does not install, upload or publish an application. Actual iPhone/iPad build,
installation, touch input, and full-length thermal/performance tests remain required.
