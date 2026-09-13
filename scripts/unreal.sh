#!/usr/bin/env bash
set -euo pipefail

# UE_ROOT accepts either /path/UE_5.x or /path/UE_5.x/Engine.
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
project_file="$project_root/Cinderline.uproject"
prepared_ios_engine="$(dirname "$project_root")/CinderlineEngineIOS27"
ios_engine_preparer="$project_root/scripts/prepare-ios27-engine.py"
ios_app_archiver="$project_root/scripts/archive-ios-app.py"
ue_root_is_explicit=false
[[ -n "${UE_ROOT:-}" ]] && ue_root_is_explicit=true
using_prepared_ios_engine=false
using_prepared_metalfx_engine=false
action="${1:-help}"
shift || true

# shellcheck source=lib/unreal-engine.sh
source "$project_root/scripts/lib/unreal-engine.sh"

find_engine() { cinder_find_engine "$project_root"; }
prepared_engine_source() { cinder_prepared_engine_source "$1"; }

ios_scene_lifecycle_enabled() {
  awk '
    function trim(value) {
      sub(/^[[:space:]]+/, "", value)
      sub(/[[:space:]]+$/, "", value)
      return value
    }
    /^[[:space:]]*\[/ {
      section = tolower(trim($0))
      in_ios_settings = (section == "[/script/iosruntimesettings.iosruntimesettings]")
      next
    }
    in_ios_settings && /^[[:space:]]*bUseSceneBasedLifecycle[[:space:]]*=/ {
      value = $0
      sub(/^[^=]*=/, "", value)
      sub(/[;#].*$/, "", value)
      enabled = (tolower(trim(value)) == "true")
    }
    END { exit(enabled ? 0 : 1) }
  ' "$project_root/Config/DefaultEngine.ini"
}

prepared_ios_engine_error() {
  printf '%s\n' \
    'Cinderline requires rebuilt ApplicationCore and Launch lifecycle modules plus the Core iOS tracing repair.' \
    'The stock precompiled engine does not contain these target-specific fixes.' \
    'Prepare the isolated engine, then retry:' \
    '  ./scripts/prepare-ios27-engine.py' \
    '  ./scripts/prepare-ios27-engine.py --check' >&2
  exit 2
}

validate_prepared_ios_engine() {
  local destination="${1:-$prepared_ios_engine}"
  local manifest="$destination/.cinderline-ios27-engine.json"
  if [[ ! -x "$ios_engine_preparer" || ! -f "$manifest" ]]; then
    prepared_ios_engine_error
  fi
  local source
  source="$(prepared_engine_source "$manifest")"
  if ! "$ios_engine_preparer" --engine "$source" --destination "$destination" --check; then
    printf '%s\n' 'The prepared iOS engine failed verification; repair or recreate it before building.' >&2
    exit 2
  fi
}

find_physical_ios_engine() {
  if [[ "$ue_root_is_explicit" == false && -f "$prepared_ios_engine/.cinderline-ios27-engine.json" ]]; then
    UE_ROOT="$prepared_ios_engine"
  elif [[ "$ue_root_is_explicit" == false ]] && ios_scene_lifecycle_enabled; then
    prepared_ios_engine_error
  fi

  find_engine
  case "$engine_root/" in
    "$project_root/"*)
      printf '%s\n' 'The iOS engine must be outside the project directory; nested engines produce incorrect packaged content paths.' >&2
      exit 2
      ;;
  esac
  if [[ -f "$engine_root/.cinderline-ios27-engine.json" ]]; then
    validate_prepared_ios_engine "$engine_root"
    using_prepared_ios_engine=true
  elif ios_scene_lifecycle_enabled && [[ -f "$engine_root/Engine/Build/InstalledBuild.txt" ]]; then
    printf '%s\n' \
      "UE_ROOT points to an installed engine without Cinderline's verified scene-lifecycle rebuild: $engine_root" \
      'Unset UE_ROOT to use the prepared sibling CinderlineEngineIOS27, or point UE_ROOT to a coherent source-built engine.' >&2
    prepared_ios_engine_error
  fi
}

build() {
  # Bash 3.2 treats an empty array expansion as unbound with set -u. Keep
  # one nonempty argv array, and fold explicit parallel flags into one value.
  local parallel_arg=-MaxParallelActions=2
  local build_arg
  for build_arg in "$@"; do
    if [[ "$build_arg" == -MaxParallelActions=* ]]; then parallel_arg="$build_arg"; fi
  done
  local build_args=(CinderlineEditor "$platform" Development "$project_file" -WaitMutex "$parallel_arg")
  if [[ "$using_prepared_metalfx_engine" == true ]]; then
    build_args+=(-ForceRulesCompile -SkipRulesCompile)
  fi
  for build_arg in "$@"; do
    if [[ "$build_arg" != -MaxParallelActions=* ]]; then build_args+=("$build_arg"); fi
  done
  "$build_script" "${build_args[@]}"
}
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

require_ios_payload() {
  local ios_target="$engine_root/Engine/Binaries/IOS/UnrealGame.target"
  if [[ ! -f "$ios_target" ]]; then
    printf '%s\n' "Unreal's iOS platform payload is missing: $ios_target" >&2
    printf '%s\n' 'In Epic Games Launcher, open Library -> UE 5.8 -> Options, enable iOS, apply the change, and wait for the download to finish.' >&2
    exit 2
  fi
}

prepare_mobile_uat_args() {
  mobile_uat_args=()
  local argument has_ubt_args=false has_additional_cooker_options=false
  for argument in "$@"; do
    case "$argument" in
      -[Uu][Bb][Tt][Aa][Rr][Gg][Ss]=*)
        has_ubt_args=true
        if [[ "$using_prepared_ios_engine" == true || "$using_prepared_metalfx_engine" == true ]]; then
          local normalized_ubt_args
          normalized_ubt_args="$(printf '%s' "${argument#*=}" | tr '[:upper:]' '[:lower:]')"
          if [[ " $normalized_ubt_args " != *" -forcerulescompile "* ]]; then
            argument="$argument -ForceRulesCompile"
          fi
          if [[ " $normalized_ubt_args " != *" -skiprulescompile "* ]]; then
            argument="$argument -SkipRulesCompile"
          fi
        fi
        ;;
      -[Aa][Dd][Dd][Ii][Tt][Ii][Oo][Nn][Aa][Ll][Cc][Oo][Oo][Kk][Ee][Rr][Oo][Pp][Tt][Ii][Oo][Nn][Ss]=*)
        has_additional_cooker_options=true
        ;;
    esac
    mobile_uat_args+=("$argument")
  done
  if [[ "$has_ubt_args" == false ]]; then
    if [[ "$using_prepared_ios_engine" == true || "$using_prepared_metalfx_engine" == true ]]; then
      mobile_uat_args+=("-ubtargs=-MaxParallelActions=2 -ForceRulesCompile -SkipRulesCompile")
    else
      mobile_uat_args+=("-ubtargs=-MaxParallelActions=2")
    fi
  fi
  if [[ "$has_additional_cooker_options" == false ]]; then
    local logical_cpu_count unused_shader_threads
    logical_cpu_count=""
    case "$(uname -s)" in
      Darwin) logical_cpu_count="$(sysctl -n hw.logicalcpu 2>/dev/null || true)" ;;
      Linux) logical_cpu_count="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)" ;;
    esac
    # Unreal retains at least one worker. Failed CPU discovery must not enable all cores.
    unused_shader_threads=1024
    if [[ "$logical_cpu_count" =~ ^[0-9]+$ ]] && (( logical_cpu_count > 0 )); then
      unused_shader_threads=$((logical_cpu_count - 1))
    fi
    mobile_uat_args+=("-AdditionalCookerOptions=-CookProcessCount=1 -ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=$unused_shader_threads -ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame=$unused_shader_threads")
  fi
}

