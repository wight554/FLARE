## ADDED Requirements

### Requirement: Injectable retract guard primitives
`flare_mmu.cfg` SHALL provide `_FLARE_RETRACT_GUARD_BEGIN` and
`_FLARE_RETRACT_GUARD_END` so a buffer lock can be wrapped around a retract that
lives inside a macro FLARE does not own, without editing that macro.

`_FLARE_RETRACT_GUARD_BEGIN` SHALL arm a tension buffer lock and return only
once the firmware reports it established. It SHALL NOT issue a `BS` first: `BS`
parks the buffer at `BUF_GOAL`, which is on the compression side, maximizing the
prime distance the following `BL:T` must undo. `_FLARE_RETRACT_GUARD_END` SHALL
issue the closing `BS` and `_FLARE_SYNC_TOOLHEAD`.

`LENGTH`, `SPEED`, and `TIMEOUT` SHALL be optional parameters defaulting to
`_FLARE_VARS.print_end_retract_len` (mm), `_FLARE_VARS.print_end_retract_speed`
(mm/s), and 120 seconds. `_FLARE_VARS` SHALL expose the first two, documented as
needing to match the wrapped macro's own retract.

`LENGTH` SHALL be the true retract length, passed to `BL` unmodified: the
firmware subtracts `BUF_MAX_TRAVEL_MM / 2` by design so the follow parks the
buffer at NEUTRAL rather than at the armed rail. The macros SHALL NOT pad it.

`flare_mmu.cfg` SHALL NOT define any macro named for a host print-lifecycle
hook (`END_PRINT`, `CANCEL_PRINT`, `PRINT_END`, or similar), so the wrapper
recipe can never collide with the operator's existing configuration or with
another plugin that wraps the same names.

#### Scenario: Guard wraps a foreign macro
- **WHEN** the operator renames `END_PRINT` via `rename_existing` and calls
  `_FLARE_RETRACT_GUARD_BEGIN`, the renamed macro, then
  `_FLARE_RETRACT_GUARD_END`
- **THEN** the buffer lock is established before the wrapped macro runs
- **AND** the MMU follows the retract inside it under the buffer-lock catch
- **AND** the lock is released by the closing `BS`
- **AND** no `EV:BL:TIMEOUT` is emitted

#### Scenario: True retract length is passed unpadded
- **WHEN** `_FLARE_RETRACT_GUARD_BEGIN LENGTH=40` is called with a 16 mm buffer
- **THEN** the `BL` follow distance argument is 40
- **AND** the firmware follows 32 mm, leaving the buffer at NEUTRAL after a
  40 mm extruder retract

#### Scenario: Parameters override the variable defaults
- **WHEN** `_FLARE_RETRACT_GUARD_BEGIN LENGTH=40 SPEED=35` is called
- **THEN** the guard is sized for a 40 mm, 35 mm/s retract
- **AND** `_FLARE_VARS.print_end_retract_len` / `print_end_retract_speed` are
  not consulted

#### Scenario: Bare call uses the variable defaults
- **WHEN** `_FLARE_RETRACT_GUARD_BEGIN` is called with no parameters
- **THEN** it uses `_FLARE_VARS.print_end_retract_len` and
  `print_end_retract_speed`

#### Scenario: Guard sets a watchdog window for the wrapped macro
- **WHEN** `_FLARE_RETRACT_GUARD_BEGIN` is called with no `TIMEOUT`
- **THEN** the `BL` command carries a 120 second timeout
- **AND** a wrapped macro that parks, retracts, and cools within that window
  does not trip the locked-state watchdog

### Requirement: Retract reserve-budget warning
`flare_mmu.cfg` SHALL warn the operator when a buffer-locked retract cannot be
followed within the buffer's reserve. The MMU follow rate is bounded by the
firmware `GLOBAL_MAX_SPS` ceiling (83.33 mm/s); a retract faster than the
ceiling consumes buffer reserve at `SPEED - ceiling` mm/s, and the total reserve
is `BUF_MAX_TRAVEL_MM`.

