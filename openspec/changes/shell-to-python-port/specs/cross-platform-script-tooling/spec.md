# cross-platform-script-tooling Specification

## Purpose
Cross-platform Python host script contract — all operational scripts in `scripts/` SHALL be Python, working on Linux (Raspberry Pi, Debian/Ubuntu) and macOS without modification.

## Requirements

### Requirement: All operational scripts SHALL be Python

Every operational script in `scripts/` SHALL be implemented in Python using only stdlib + pyserial. Bash `.sh` files MAY exist only as deprecated forwarding wrappers.

#### Scenario: New script is added

- **WHEN** a contributor adds new operational script to `scripts/`
- **THEN** it SHALL be a `.py` file using only stdlib + pyserial
- **AND** it SHALL work on both Linux and macOS

#### Scenario: Deprecated wrapper invocation

- **WHEN** a user runs `bash scripts/foo.sh`
- **THEN** wrapper forwards to `python3 scripts/foo.py` with all arguments
- **AND** exit code is preserved

### Requirement: Linux and macOS compatibility

Each ported script SHALL produce identical functional behavior on Linux (Raspberry Pi / Debian / Ubuntu / Fedora) and macOS. Platform-specific operations (device discovery, mount, `diskutil`) SHALL be branched via `platform.system()`.

#### Scenario: Flash script on macOS

- **WHEN** `python3 scripts/flash_flare.py` runs on macOS
- **THEN** RPI-RP2 device discovery uses `diskutil` and `/Volumes/RPI-RP2`
- **AND** no `lsblk` or `sudo mount` calls are made

#### Scenario: Flash script on Linux

- **WHEN** `python3 scripts/flash_flare.py` runs on Linux
- **THEN** RPI-RP2 device discovery uses `lsblk` and standard mount paths
- **AND** no `diskutil` calls are made

#### Scenario: Install daemon on macOS

- **WHEN** `python3 scripts/install_daemon.py` runs on macOS
- **THEN** script detects non-Linux platform and exits with clear error explaining systemd requirement

### Requirement: No inline shell Python heredocs

Scripts SHALL NOT embed Python code inside bash heredocs. Serial I/O, device communication, and other Python operations SHALL use direct imports from shared modules (`serial_utils`, `path_utils`).

#### Scenario: Flash script serial operations

- **WHEN** `flash_flare.py` triggers BOOTSEL or verifies firmware version
- **THEN** it uses direct `import serial` and `from serial_utils import find_port`
- **AND** no subprocess spawning of inline Python code occurs

### Requirement: Color output with terminal detection

Scripts with colored terminal output SHALL detect non-interactive terminals and `NO_COLOR` environment variable, disabling ANSI escape sequences when appropriate.

#### Scenario: Piped output

- **WHEN** script stdout is piped or `NO_COLOR` is set
- **THEN** output contains no ANSI escape sequences

#### Scenario: Interactive terminal

- **WHEN** script runs in interactive terminal without `NO_COLOR`
- **THEN** output uses ANSI color codes for status/error differentiation
