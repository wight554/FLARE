# FLARE

## What This Is

FLARE (Filament Lane Automation and Reload Engine) is a standalone dual-lane MMU / RELOAD controller firmware for the RP2040 microcontroller on the FYSETC ERB V2.0 board. It provides reliable filament management, automated runout failover reloading, and tight sync-feedback buffer control for 3D printing without requiring a dedicated Klipper plugin.

## Core Value

Autonomous, reliable dual-lane filament switching and reloading on runout with real-time sync-feedback buffer control.

## Requirements

### Validated

- ✓ Dual-lane TMC2209 stepper control over UART — v1.0
- ✓ Per-lane IN / OUT filament endstop switches and optional toolhead sensor — v1.0
- ✓ Type D dual-endstop and Type P analog / Hall-effect buffer sensor support — v1.0
- ✓ Serial CDC protocol at 115200 baud (`CMD:`, `OK:`, `ER:`, `EV:`) — v1.0
- ✓ RELOAD auto-switch mode (WAIT_Y → APPROACH → FOLLOW) — v1.0

### Active

- [ ] Complete host sync simulation coverage
- [ ] Buffer-lock state hardening and catch logic
- [ ] PSF runout escalation race condition fixes
- [ ] Flash wear visibility and persistence monitoring
- [ ] Live tuner & daemon Klipper mirror event-driven updates

### Out of Scope

- Klipper C-module plugin requirement — FLARE remains standalone via serial protocol and shell helpers
- More than 2 filament lanes on FYSETC ERB V2.0 — hardware boundary is 2 lanes

## Context

- Microcontroller: RP2040 on FYSETC ERB V2.0
- Drivers: TMC2209 stepper drivers (one per lane) communicating via UART
- Host: Klipper via `scripts/flare_cmd.py` and optional Unix socket daemon
- Build system: Pico SDK, CMake, Ninja, C99/C11 standards

## Constraints

- **Hardware**: FYSETC ERB V2.0 pin mapping and RP2040 flash memory bounds
- **Safety**: Motor safety lockouts, cutter feed timeouts, and buffer reserve safety floors must be enforced in hardware/firmware loops
- **Persistence**: Flash sector wear limits must be respected with versioned `settings_t` structs

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Standalone USB CDC serial protocol | Decouples firmware from host OS and Klipper updates | ✓ Good |
| Type-D and Type-P buffer sensing support | Accommodates both discrete dual-microswitch and continuous Hall sensors | ✓ Good |
| Centralized config in config.ini | Single source of truth generating tune.h across builds | ✓ Good |

---
*Last updated: 2026-09-11 after OpenSpec to GSD doc ingest*
