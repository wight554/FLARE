# cutter-feed-timeout Specification

## Purpose
Documents the cutter feed timeout tunable and its serial/config exposure for safe long cutter-feed moves.

## Requirements
### Requirement: cutter-feed-timeout-tunable
`CUT_TIMEOUT_FEED_MS` — the per-phase motor-feed safety timeout used in `CUT_FEED_WAIT` — SHALL be a runtime-tunable parameter sourced from `config.ini` (`cut_feed_timeout_ms`), persisted in flash, and accessible via `GET:CUT_FEED_MS` / `SET:CUT_FEED_MS` serial protocol commands.

The parameter SHALL be clamped to [1000, 120000] ms on `SET:`. Default SHALL be 30000 ms. It SHALL appear in `flare_cmd.py --dump` output.

#### Scenario: Large feed completes without abort
- **WHEN** `CUT_FEED_MM` is set such that `feed_initial_ms > 5000 ms` and `CUT_TIMEOUT_FEED_MS` is at its default 30000 ms
- **THEN** `CUT_FEED_WAIT` completes normally and the cutter proceeds to `CUT_CLOSING` without emitting `CUT:ERROR ABORTED`

#### Scenario: Timeout still fires on genuine jam
- **WHEN** the cutter motor is running in `CUT_FEED_WAIT` and elapsed time exceeds `CUT_TIMEOUT_FEED_MS`
- **THEN** `cutter_abort()` is called, `CUT:ERROR ABORTED` is emitted, and the servo returns to `SERVO_BLOCK_US`

#### Scenario: GET returns current value
- **WHEN** host sends `GET:CUT_FEED_MS`
- **THEN** firmware replies `OK:CUT_FEED_MS:<value>` reflecting the current runtime value

#### Scenario: SET updates and persists value
- **WHEN** host sends `SET:CUT_FEED_MS:20000` followed by `SV:`
- **THEN** `GET:CUT_FEED_MS` returns 20000 after reflash

### Requirement: cutter-settle-timeout-tunable
`CUT_TIMEOUT_SETTLE_MS` — the per-phase servo-settle safety timeout used in `CUT_OPEN_WAIT`, `CUT_CLOSE_WAIT`, and `CUT_REOPEN_WAIT` — SHALL be a runtime-tunable parameter sourced from `config.ini` (`cut_settle_timeout_ms`), persisted in flash, and accessible via `GET:CUT_SETTLE_MS` / `SET:CUT_SETTLE_MS` serial protocol commands.

The parameter SHALL be clamped to [500, 10000] ms on `SET:`. Default SHALL be 3000 ms. It SHALL appear in `flare_cmd.py --dump` output.

#### Scenario: Settle timeout exceeds SERVO_SETTLE_MS
- **WHEN** `SERVO_SETTLE_MS` is set to any value ≤ `CUT_TIMEOUT_SETTLE_MS`
- **THEN** `CUT_OPEN_WAIT`, `CUT_CLOSE_WAIT`, and `CUT_REOPEN_WAIT` all complete normally without triggering `cutter_abort()`

#### Scenario: Abort fires when servo hangs
- **WHEN** the servo fails to settle within `CUT_TIMEOUT_SETTLE_MS`
- **THEN** `cutter_abort()` is called and `CUT:ERROR ABORTED` is emitted

#### Scenario: GET returns current settle timeout
- **WHEN** host sends `GET:CUT_SETTLE_MS`
- **THEN** firmware replies `OK:CUT_SETTLE_MS:<value>`
