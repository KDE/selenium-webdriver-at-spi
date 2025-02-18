#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# SPDX-FileCopyrightText: 2025 Benjamin Port <benjamin.port@enioka.com>

set -euo pipefail

# Constants
readonly SELENIUM_DIR="/home/ubuntu/selenium-webdriver-at-spi"
readonly VENV_DIR="/home/ubuntu/venv"
readonly BUILD_DIR="/home/ubuntu/build"
readonly MAX_WAIT_TIME=30
readonly TEST_FILE_PATH="/home/ubuntu/test-file"

show_help() {
    cat << EOF
Usage: $0 {build-driver|run}

Commands:
  build-driver : build selenium-webdriver-at-spi
  run                    : Run test

Examples:
  $0 build-driver
  $0 run <snap name>
EOF
}

log_info() {
    echo "[INFO] $*"
}

log_error() {
    echo "[ERROR] $*" >&2
}

build_driver() {
    log_info "Building selenium-webdriver-at-spi..."
    cd "$SELENIUM_DIR"

    log_info "Creating Python virtual environment..."
    python3 -m venv --system-site-packages "$VENV_DIR"
    source "$VENV_DIR/bin/activate"

    log_info "Installing Python requirements..."
    python3 -m pip install -r requirements.txt

    log_info "Building with CMake..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake "$SELENIUM_DIR" -DQT_MAJOR_VERSION=6
    make

    log_info "Installing..."
    sudo make install
}

is_dbus_active() {
    systemctl status --user dbus 2>/dev/null | grep -q "active (running)"
}

wait_dbus() {
    local attempt=0

    log_info "Starting DBus..."
    systemctl --user start dbus

    while ! is_dbus_active; do
        ((attempt++))
        if [ "$attempt" -ge "$MAX_WAIT_TIME" ]; then
            log_error "DBus failed to start after $MAX_WAIT_TIME seconds"
            return 1
        fi
        log_info "Waiting for DBus to start... (attempt $attempt/$MAX_WAIT_TIME)"
        sleep 1
    done
}

check_a11y_bus() {
    log_info "Checking accessibility bus..."
    if ! dbus-send --print-reply=literal --dest=org.a11y.Bus /org/a11y/bus org.a11y.Bus.GetAddress; then
        log_error "Failed to get accessibility bus address"
        return 1
    fi
}

launch_openbox() {
    if pgrep openbox >/dev/null; then
        log_info "Openbox already running"
        return 0
    fi

    log_info "Launching Openbox..."
    nohup sudo startx >/dev/null 2>&1 &

    log_info "Waiting for X server..."
    sleep 1

    log_info "Allow ubuntu user to access X socket"
    DISPLAY=:0 sudo xhost +
}

run() {
    if [ "$#" -lt 1 ]; then
        log_error "Missing snap name parameter"
        show_help
        return 1
    fi

    local snap_name="$1"

    launch_openbox
    wait_dbus
    check_a11y_bus

    log_info "Running tests for $snap_name"
    cd "$SELENIUM_DIR"
    source "$VENV_DIR/bin/activate"

    chmod +x "$TEST_FILE_PATH"

    SELENIUM_OVERRIDE_LAUNCH="snap run ${snap_name}" selenium-webdriver-at-spi-run "$TEST_FILE_PATH"
}

main() {
    if [ "$#" -lt 1 ]; then
        show_help
        exit 1
    fi

    local command="$1"
    shift

    case "$command" in
        build-driver)
            build_driver "$@"
            ;;
        run)
            run "$@"
            ;;
        *)
            show_help
            exit 1
            ;;
    esac
}

main "$@"
