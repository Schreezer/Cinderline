#!/usr/bin/env bash
set -euo pipefail

# UE_ROOT accepts either /path/UE_5.x or /path/UE_5.x/Engine.
project_root="$(cd "$(dirname "$0")/.." && pwd)"
project_file="$project_root/Cinderline.uproject"
action="${1:-help}"
shift || true

find_engine() {
  if [[ -n "${UE_ROOT:-}" ]]; then
    engine_root="${UE_ROOT%/}"
    [[ "$(basename "$engine_root")" == Engine ]] && engine_root="$(dirname "$engine_root")"
  else
    engine_root=""
    local candidate
    for candidate in /Users/Shared/Epic\ Games/UE_5.* /Users/Shared/UnrealEngine/UE_5.* "$HOME"/UnrealEngine/UE_5.* /opt/unreal-engine /opt/UnrealEngine; do
      if [[ -d "$candidate/Engine/Build/BatchFiles" ]]; then engine_root="$candidate"; fi
    done
  fi
  if [[ -z "$engine_root" || ! -d "$engine_root/Engine/Build/BatchFiles" ]]; then
    printf '%s\n' 'Unreal Engine is not available yet. Once installed, set UE_ROOT to its installation directory.' >&2
    printf '%s\n' 'Example: UE_ROOT="/Users/Shared/Epic Games/UE_5.8" ./scripts/unreal.sh build' >&2
    exit 2
  fi
  case "$(uname -s)" in
    Darwin)
      platform=Mac
      build_script="$engine_root/Engine/Build/BatchFiles/Mac/Build.sh"
      editor="$engine_root/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
      command_editor="$engine_root/Engine/Binaries/Mac/UnrealEditor-Cmd.app/Contents/MacOS/UnrealEditor-Cmd"
      [[ -x "$command_editor" ]] || command_editor="$editor"
      ;;
    Linux)
      platform=Linux
      build_script="$engine_root/Engine/Build/BatchFiles/Linux/Build.sh"
      editor="$engine_root/Engine/Binaries/Linux/UnrealEditor"
      command_editor="$editor"
      ;;
    *) printf '%s\n' 'Use UnrealBuildTool / UnrealEditor directly on Windows; this helper supports macOS and Linux.' >&2; exit 2 ;;
  esac
}

build() { "$build_script" CinderlineEditor "$platform" Development "$project_file" -WaitMutex "$@"; }
bootstrap() {
  mkdir -p "$project_root/Saved/Logs"
  "$command_editor" "$project_file" /Engine/Maps/Entry -unattended -nop4 -nosplash \
    "-ExecutePythonScript=$project_root/scripts/unreal_bootstrap.py" \
    "-abslog=$project_root/Saved/Logs/CinderlineBootstrap.log" "$@"
  if ! rg -q CINDERLINE_BOOTSTRAP_OK "$project_root/Saved/Logs/CinderlineBootstrap.log"; then
    printf '%s\n' 'Bootstrap did not produce a success marker. Inspect Saved/Logs/CinderlineBootstrap.log.' >&2
    exit 1
  fi
}

case "$action" in
  help|-h|--help)
    printf '%s\n' 'Cinderline Unreal workflow (set UE_ROOT if automatic discovery is ambiguous):' \
      '  ./scripts/unreal.sh doctor       Show engine / toolchain availability' \
      '  ./scripts/unreal.sh build        Build CinderlineEditor' \
      '  ./scripts/unreal.sh bootstrap    Generate material and map after first build' \
      '  ./scripts/unreal.sh setup        Build, then bootstrap' \
      '  ./scripts/unreal.sh editor       Open the Unreal editor' \
      '  ./scripts/unreal.sh play         Run the native game in a standalone window' \
      '    Mac defaults to 2560x1440 framebuffer pixels; Linux defaults to 1280x720.' \
      '    Override with CINDERLINE_RES_X / CINDERLINE_RES_Y or -ResX=667 -ResY=375.' \
      '    CLI resolution values take precedence over environment values.' \
      '  ./scripts/unreal.sh package-ios  Cook/package development iOS (requires signing)'
    ;;
  doctor)
    find_engine
    printf 'Engine: %s\nPlatform: %s\nEditor: %s\n' "$engine_root" "$platform" "$editor"
    [[ -x "$editor" ]] || { printf '%s\n' 'Editor executable missing; installation may still be running.' >&2; exit 2; }
    if [[ "$platform" == Mac ]]; then
      xcode-select -p
      xcrun --find clang
      xcrun --sdk macosx metal --version
    fi
    ;;
  build) find_engine; build "$@" ;;
  bootstrap) find_engine; bootstrap "$@" ;;
  setup) find_engine; build; bootstrap "$@" ;;
  editor) find_engine; exec "$editor" "$project_file" "$@" ;;
  play)
    find_engine
    [[ -f "$project_root/Content/Maps/Frontier.umap" ]] || { printf '%s\n' 'Run ./scripts/unreal.sh setup first to generate the map.' >&2; exit 2; }
    if [[ "$platform" == Mac ]]; then
      play_res_x="${CINDERLINE_RES_X:-2560}"
      play_res_y="${CINDERLINE_RES_Y:-1440}"
    else
      play_res_x="${CINDERLINE_RES_X:-1280}"
      play_res_y="${CINDERLINE_RES_Y:-720}"
    fi
    play_window_mode=-windowed
    play_arguments=("$project_file" /Game/Maps/Frontier -game -log)
    # Resolve overrides once: Unreal otherwise receives conflicting resolution flags.
    while (( $# )); do
      case "$1" in
        -[Rr][Ee][Ss][Xx]=*) play_res_x="${1#*=}" ;;
        -[Rr][Ee][Ss][Yy]=*) play_res_y="${1#*=}" ;;
        -[Ww][Ii][Nn][Dd][Oo][Ww][Ee][Dd]) play_window_mode=-windowed ;;
        -[Ff][Uu][Ll][Ll][Ss][Cc][Rr][Ee][Ee][Nn]) play_window_mode=-fullscreen ;;
        *) play_arguments+=("$1") ;;
      esac
      shift
    done
    for play_dimension in "$play_res_x" "$play_res_y"; do
      if [[ ! "$play_dimension" =~ ^[1-9][0-9]{0,4}$ ]] || (( play_dimension > 16384 )); then
        printf '%s\n' 'Framebuffer dimensions must be integers from 1 to 16384.' >&2
        exit 2
      fi
    done
    exec "$editor" "${play_arguments[@]}" "$play_window_mode" "-ResX=$play_res_x" "-ResY=$play_res_y"
    ;;
  package-ios)
    find_engine
    "$engine_root/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun "-project=$project_file" \
      -noP4 -platform=IOS -clientconfig=Development -build -cook -stage -pak -package -archive \
      "-archivedirectory=$project_root/Saved/Packages/IOS" "$@"
    ;;
  *) printf 'Unknown action: %s\n' "$action" >&2; exit 2 ;;
esac