is_ios_simulator_package() {
  local argument
  for argument in "$@"; do
    case "$argument" in
      -[Cc][Ll][Ii][Ee][Nn][Tt][Aa][Rr][Cc][Hh][Ii][Tt][Ee][Cc][Tt][Uu][Rr][Ee]=[Ii][Oo][Ss][Ss][Ii][Mm][Uu][Ll][Aa][Tt][Oo][Rr]) return 0 ;;
    esac
  done
  return 1
}

resolve_archive_directory() {
  mobile_archive_directory="$1"
  shift
  mobile_archive_args=("-archivedirectory=$mobile_archive_directory")
  local argument
  for argument in "$@"; do
    case "$argument" in
      -[Aa][Rr][Cc][Hh][Ii][Vv][Ee][Dd][Ii][Rr][Ee][Cc][Tt][Oo][Rr][Yy]=*)
        mobile_archive_directory="${argument#*=}"
        mobile_archive_args=()
        ;;
    esac
  done
  if [[ -z "$mobile_archive_directory" ]]; then
    printf '%s\n' 'The iOS archive directory cannot be empty.' >&2
    exit 2
  fi
}

run_mobile_command() {
  # Xcode retries can print the entire inherited environment. Pass only the
  # build environment; signing uses the keychain and local Unreal settings.
  local build_environment=("HOME=$HOME" "PATH=$PATH")
  local variable
  for variable in USER LOGNAME TMPDIR SHELL LANG LC_ALL DEVELOPER_DIR XCODE_XCCONFIG_FILE; do
    if [[ -n "${!variable:-}" ]]; then build_environment+=("$variable=${!variable}"); fi
  done
  env -i "${build_environment[@]}" "$@"
}

