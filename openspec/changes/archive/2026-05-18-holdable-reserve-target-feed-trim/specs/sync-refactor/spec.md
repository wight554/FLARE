## ADDED Requirements

### Requirement: Reserve target stays holdable off the fault wall

The reserve position target SHALL cap the bias contribution at
`SYNC_RESERVE_BIAS_POS_FRAC_CAP` so the buffer parks well off the trailing
fault wall, leaving margin for frequent switch crossings that keep the
switch-crossing estimator fresh. The parked depth MUST NOT encode the full
trailing bias; bias beyond the cap is applied as a feed-rate lean, not
position depth.

#### Scenario: Floored bias no longer parks on the wall

- **WHEN** the effective bias is at or above the configured floor and the
  cap is below it
- **THEN** the reserve target uses only the capped bias position
  contribution, leaving multi-millimetre fault margin

#### Scenario: Bias at or below the cap is unchanged

- **WHEN** the effective bias is at or below
  `SYNC_RESERVE_BIAS_POS_FRAC_CAP`
- **THEN** the reserve position target is identical to the pre-change
  value (degenerate parity)

### Requirement: Trailing lean is a bounded advance-side feed trim

The never-ADVANCE trailing lean SHALL be applied as a feed-rate reduction
bounded by `SYNC_TRAILING_FEED_TRIM_MAX_SPS`, only while the buffer is in
`BUF_MID` and on the advance side of the reserve target. The trim MUST NOT
deepen the buffer past the reserve target nor reduce feed when the buffer
is at or trailing of the target, so it cannot reproduce reserve starvation.

#### Scenario: Advance side of target in MID

- **WHEN** the buffer is in `BUF_MID` with `reserve_error_mm > 0`
- **THEN** feed is trimmed by at most `SYNC_TRAILING_FEED_TRIM_MAX_SPS`,
  nudging the buffer toward the holdable reserve target

#### Scenario: At or trailing of target, or not MID

- **WHEN** the buffer is at/trailing of the reserve target, or not in
  `BUF_MID`
- **THEN** no trailing feed trim is applied and starvation cannot occur
