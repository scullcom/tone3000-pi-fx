#!/usr/bin/env bash
set -euo pipefail

usage() {
  printf '%s\n' 'Usage: bash install.sh [--with-guitarix | --without-guitarix]' \
    'Build and install TONE3000 FX for the current desktop user.' \
    'By default the installer asks whether to install Guitarix effects and artwork.'
}

die() { printf 'TONE3000 FX: %s\n' "$*" >&2; exit 1; }

guitarix=ask
case "${1:-}" in
  '') ;;
  --with-guitarix) guitarix=yes ;;
  --without-guitarix) guitarix=no ;;
  -h|--help) usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac
(( $# <= 1 )) || die 'Choose at most one option.'
(( EUID != 0 )) || die 'Run this as your desktop user. The installer uses sudo for packages.'
[[ -r /etc/os-release ]] || die 'This installer requires Raspberry Pi OS Trixie.'
. /etc/os-release
[[ "${VERSION_CODENAME:-}" == trixie ]] && [[ "$(dpkg --print-architecture)" == arm64 ]] ||
  die 'This installer requires 64-bit Raspberry Pi OS Trixie (arm64).'

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ -f "$source_dir/plugin/CMakeLists.txt" ]] || die 'Extract the complete GitHub source archive first.'
for file in 'Clean Trem and Verb.LICENSE.txt' 'Clean Trem and Verb.t3kpreset'; do
  [[ -f "$source_dir/resources/factory-presets/$file" ]] || die "Missing factory asset: $file"
done

if [[ "$guitarix" == ask ]]; then
  [[ -t 0 ]] || die 'For unattended use, choose --with-guitarix or --without-guitarix.'
  read -r -p 'Install Guitarix LV2 effects and download their pedal artwork? [Y/n] ' answer
  case "$answer" in
    ''|[Yy]|[Yy][Ee][Ss]) guitarix=yes ;;
    [Nn]|[Nn][Oo]) guitarix=no ;;
    *) die 'Answer yes or no, or use an explicit installer option.' ;;
  esac
fi

packages=(ca-certificates git build-essential cmake ninja-build pkg-config python3 zenity
          libgtk-3-dev libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfreetype-dev
          libx11-dev libxcomposite-dev libxcursor-dev libxi-dev libxinerama-dev
          libxrandr-dev libxrender-dev)
if [[ "$guitarix" == yes ]]; then
  packages+=(guitarix-lv2)
fi
missing=()
for package in "${packages[@]}"; do
  if [[ "$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null)" != installed ]]; then
    missing+=("$package")
  fi
