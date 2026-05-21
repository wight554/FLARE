## ADDED Requirements

### Requirement: NEUTRAL-band demand-collapse estimator correction

The controller SHALL decay `extruder_est_sps` toward the relay-estimator rate
that arrests the drift (the current MMU feed rate divided by
`RELAY_NEUTRAL_FRAC`, minus a small margin) in type-D standalone mode
(`BUF_SENSOR_TYPE == 0`) while the buffer is in `BUF_NEUTRAL` and the virtual
buffer position is drifting toward COMPRESSION (the MMU is out-feeding whatever
the estimator claims) with no recent TENSION refill. This fills
the gap left by the rail-only correctors — the TENSION catch-up (raises EST at
the empty rail) and the COMPRESSION bleed-down (lowers EST at the full rail) —
which do not act while both the physical switches and the virtual model read
NEUTRAL. The corrector MUST be self-gating on the actual drift condition (not a
purge-specific or mode flag) so it cannot engage during genuine extrusion where
the buffer holds mid-band, and MUST require an active feed
(`sync_current_sps > 0`) and a short NEUTRAL dwell before acting. Type-P analog
mode (`BUF_SENSOR_TYPE != 0`) behavior MUST be unchanged.

#### Scenario: Demand collapses after a tension catch-up surge

- **WHEN** in type-D mode the estimator has learned a high
  `extruder_est_sps` during a TENSION catch-up, the buffer then settles into
  NEUTRAL, and the extruder demand drops to ~0 (e.g. a purge ends) so the
  virtual buffer position slides toward COMPRESSION while feeding
- **THEN** `extruder_est_sps` decays toward the current relay-arrest estimator
  rate, the NEUTRAL relay target falls with it, the MMU backs off, and the
  buffer stabilizes in-band instead of reaching the compression switch

#### Scenario: Genuine high-flow extrusion is not disturbed

- **WHEN** in type-D mode the extruder is actively pulling at high flow and the
  buffer holds in the NEUTRAL band without drifting toward COMPRESSION
- **THEN** the demand-collapse corrector does not engage and
  `extruder_est_sps` is left to the existing learning paths

#### Scenario: Type-P analog mode unchanged

- **WHEN** `BUF_SENSOR_TYPE != 0` (type P analog)
- **THEN** the NEUTRAL demand-collapse corrector does not apply and prior
  estimator behavior is byte-identical

### Requirement: Fast-brake arms on NEUTRAL to COMPRESSION when feed is hot

In type-D standalone mode the controller SHALL arm the existing fast-brake when
the buffer transitions into `BUF_COMPRESSION` from `BUF_NEUTRAL` while the MMU
feed rate is hot, in addition to the existing `TENSION → COMPRESSION` arming, so
a buffer that reaches the compression switch from the NEUTRAL band at speed gets
the instant zero-feed stop rather than coasting in under a slow ramp-down. The
arming MUST be gated to a hot feed rate so a slow, benign NEUTRAL→COMPRESSION
drift (which the relay stop already handles) does not trigger a hard brake.
Type-P analog mode behavior MUST be unchanged.

#### Scenario: Hot NEUTRAL to COMPRESSION transition

- **WHEN** in type-D mode the buffer enters `BUF_COMPRESSION` from
  `BUF_NEUTRAL` while the MMU feed rate is above the hot threshold
- **THEN** the fast-brake is armed and feed is driven to zero immediately,
  preventing the MMU from coasting into the full buffer

#### Scenario: Slow NEUTRAL to COMPRESSION drift

- **WHEN** in type-D mode the buffer enters `BUF_COMPRESSION` from
  `BUF_NEUTRAL` while the MMU feed rate is at or near `SYNC_MIN_SPS`
- **THEN** the fast-brake is not armed and the normal relay stop handles the
  full buffer
