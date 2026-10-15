# Arch dependency helpers sourced by install.sh; use its log, run, and fail helpers.
# Package snapshots limit cleanup to dependencies introduced by this installation.

whatly_install_dependencies() {
    local ID= ID_LIKE=
    [[ -r /etc/os-release ]] || fail 'Cannot detect the system. Prepare dependencies and use --skip-deps.'
    source /etc/os-release
    [[ "$ID" == arch || " $ID_LIKE " == *' arch '* ]] ||
        fail 'Automatic dependencies require Arch Linux. Prepare dependencies and use --skip-deps.'
    command -v pacman >/dev/null 2>&1 || fail 'pacman is required to install Arch dependencies.'

    local -a runtime_dependencies=(
        qt6-base qt6-webengine qt6-webchannel qt6-positioning qt6-svg qt6-wayland
        libx11 libxcb tar gzip xdg-utils
    )
    local -a build_dependencies=(git cmake ninja base-devel qt6-tools)
    local -a missing=()
    local -a new_build=()
    local -A before=()
    local -A after=()
    local snapshot package
    declare -gA whatly_added_packages=()
    whatly_cleanup_ready=false

    if ! snapshot=$(pacman -Qq); then
        fail 'Cannot inspect installed packages before dependency setup.'
    fi
    while IFS= read -r package; do
        if [[ -n "$package" ]]; then
            before["$package"]=1
        fi
    done <<< "$snapshot"

    for package in "${runtime_dependencies[@]}" "${build_dependencies[@]}"; do
        if [[ -z "${before[$package]+present}" ]]; then
            missing+=("$package")
        fi
    done
    if (( ${#missing[@]} == 0 )); then
        log INFO 'All build and runtime dependencies are already installed.'
        return 0
    fi

    command -v sudo >/dev/null 2>&1 || fail 'sudo is required for missing dependencies.'
    log INFO "Installing missing dependencies: ${missing[*]}"
    log WARN 'The pacman -Syu transaction may also upgrade existing system packages.' >&2
    local -a options=(-Syu --needed)
    if [[ "$non_interactive" == true ]]; then
        options+=(--noconfirm)
    fi

    # --asdeps on -Syu would also change existing upgraded packages' reasons.
    # Install normally, then change only the new build targets to dependencies.
    # Newly requested runtime roots stay explicit because Whatly is not in pacman.
    run sudo pacman "${options[@]}" -- "${missing[@]}" || return "$?"
    if ! snapshot=$(pacman -Qq); then
        fail 'Cannot inspect installed packages after dependency setup.'
    fi
    while IFS= read -r package; do
        if [[ -n "$package" ]]; then
            after["$package"]=1
            if [[ -z "${before[$package]+present}" ]]; then
                whatly_added_packages["$package"]=1
            fi
        fi
    done <<< "$snapshot"

    for package in "${runtime_dependencies[@]}" "${build_dependencies[@]}"; do
        [[ -n "${after[$package]+present}" ]] || fail "Required dependency is still missing: $package"
    done
    for package in "${build_dependencies[@]}"; do
        if [[ -z "${before[$package]+present}" ]]; then
            new_build+=("$package")
        fi
    done
    if (( ${#new_build[@]} > 0 )); then
        run sudo pacman -D --asdeps -- "${new_build[@]}" || return "$?"
    fi

    # Keep runtime roots out of the removal set even if their reason is changed
    # externally during a build. Their installed dependency closure then stays in use.
    for package in "${runtime_dependencies[@]}"; do
        unset 'whatly_added_packages[$package]'
    done
    whatly_cleanup_ready=true
}

whatly_cleanup_build_dependencies() {
    [[ "${whatly_cleanup_ready:-false}" == true ]] || return 0
    whatly_cleanup_ready=false
    local output package status
    local removed=0
    local -a candidates=()
    local -a options=(-R)
    if [[ "$non_interactive" == true ]]; then
        options+=(--noconfirm)
    fi

    # Each successful round removes names from this finite snapshot. Rechecking
    # orphans exposes new transitive build dependencies without recursive removal,
    # which could otherwise also delete dependencies present before installation.
    while (( ${#whatly_added_packages[@]} > 0 )); do
        if output=$(pacman -Qdtq); then
            :
        else
            status=$?
            if (( status == 1 )) && [[ -z "$output" ]]; then
                break
            fi
            log WARN 'Could not inspect unused build dependencies; Whatly is installed and dependency cleanup was skipped.' >&2
            return 0
        fi

        candidates=()
        while IFS= read -r package; do
            if [[ -n "$package" && -n "${whatly_added_packages[$package]+introduced}" ]]; then
                candidates+=("$package")
            fi
        done <<< "$output"
        if (( ${#candidates[@]} == 0 )); then
            break
        fi

        log INFO "Removing temporary build dependencies: ${candidates[*]}"
        if ! run sudo pacman "${options[@]}" -- "${candidates[@]}"; then
            log WARN 'Build dependency removal failed; Whatly is installed and the remaining dependencies were kept.' >&2
            return 0
        fi
        for package in "${candidates[@]}"; do
            unset 'whatly_added_packages[$package]'
            removed=$((removed + 1))
        done
    done

    if (( removed > 0 )); then
        log OK "Removed $removed temporary build dependencies."
    fi
    return 0
}