run_mobile_uat() {
  run_mobile_command "$engine_root/Engine/Build/BatchFiles/RunUAT.sh" "$@"
}

quarantine_existing_ios_app() {
  local source_app="$project_root/Binaries/IOS/Cinderline.app"
  local quarantine_root="$project_root/Saved/IOSDevice/Archives"
  python3 - "$project_root" "$source_app" "$quarantine_root" <<'PY'
from datetime import datetime, timezone
import os
from pathlib import Path
import sys


project_root = Path(sys.argv[1]).resolve(strict=True)
source_input = Path(os.path.abspath(sys.argv[2]))
quarantine_input = Path(os.path.abspath(sys.argv[3]))
expected_source = project_root / "Binaries/IOS/Cinderline.app"
expected_quarantine = project_root / "Saved/IOSDevice/Archives"

if source_input != expected_source or quarantine_input != expected_quarantine:
    raise SystemExit("error: refusing unexpected iOS bundle quarantine paths")


def reject_symlink_components(path: Path) -> None:
    relative = path.relative_to(project_root)
    current = project_root
    for component in relative.parts:
        current /= component
        if current.is_symlink():
            raise SystemExit(f"error: iOS bundle quarantine path must not contain a symbolic link: {current}")


reject_symlink_components(source_input)
reject_symlink_components(quarantine_input)
source = source_input.resolve()
quarantine_root = quarantine_input.resolve()
if source == quarantine_root or source in quarantine_root.parents or quarantine_root in source.parents:
    raise SystemExit("error: iOS source app and quarantine root must be separate, non-nested paths")

if not source_input.exists():
    print(f"No existing physical-iOS app to quarantine: {source_input}")
    raise SystemExit(0)
if not source_input.is_dir():
    raise SystemExit(f"error: existing physical-iOS output is not a regular app bundle: {source_input}")

quarantine_input.mkdir(parents=True, exist_ok=True)
reject_symlink_components(quarantine_input)
stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
destination = quarantine_input / f"build-{stamp}-{os.getpid()}-Cinderline.app"
if destination.exists() or destination.is_symlink():
    raise SystemExit(f"error: iOS bundle quarantine destination already exists: {destination}")
if source_input.stat().st_dev != quarantine_input.stat().st_dev:
    raise SystemExit("error: iOS bundle quarantine must remain on the same filesystem")

os.replace(source_input, destination)
print(f"Existing physical-iOS app quarantined before Xcode assembly: {destination}")
PY
}

