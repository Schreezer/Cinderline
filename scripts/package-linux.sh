#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
build_dir="${CINDERLINE_LINUX_BUILD_DIR:-$project_dir/build/linux-package}"
package_root="${CINDERLINE_LINUX_PACKAGE_DIR:-$project_dir/artifacts/Cinderline-Ubuntu-x86_64}"
archive="$project_dir/artifacts/Cinderline-Ubuntu-x86_64.tar.gz"

if [[ "$(uname -s)" != Linux ]]; then
  printf '%s\n' 'package-linux.sh must run in a Linux build environment.' >&2
  exit 2
fi

cmake -S "$project_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCINDERLINE_BUILD_NATIVE=OFF \
  -DCINDERLINE_BUILD_LINUX=ON \
  -DCINDERLINE_BUILD_SERVER=OFF \
  -DBUILD_TESTING=ON
cmake --build "$build_dir" --parallel "${CINDERLINE_BUILD_JOBS:-4}"

ctest --test-dir "$build_dir" --output-on-failure

mkdir -p "$package_root/bin" "$package_root/lib" "$package_root/share"
install -m 0755 "$build_dir/CinderlineLinux" "$package_root/bin/cinderline"
install -m 0644 /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "$package_root/share/DejaVuSans.ttf"

while IFS= read -r library; do
  [[ -f "$library" ]] || continue
  case "$(basename -- "$library")" in
    libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libstdc++.so.*|libgcc_s.so.*|ld-linux*.so.*) ;;
    *) cp -L "$library" "$package_root/lib/" ;;
  esac
done < <(ldd "$build_dir/CinderlineLinux" | awk '/=> \/|^\// { for (i=1; i<=NF; ++i) if ($i ~ /^\//) print $i }')

install -m 0755 "$project_dir/scripts/run-cinderline-linux.sh" "$package_root/Cinderline.sh"
install -m 0644 "$project_dir/docs/UBUNTU_PLAYTEST.txt" "$package_root/README.txt"

tar -C "$project_dir/artifacts" -czf "$archive" "$(basename -- "$package_root")"
(
  cd -- "$(dirname -- "$archive")"
  sha256sum "$(basename -- "$archive")" > "$(basename -- "$archive").sha256"
)

printf '%s\n' "$archive"
