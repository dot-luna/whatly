#!/usr/bin/env bash
# Build this native Whatly checkout and install it for the current user.
# Run from any directory; only missing Arch dependencies require sudo.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: bash scripts/install.sh [options]

Build Whatly with CMake and Ninja, then install it into ~/.local.

  --prefix PATH       Absolute installation prefix (default: ~/.local)
  --non-interactive   Accept package-manager confirmations automatically
  --skip-deps         Use prepared dependencies without invoking pacman
  -h, --help          Show this help

Missing dependencies are installed in one Arch package transaction. Sudo may
still request your password. Build and installation run as your normal user.
After success, newly added unused build dependencies are removed; runtime and
preexisting packages are kept.
CMAKE_BUILD_PARALLEL_LEVEL controls build jobs (default: 2).
EOF
}

# Keep labels readable when invoked directly or through another installer's tee.
color=${LUNA_COLOR:-auto}
if [[ -n "${NO_COLOR:-}" ]]; then
    color=0
elif [[ "$color" == auto ]]; then
    if [[ -t 1 && "${TERM:-}" != dumb ]]; then
        color=1
    else
        color=0
    fi
fi

log() {
    local level=$1
    shift
    local tint=
    local reset=
    if [[ "$color" == 1 ]]; then
        reset=$'\e[0m'
        case "$level" in
            OK)
                tint=$'\e[32m'
                ;;
            WARN)
                tint=$'\e[1;33m'
                ;;
            ERROR)
                tint=$'\e[1;31m'
                ;;
            *)
                tint=$'\e[36m'
                ;;
        esac
    fi
    printf '%s[Whatly] [%s]%s %s\n' "$tint" "$level" "$reset" "$*"
}

fail() {
    log ERROR "$*" >&2
    exit 1
}

tool_output() {
    # Match terminal escape bytes consistently without changing the tool's locale.
    local LC_ALL=C
    local line
    local ansi=$'\e''\[[0-?]*[ -/]*[@-~]'
    while IFS= read -r line || [[ -n "$line" ]]; do
        while [[ "$line" =~ $ansi ]]; do
            line=${line/"${BASH_REMATCH[0]}"/}
        done
        if [[ "$color" == 1 ]]; then
            printf '\e[90m  %s\e[0m\n' "$line"
        else
            printf '  %s\n' "$line"
        fi
    done
}

# Preserve command failures across the output filter; stdin remains available to sudo.
run() {
    local -a statuses
    if "$@" 2>&1 | tool_output; then
        statuses=("${PIPESTATUS[@]}")
    else
        statuses=("${PIPESTATUS[@]}")
    fi
    if (( statuses[0] != 0 )); then
        return "${statuses[0]}"
    fi
    return "${statuses[1]}"
}

install_failed() {
    local install_status=$?
    log ERROR "Installation stopped (exit $install_status)." >&2
    exit "$install_status"
}

cleanup_desktop_temp() {
    if [[ -n "${desktop_tmp:-}" ]]; then
        rm -f -- "$desktop_tmp"
    fi
}

prefix=${HOME:+$HOME/.local}
non_interactive=false
skip_deps=false

