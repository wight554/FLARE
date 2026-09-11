## ADDED Requirements

### Requirement: Flash Erase-Cycle Visibility

The firmware SHALL count settings-sector erase cycles across the sector's
lifetime and expose the count read-only, without implementing wear
leveling. `settings_save()` SHALL increment `g_flash_erase_count` once per
call, before that call's own erase+program, and persist the incremented
value as part of the same write. `settings_load()` SHALL restore the count
from a valid flash sector; `settings_defaults()` SHALL seed it to `0` (a
genuinely fresh/corrupt sector, or an explicit `RS:` reset, has no prior
history to restore — consistent with every other `settings_t` field's
behavior under `RS:`). The firmware SHALL emit `EV:FLASH:WEAR_WARNING`
exactly once, on the save where the count first reaches
`FLASH_WEAR_WARN_THRESHOLD`.

#### Scenario: Count persists across a normal save/load cycle

- **WHEN** `settings_save()` is called N times across reboots
- **THEN** `GET:FLASH_ERASE_COUNT` returns N after a fresh boot's
  `settings_load()`

#### Scenario: Count resets to 0 on a fresh or corrupt sector

- **WHEN** the settings sector fails the magic/version/CRC check
  (`settings_load()`'s fallback to `settings_defaults()`)
- **THEN** `GET:FLASH_ERASE_COUNT` reads `0` until the next save

#### Scenario: RS: reset also resets the count (documented, not a bug)

- **WHEN** an operator issues `RS:` (settings reset)
- **THEN** `settings_defaults()` seeds the count to `0`, and the
  immediately-following `settings_save()` persists it as `1` — matching
  every other `settings_t` field's behavior under `RS:` (this repo has no
  field-migration path; wear history is not exempt from that)

#### Scenario: Warning fires exactly once

- **WHEN** `g_flash_erase_count` reaches `FLASH_WEAR_WARN_THRESHOLD` on a
  given `settings_save()` call
- **THEN** `EV:FLASH:WEAR_WARNING` fires on that save only, not on every
  subsequent save past the threshold