done
if (( ${#missing[@]} )); then
  printf 'Required packages to install: %s\n' "${missing[*]}"
  sudo apt-get update
  sudo apt-get install -- "${missing[@]}"
fi

fetch_archive_submodule() {
  local path=$1 url=$2 revision=$3
  [[ -f "$path/CMakeLists.txt" ]] && return 0
  mkdir -p -- "$path"
  git -C "$path" init -q
  git -C "$path" fetch --depth 1 "$url" "$revision"
  git -C "$path" checkout --detach --quiet FETCH_HEAD
  git -C "$path" submodule update --init --recursive
}

if [[ -e "$source_dir/.git" ]]; then
  git -C "$source_dir" submodule update --init --recursive
else
  # GitHub ZIP/tar downloads omit submodule contents. Match this revision's gitlinks.
  fetch_archive_submodule "$source_dir/plugin/AudioDSPTools" \
    https://github.com/sdatkinson/AudioDSPTools.git 0827c6c2fc0deced568536142ea86f189e0b98a1
  fetch_archive_submodule "$source_dir/plugin/NeuralAmpModelerCore" \
    https://github.com/sdatkinson/NeuralAmpModelerCore.git 1f42f88535884450104b8711d7595019afa0495b
fi

publishable_key=${T3K_PUBLISHABLE_KEY:-$(sed -n 's/^T3K_PUBLISHABLE_KEY=\(t3k_pub_[A-Za-z0-9_-]*\)$/\1/p' "$source_dir/.env.example")}
[[ "$publishable_key" =~ ^t3k_pub_[A-Za-z0-9_-]+$ ]] ||
  die 'The documented public OAuth client ID is missing or invalid.'

build_key=$(printf '%s' "$source_dir" | sha256sum)
cache_home="${XDG_CACHE_HOME:-$HOME/.cache}/tone3000-pi-fx"
build_dir=${T3K_PI_BUILD_DIR:-"$cache_home/build-${build_key:0:12}"}
jobs=${T3K_PI_JOBS:-2}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || die 'T3K_PI_JOBS must be a positive integer.'
printf '%s\n' 'Building the ARM64 standalone. The first build can take several minutes.'
T3K_PUBLISHABLE_KEY="$publishable_key" T3K_UPDATE_NOTICE=false \
  cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
grep -Eq 'kPublishableKey.*"t3k_pub_[[:alnum:]_-]+"' "$build_dir/t3k_ui/T3kConfig.h" ||
  die 'CMake did not configure the TONE3000 sign-in client ID.'
cmake --build "$build_dir" --target TONE3000_Standalone --parallel "$jobs"

binary="$build_dir/plugin/TONE3000_artefacts/Release/Standalone/TONE3000"
[[ -x "$binary" ]] || die 'The build did not produce the standalone executable.'
if ldd "$binary" | grep 'not found'; then die 'The standalone is missing a runtime library.'; fi

data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
app="$data_home/tone3000-pi-fx"
profile="${XDG_CONFIG_HOME:-$HOME/.config}/tone3000-pi-fx/TONE3000"
factory="$profile/Presets/Factory"
bin_dir=${T3K_PI_BIN_DIR:-"$HOME/.local/bin"}
launcher="$bin_dir/tone3000-pi-fx"
mkdir -p -- "$app" "$factory" "$bin_dir" "$data_home/applications"

# Replace the application atomically. Existing user settings and presets are separate.
next=$(mktemp "$app/TONE3000.XXXXXX")
trap 'rm -f -- "$next"' EXIT
install -m 0755 -- "$binary" "$next"
mv -f -- "$next" "$app/TONE3000"

{
  printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail'
  printf 'export XDG_CONFIG_HOME=%q\n' "${profile%/TONE3000}"
  printf 'export XDG_DATA_HOME=%q\n' "$app"
  printf 'export XDG_CACHE_HOME=%q\n' "$cache_home"
  cat <<'LAUNCHER'
favorites="$XDG_CONFIG_HOME/TONE3000/lv2-favorites.txt"
if [[ -z "${T3K_LV2_FAVORITES:-}" && -r "$favorites" ]]; then
  T3K_LV2_FAVORITES=$(paste -sd ';' "$favorites")
  export T3K_LV2_FAVORITES
fi
exec "$XDG_DATA_HOME/TONE3000" "$@"
LAUNCHER
} > "$launcher"
chmod 0755 -- "$launcher"

# Keep the model's attribution next to the preset and preserve any previous copy.
for file in 'Clean Trem and Verb.LICENSE.txt' 'Clean Trem and Verb.t3kpreset'; do
  if [[ ! -e "$factory/$file" ]]; then
    install -m 0644 -- "$source_dir/resources/factory-presets/$file" "$factory/$file"
  fi
done

if [[ "$guitarix" == yes ]]; then
  if [[ ! -e "$profile/lv2-favorites.txt" ]]; then
    cat > "$profile/lv2-favorites.txt" <<'FAVORITES'
http://guitarix.sourceforge.net/plugins/gx_tremolo#_tremolo
http://guitarix.sourceforge.net/plugins/gx_reverb_stereo#_reverb_stereo
http://guitarix.sourceforge.net/plugins/gx_chorus_stereo#_chorus_stereo
http://guitarix.sourceforge.net/plugins/gx_digital_delay_st_#_digital_delay_st_
FAVORITES
  fi
  if ! python3 "$source_dir/script/install-guitarix-artwork.py" "$profile"; then
    printf '%s\n' 'Some artwork could not be downloaded. Effects still work. Run the installer again to retry.' >&2
  fi
fi

# Quote the launcher for desktop-entry Exec, including paths with spaces.
desktop_exec=$(python3 - "$launcher" <<'PY'
import sys
path = sys.argv[1]
if '\n' in path or '\r' in path:
    raise SystemExit('The launcher path must not contain a new line.')
special = {chr(92), chr(34), chr(36), chr(96)}
quoted = ''.join(chr(92) + ch if ch in special else ch for ch in path.replace('%', '%%'))
print('"'+quoted.replace('\\','\\\\')+'"')
PY
)
cat > "$data_home/applications/tone3000-pi-fx.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=TONE3000 FX
Comment=NAM, cabinet impulses, and LV2 guitar effects
Exec=$desktop_exec
Icon=audio-card
Terminal=false
Categories=AudioVideo;Audio;
DESKTOP
printf 'Installed. Open TONE3000 FX from the desktop menu or run:\n  %s\n' "$launcher"