# Validate all options before touching dependencies, the checkout, or the prefix.
while (( $# > 0 )); do
    case "$1" in
        --prefix)
            (( $# >= 2 )) || fail '--prefix requires an absolute path.'
            [[ "$2" == /* ]] || fail '--prefix requires an absolute path.'
            prefix=$2
            shift 2
            ;;
        --non-interactive)
            non_interactive=true
            shift
            ;;
        --skip-deps)
            skip_deps=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "Unknown option: $1"
            ;;
    esac
done

[[ "$prefix" == /* ]] || fail 'Set HOME or provide --prefix with an absolute path.'
[[ "$prefix" != *'='* && "$prefix" != *$'\n'* && "$prefix" != *$'\r'* ]] ||
    fail 'The desktop launcher requires a prefix without equals signs or line breaks.'
(( EUID != 0 )) || fail 'Run this installer as a normal user; sudo is used only for dependencies.'
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail 'CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer.'

script_path=$(readlink -f -- "${BASH_SOURCE[0]}")
source_dir=$(dirname -- "$(dirname -- "$script_path")")
build_dir="$source_dir/build-native"
[[ -f "$source_dir/CMakeLists.txt" && -e "$source_dir/.git" ]] ||
    fail 'Run scripts/install.sh from a Git checkout of Whatly.'

trap install_failed ERR
log INFO "Installing native Whatly into $prefix."
if [[ "$skip_deps" == false ]]; then
    source "$source_dir/scripts/dependencies.sh"
    whatly_install_dependencies
fi

for tool in git cmake ninja; do
    command -v "$tool" >/dev/null 2>&1 || fail "Missing build tool: $tool"
done
mkdir -p -- "$prefix"
[[ -w "$prefix" ]] || fail "Installation prefix is not writable: $prefix"

if [[ "$non_interactive" == true ]]; then
    export GIT_TERMINAL_PROMPT=0
fi
log INFO 'Preparing submodules.'
run git -C "$source_dir" submodule update --init --recursive

# Configure the prefix before building: some install destinations are absolute.
log INFO 'Configuring the Release build.'
run cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix"
log INFO "Building Whatly with $jobs parallel jobs."
run cmake --build "$build_dir" --parallel "$jobs"
log INFO 'Installing the executable and desktop files.'
run cmake --install "$build_dir"
[[ -x "$prefix/bin/whatly" ]] || fail 'The build finished without installing the Whatly executable.'

# Desktop sessions may not inherit a newly configured shell PATH. Keep distro
# packaging unchanged and point only this user's installed launcher at the binary.
desktop_file="$prefix/share/applications/net.shakaran.whatly.desktop"
[[ -f "$desktop_file" ]] || fail 'The build did not install the desktop launcher.'
desktop_binary="$prefix/bin/whatly"

# Exec quoting happens after desktop-entry string unescaping: escape command
# characters first, then double backslashes for the outer string representation.
desktop_binary=${desktop_binary//\\/\\\\}
desktop_binary=${desktop_binary//\"/\\\"}
desktop_binary=${desktop_binary//\$/\\\$}
desktop_binary=${desktop_binary//\`/\\\`}
desktop_binary=${desktop_binary//%/%%}
desktop_binary=${desktop_binary//\\/\\\\}
desktop_binary=${desktop_binary//$'\t'/\\t}
desktop_command="\"$desktop_binary\""
# GLib checks executable existence before expanding %%; env lets that expansion
# happen in an argument while still executing the absolute binary without a shell.
if [[ "$prefix" == *'%'* ]]; then
    desktop_command="/usr/bin/env $desktop_command"
fi

desktop_tmp=$(mktemp "$desktop_file.XXXXXX")
trap cleanup_desktop_temp EXIT
while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
        'Exec=whatly'|'Exec=whatly '*)
            printf 'Exec=%s%s\n' "$desktop_command" "${line#Exec=whatly}"
            ;;
        *)
            printf '%s\n' "$line"
            ;;
    esac
done < "$desktop_file" > "$desktop_tmp"
chmod 644 "$desktop_tmp"
mv -- "$desktop_tmp" "$desktop_file"
desktop_tmp=

# Desktop caches are optional; their absence or failure must not undo installation.
if [[ -d "$prefix/share/applications" ]] && command -v update-desktop-database >/dev/null 2>&1; then
    if ! run update-desktop-database "$prefix/share/applications"; then
        log WARN 'Desktop cache refresh failed; the application is installed.' >&2
    fi
fi
if [[ -d "$prefix/share/icons/hicolor" ]] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
    if ! run gtk-update-icon-cache --force --ignore-theme-index "$prefix/share/icons/hicolor"; then
        log WARN 'Icon cache refresh failed; the application is installed.' >&2
    fi
fi

if [[ "$skip_deps" == false ]]; then
    whatly_cleanup_build_dependencies
fi

log OK "Installed: $prefix/bin/whatly"
case ":${PATH:-}:" in
    *":$prefix/bin:"*)
        ;;
    *)
        log INFO "Add $prefix/bin to PATH to launch Whatly by name in a terminal."
        ;;
esac
