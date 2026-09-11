# Unreal runtime

The real game project is `Cinderline.uproject`. Its C++ game mode owns a camera,
controller and HUD; the battlefield actor adapts the shared portable simulation.
Gameplay rules are not duplicated in this presentation layer.

## First run

Finish installing a compatible modern UE5 release and its supported Xcode toolchain.
The launcher installation started in this session is UE 5.8.2, and the project
uses the 5.8 association. Supply the installed engine explicitly when more than
one version exists:

```sh
export UE_ROOT="/Users/Shared/Epic Games/UE_5.8" # replace if installed elsewhere
./scripts/unreal.sh doctor
./scripts/unreal.sh setup
./scripts/unreal.sh play
```

`setup` builds the Editor target, then runs the editor Python bootstrap to create
the original parameterized material and `/Game/Maps/Frontier`. There are no copied
third-party models or textures. Primitives ship with Unreal; each unit and building
uses an original multi-part silhouette. Existing generated assets are preserved.
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
  Arrow keys pan. A attack-move, S stop, H hold, B build, F focus, Space home, Esc pause.
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

No Unreal installation was available when these sources were initially authored.
Source review and portable simulation tests do not prove UHT compilation, shader
compilation, touch behavior, Metal performance, packaging or iOS correctness.
The first required validation is `setup`, followed by an actual Unreal play session.
Do not mark checkpoint 0 complete until the battlefield launches and camera works.

The current presentation uses placeholder primitives and Canvas UI, without
character animation, audio, haptics, smooth fog edges, retained offscreen enemy
building intelligence, networking or a second asymmetric faction. Camera smoothing
is implemented; momentum/gesture tuning still requires physical-device testing.
Device-specific safe area and suspend/resume handling need a later iOS product pass.

## iOS

The project configures landscape iPhone/iPad rendering and modest mobile effects.
Set your own signing team and bundle identifier in Project Settings → iOS; the
included identifier is a placeholder. `./scripts/unreal.sh package-ios` invokes UAT
after native build/bootstrap, with signing supplied through your Unreal settings.
It does not install, upload or publish an application. Actual iPhone/iPad build,
installation, touch input, and full-length thermal/performance tests remain required.
