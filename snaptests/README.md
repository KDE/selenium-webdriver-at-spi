<!--
    SPDX-License-Identifier: CC0-1.0
    SPDX-FileCopyrightText: 2025 Benjamin Port <benjamin.port@enioka.com>
-->

# Snap Testing in VM

Tools to run automated tests for snap packages in a dedicated VM environment using lxd and selenium-web-driver-at-spi.

## How that works

- `snap-test-in-vm.sh` manages the VM and test orchestration
- `snap-test-in-vm-companion.sh` runs inside the VM to execute tests.

Using a dedicated VM allow to isolate test and prevent breaking host machine (dbus for example).

Tests are run in a VM that can run in headless mode for CI for example. For debugging you can open a console attached to the vm by invoking `./snap-test-in-vm.sh console`

## Usage

```bash
# Create VM and setup testing environment
./snap-test-in-vm.sh create [selenium-web-driver-at-spi branch]

# Update selenium-webdriver-at-spi in VM
./snap-test-in-vm.sh update-driver [selenium-web-driver-at-spi branch]

# Run test for a snap
./snap-test-in-vm.sh run <snap-file.snap> <snap-name> <test-file.py>

# Run tests for a snap
## file example
## kwrite.snap kwrite https://invent.kde.org/utilities/kate/-/raw/c932831e07f8e78607fc17f3a2d60ba7d18e19b4/appiumtests/kwritetest.py
## ...
./snap-test-in-vm.sh run-file <file>


# Access VM console (optional)
./snap-test-in-vm.sh console

# Clean up
./snap-test-in-vm.sh delete
```

You can set KDE_APPIUM_VM_NAME environment variable to override the VM name