`_FLARE_BL_RETRACT` SHALL emit a `RESPOND` warning when `SPEED` exceeds the
ceiling AND `LENGTH` exceeds
`buf_max_travel_mm / (1 - mmu_follow_ceiling / SPEED)`, naming the computed
maximum followable length. The macro SHALL NOT clamp `SPEED` or `LENGTH` and
SHALL NOT change the commanded motion: the warning is advisory, because clamping
would alter tip-forming park behavior.

`_FLARE_VARS` SHALL expose `mmu_follow_ceiling` (mm/s) and `buf_max_travel_mm`
mirroring the firmware values, documented as mirrors whose source of truth is
`flare_cmd.py --dump`.

#### Scenario: Over-budget retract warns
- **WHEN** `_FLARE_BL_RETRACT LENGTH=60 SPEED=125` is called with a 16 mm buffer
- **THEN** a warning is emitted naming a maximum followable length of 48 mm
- **AND** the retract is still performed at 60 mm / 125 mm/s

#### Scenario: Within-budget fast retract is silent
- **WHEN** `_FLARE_BL_RETRACT LENGTH=30 SPEED=125` is called with a 16 mm buffer
- **THEN** no warning is emitted

#### Scenario: Retract at or below the ceiling is silent
- **WHEN** `_FLARE_BL_RETRACT` is called with `SPEED` at or below
  `mmu_follow_ceiling`
- **THEN** no warning is emitted regardless of `LENGTH`

### Requirement: Follow rate reflects the firmware ceiling
`_FLARE_VARS.mmu_follow_rate` SHALL NOT be configured above the firmware follow
ceiling, so the configured value equals the commanded value. It SHALL be
`5000.0` mm/min (= `GLOBAL_MAX_SPS`, 83.33 mm/s), documented as the ceiling
rather than as a value the firmware harmlessly clamps.

#### Scenario: Configured follow rate is the commanded rate
- **WHEN** `_FLARE_BL_RETRACT` issues its `BL:T` command
- **THEN** the follow rate it passes is not silently reduced by the firmware
  clamp

### Requirement: Buffer helper macros wait on events, not dwells
`flare_mmu.cfg` SHALL NOT approximate firmware buffer-operation completion with
`G4` dwells, because `flare_cmd.py` blocks on the `BL` and `BS` completion
events (`klipper-integration`: "Buffer Commands Block Until Firmware
Completion").

`_FLARE_BL_RETRACT` SHALL NOT take a `PAUSE` parameter and SHALL NOT dwell
before the extruder retract: the `BL` shell command returns only once the
firmware reports the lock established, so the prime can no longer race the
retract. Its existing contract is otherwise unchanged — it primes to tension,
locks, retracts while the MMU follows at `mmu_follow_rate`, and does NOT release
the lock, so the caller still issues one `BS` when the retract chain is done.

`_FLARE_BUFFER_STABILIZE` SHALL default `SETTLE` to `0`. A non-zero `SETTLE`
SHALL remain available as an extra mechanical dwell on top of the completion
event, and every call site passing one SHALL document why.

#### Scenario: Buffer-locked retract does not dwell
- **WHEN** `_FLARE_BL_RETRACT` is called
- **THEN** it issues the `BL:T` shell command, which returns after the lock is
  established
- **AND** it emits no `G4` before the extruder retract

#### Scenario: Stabilize does not dwell by default
- **WHEN** `_FLARE_BUFFER_STABILIZE` is called with no `SETTLE`
- **THEN** it issues `BS` and returns when the firmware reports completion
- **AND** it emits no `G4`

#### Scenario: Explicit settle is still honored
- **WHEN** `_FLARE_BUFFER_STABILIZE SETTLE=2` is called
- **THEN** it dwells 2 seconds after the `BS` completion event