case "$action" in
  help|-h|--help)
    printf '%s\n' 'Cinderline Unreal workflow (set UE_ROOT if automatic discovery is ambiguous):' \
      '  ./scripts/unreal.sh doctor       Show engine / toolchain availability' \
      '  ./scripts/unreal.sh doctor-ios   Validate and show the engine used for physical iOS builds' \
      '  ./scripts/unreal.sh build        Build CinderlineEditor' \
      '  ./scripts/unreal.sh build-ios   Build the physical-device ARM64 iOS binary and target receipt' \
      '                               Does not assemble or package; follow with package-ios -skipbuild' \
      '  ./scripts/unreal.sh build-ios-simulator' \
      '                               Build the ARM64 iOS Simulator target, without deployment' \
      '  ./scripts/unreal.sh bootstrap    Generate material and map after first build' \
      '  ./scripts/unreal.sh setup        Build, then bootstrap' \
      '  ./scripts/unreal.sh editor       Open the Unreal editor' \
      '  ./scripts/unreal.sh play         Run the native game in a standalone window' \
      '    Mac defaults to 2560x1440 framebuffer pixels; Linux defaults to 1280x720.' \
      '    Override with CINDERLINE_RES_X / CINDERLINE_RES_Y or -ResX=667 -ResY=375.' \
      '    CLI resolution values take precedence over environment values.' \
      '  ./scripts/unreal.sh package-ios  Cook/package development iOS for a physical device (requires signing)' \
      '  ./scripts/unreal.sh package-ios-on-mac' \
      '                               Cook/package Designed for iPad on Apple Silicon Mac; does not launch it' \
      '    Mobile builds default to two parallel compile actions; pass -ubtargs=... to override.' \
      '    Mobile packaging defaults to one cook process and one local shader worker on this Mac.' \
      '    Pass -AdditionalCookerOptions=... to override the cooker and shader-worker defaults.' \
      '    With scene lifecycle enabled, physical iOS actions require the verified sibling CinderlineEngineIOS27 clone.' \
      '    An explicit UE_ROOT is never replaced; installed engines without that rebuild are refused.' \
      '    Physical packaging quarantines the prior Binaries/IOS app so Xcode recreates and signs the full bundle.' \
      '    -clientarchitecture=iossimulator keeps normal engine discovery and skips physical archive replacement.' \
      '    Designed for iPad output: Saved/StagedBuilds/iOSonMac/Cinderline.app'
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
  doctor-ios)
    find_physical_ios_engine
    require_ios_payload
    printf 'Physical iOS engine: %s\nScene lifecycle: %s\n' "$engine_root" "$(ios_scene_lifecycle_enabled && printf enabled || printf disabled)"
    ;;
  build) find_engine; build "$@" ;;
  build-ios)
    find_physical_ios_engine
    if [[ "$platform" != Mac ]]; then
      printf '%s\n' 'Physical-device iOS builds require macOS.' >&2
      exit 2
    fi
    require_ios_payload
    ios_build_rule_args=()
    if [[ "$using_prepared_ios_engine" == true ]]; then
      ios_build_rule_args=(-ForceRulesCompile -SkipRulesCompile)
    fi
    run_mobile_command /usr/bin/env UE_BUILD_FROM_XCODE=1 \
      "$build_script" Cinderline IOS Development "$project_file" \
      -WaitMutex -SkipDeploy "$@" -Architecture=arm64 -MaxParallelActions=2 \
      "${ios_build_rule_args[@]}"
    printf '%s\n' \
      'Physical-device iOS binary and target receipt are ready.' \
      'Next run: ./scripts/unreal.sh package-ios -skipbuild'
    ;;
  build-ios-simulator)
    find_engine
    if [[ "$platform" != Mac || "$(uname -m)" != arm64 ]]; then
      printf '%s\n' 'The iOS Simulator target requires an Apple Silicon Mac.' >&2
      exit 2
    fi
    require_ios_payload
    "$build_script" Cinderline IOS Development "$project_file" \
      -Architecture=iossimulator -WaitMutex -SkipDeploy -MaxParallelActions=2 "$@"
    ;;
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
    package_is_simulator=false
    if is_ios_simulator_package "$@"; then
      package_is_simulator=true
      find_engine
      resolve_archive_directory "$project_root/Saved/Packages/IOSSimulator" "$@"
    else
      find_physical_ios_engine
      resolve_archive_directory "$project_root/Saved/Packages/IOS" "$@"
    fi
    require_ios_payload
    prepare_mobile_uat_args "$@"
    if [[ "$package_is_simulator" == false ]]; then
      quarantine_existing_ios_app
    fi
    run_mobile_uat BuildCookRun "-project=$project_file" \
      -noP4 -platform=IOS -clientconfig=Development -build -cook -stage -pak -package -archive \
      -target=Cinderline "${mobile_archive_args[@]}" "${mobile_uat_args[@]}"
    if [[ "$package_is_simulator" == false ]]; then
      "$ios_app_archiver" \
        "--source=$project_root/Binaries/IOS/Cinderline.app" \
        "--destination=$mobile_archive_directory/Cinderline.app" \
        "--quarantine-root=$project_root/Saved/IOSDevice/Archives"
    else
      printf '%s\n' 'Simulator package completed; no physical-device archive verification was applied.'
    fi
    ;;
  package-ios-on-mac)
    find_physical_ios_engine
    require_ios_payload
    if [[ "$platform" != Mac ]]; then
      printf '%s\n' 'Designed for iPad packaging requires macOS.' >&2
      exit 2
    fi
    if [[ "$(uname -m)" != arm64 ]]; then
      printf '%s\n' 'Designed for iPad packaging requires an Apple Silicon Mac.' >&2
      exit 2
    fi
    resolve_archive_directory "$project_root/Saved/Packages/IOSOnMac" "$@"
    prepare_mobile_uat_args "$@"
    quarantine_existing_ios_app
    run_mobile_uat BuildCookRun "-project=$project_file" \
      -noP4 -platform=IOS -clientconfig=Development -build -cook -stage -pak -package -archive \
      -target=Cinderline "${mobile_archive_args[@]}" -deploy -macnative \
      "-cmdline=-fulltouchcontrols" "${mobile_uat_args[@]}"
    ;;
  *) printf 'Unknown action: %s\n' "$action" >&2; exit 2 ;;
esac
