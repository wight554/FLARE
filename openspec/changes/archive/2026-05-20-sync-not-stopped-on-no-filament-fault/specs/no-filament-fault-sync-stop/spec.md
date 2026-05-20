## ADDED Requirements

### Requirement: Sync stops on NO_FILAMENT fault
When `reload_trigger` cannot switch lanes because the other lane has no filament, the system SHALL disable the sync motor before emitting the `RELOAD:FAULT NO_FILAMENT` event.

#### Scenario: Runout with both lanes empty
- **WHEN** active lane runs out of filament and the other lane has no filament present (`lane_in_present` returns false)
- **THEN** `reload_trigger` calls `sync_disable(true)` and the sync motor stops (`SM:0` in `?:` status) before emitting `RELOAD:FAULT NO_FILAMENT`

#### Scenario: Status reflects stopped sync after fault
- **WHEN** `RELOAD:FAULT NO_FILAMENT` has been emitted
- **THEN** `?:` status shows `SM:0` and `ST:0` without requiring a manual `ST` command
