#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# SPDX-FileCopyrightText: 2025 Benjamin Port <benjamin.port@enioka.com>

set -euo pipefail

# Constants
readonly DEFAULT_INSTANCE_NAME="kde-appium-test-vm"
readonly INSTANCE_NAME="${KDE_APPIUM_VM_NAME:-$DEFAULT_INSTANCE_NAME}"
readonly VM_IMAGE="ubuntu:24.10"
readonly VM_CPU=4
readonly VM_MEMORY="4GiB"
readonly REQUIRED_PACKAGES=(
    accerciser
    at-spi2-core
    cmake
    extra-cmake-modules
    g++
    gcc
    git
    gobject-introspection
    kwayland-dev
    kwin-wayland
    libcairo2-dev
    libgirepository1.0-dev
    libkf6coreaddons-dev
    libkf6windowsystem-dev
    libkpipewire-dev
    libwayland-dev
    libxkbcommon-dev
    openbox
    pipewire
    plasma-wayland-protocols
    python3-cairo
    python3-pip
    python3-venv
    qt6-base-private-dev
    qt6-wayland-dev
    ruby
    wireplumber
    x11-xserver-utils
    xdg-desktop-portal-kde
    xinit
)

# Environment variables to be added to profile
readonly ENV_VARS=(
    "export QT_QPA_PLATFORM=wayland"
    "export DISPLAY=:0"
    "export USE_CUSTOM_BUS=0"
)

show_help() {
    cat << EOF
Usage: $0 {create|build-driver|run|run-file|delete|console}

Commands:
  create       : Create vm and setup everything to run selenium test
  build-driver : Build selenium-webdriver-at-spi
  run          : Run a test
  run-file     : Run test from a file listing them (each line contains <snap path> <snap name> <test file url>)
  delete       : Delete the vm
  console      : Launch a VGA console attached to the VM

Environment Variables:
  KDE_APPIUM_VM_NAME : VM instance name (default: $DEFAULT_INSTANCE_NAME)

Examples:
  $0 create
  $0 build-driver
  $0 run <snap path> <snap name> <test file path>
  $0 run-file <file-path>
  $0 delete
  $0 console
EOF
}

is_vm_running() {
    lxc info "$INSTANCE_NAME" 2>/dev/null | grep -q "RUNNING"
}

wait_vm_ready() {
    local max_attempts=180
    local attempt=0

    if ! is_vm_running; then
        echo "Error: VM '$INSTANCE_NAME' not started" >&2
        return 1
    fi

    while ! lxc exec "$INSTANCE_NAME" -- sh -c "echo 'VM ready'" &>/dev/null; do
        ((attempt++))
        if [ "$attempt" -ge "$max_attempts" ]; then
            echo "Error: VM startup timeout after $max_attempts attempts" >&2
            return 1
        fi
        echo "Waiting for VM to start... (attempt $attempt/$max_attempts)"
        sleep 1
    done
}

wait_dbus_user_ready() {
    local max_attempts=30
    local attempt=0

    while ! exec_user "env | grep -q DBUS_SESSION_BUS_ADDRESS"; do
        ((attempt++))
        if [ "$attempt" -ge "$max_attempts" ]; then
            echo "Error: DBus startup timeout after $max_attempts attempts" >&2
            return 1
        fi
        echo "Waiting for DBus to be ready... (attempt $attempt/$max_attempts)"
        sleep 1
    done
}

exec_user() {
    lxc exec "$INSTANCE_NAME" -- sudo --login --user ubuntu bash -lc "$*"
}

run_remote() {
    exec_user "/home/ubuntu/selenium-webdriver-at-spi/snaptests/snap-test-in-vm-companion.sh $*"
}

exec_root() {
    lxc exec "$INSTANCE_NAME" -- "$@"
}

create() {
    local script_path
    script_path="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    echo "Creating VM instance..."
    lxc launch "$VM_IMAGE" --vm \
        -c limits.cpu="$VM_CPU" \
        -c limits.memory="$VM_MEMORY" \
        "$INSTANCE_NAME"

    wait_vm_ready || exit 1

    echo "Updating system packages..."
    exec_root bash -c "apt update && apt upgrade -y"

    echo "Installing required packages..."
    exec_root apt install -qy "${REQUIRED_PACKAGES[@]}"

    lxc config device add "$INSTANCE_NAME" selenium-dir disk source="$script_path" path=/home/ubuntu/selenium-webdriver-at-spi/ readonly=true

    echo "Configuring environment..."
    printf '%s\n' "${ENV_VARS[@]}" | exec_user bash -c 'cat >> /home/ubuntu/.profile'


    echo "Mounting selenium-webdriver-at-spi..."
    build-driver
}

build-driver() {
    echo "Building selenium-webdriver..."
    run_remote build-driver
}

install_snap() {
    local snap_path="$1"

    echo "Installing snap package..."
    lxc file push "$snap_path" "$INSTANCE_NAME/home/ubuntu/snap-to-test"
    exec_user "sudo snap install --dangerous /home/ubuntu/snap-to-test"
}

run() {
    if [ "$#" -ne 3 ]; then
        echo "Error: run command requires 3 arguments" >&2
        show_help
        return 1
    fi

    local snap_path="$1"
    local snap_name="$2"
    local test_file_path="$3"

    if ! is_vm_running; then
        echo "Starting VM..."
        lxc start "$INSTANCE_NAME"
    fi

    wait_vm_ready || exit 1
    wait_dbus_user_ready || exit 1

    install_snap "$snap_path"

    echo "Copying test file..."
    lxc file push "$test_file_path" "$INSTANCE_NAME/home/ubuntu/test-file"

    echo "Running tests..."
    run_remote run "$snap_name"
}

run-file() {
    readonly TEST_LIST_FILE="$1"
    if [ "$#" -ne 1 ]; then
        echo "Error: run-file command requires 1 arguments" >&2
        show_help
        return 1
    fi
    if [ ! -f "$TEST_LIST_FILE" ]; then
        echo "Error: Test list file not found: $TEST_LIST_FILE"
        exit 1
    fi

    while IFS=' ' read -r snap_path app_name test_url || [ -n "$snap_path" ]; do
        [[ -z "$snap_path" || "$snap_path" =~ ^# ]] && continue

        local test_file="/tmp/test_file"
        # Check if test_path is a URL or local file
        if [[ "$test_url" =~ ^https?:// ]]; then
            # It's a URL, download it
            if ! wget -q -O "$test_file" "$test_url"; then
                echo "Error: Failed to download test file from $test_url"
                continue
            fi
        else
            # It's a local file path, check if it exists
            if [ ! -f "$test_url" ]; then
                echo "Error: Local test file not found: $test_url"
                continue
            fi
            # Copy the local file to temp location
            cp "$test_url" "$test_file"
        fi

        run "$snap_path" "$app_name" "$test_file"
        rm "$test_file"
    done < "$TEST_LIST_FILE"
}

console() {
    wait_vm_ready || exit 1
    lxc console "$INSTANCE_NAME" --type vga
}

delete() {
    if lxc info "$INSTANCE_NAME" &>/dev/null; then
        echo "Deleting VM instance..."
        lxc delete -f "$INSTANCE_NAME"
    else
        echo "VM instance does not exist"
    fi
}

main() {
    if [ "$#" -lt 1 ]; then
        show_help
        exit 1
    fi

    local command="$1"
    shift

    case "$command" in
        create|build-driver|run|run-file|delete|console)
            "$command" "$@"
            ;;
        *)
            show_help
            exit 1
            ;;
    esac
}

main "$@"
