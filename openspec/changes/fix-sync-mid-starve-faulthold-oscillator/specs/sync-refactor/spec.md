## ADDED Requirements

### Requirement: MID reserve control refills the buffer toward the reserve target

Standalone MID reserve control SHALL hold active-lane feed at no less than
the baseline-derived assist floor whenever the buffer is in `BUF_MID`, the
active lane is feeding without fault, and the buffer is at or below the
reserve target. The assist floor SHALL be derived from the learned baseline
control floor, not from the live extruder estimate, so it cannot itself
drive the buffer toward ADVANCE. Estimator freshness or confidence MUST NOT
gate this floor; a fresh-but-collapsed estimator is the failure this
requirement exists to prevent.

#### Scenario: Fresh-but-collapsed estimator at MID below target

- **WHEN** the buffer is in `BUF_MID` at/below the reserve target, the
  active lane is feeding, and the extruder estimate is fresh and
  high-confidence but collapsed well below the learned baseline
- **THEN** feed is floored at the baseline-derived assist rate so the
  buffer refills toward the reserve target instead of draining to the
  trailing wall

#### Scenario: Buffer above target

- **WHEN** the buffer is in `BUF_MID` above the reserve target
  (`reserve_error` above the deadband)
- **THEN** no assist floor is applied and normal control is unchanged

### Requirement: Trailing-recovery collapse cap never starves MID feed

The trailing-recovery collapse cap SHALL NOT reduce active-lane feed below
the baseline control floor while the buffer is in `BUF_MID`. Full collapse
braking SHALL remain unchanged once the buffer is in `BUF_TRAILING`.

#### Scenario: Recovery cap below baseline while still MID

- **WHEN** trailing recovery is active, the buffer is still in `BUF_MID`,
  and the computed recovery cap is below the baseline control floor
- **THEN** the recovery cap is raised to the baseline control floor so the
  buffer can refill

#### Scenario: Collapse braking in TRAILING preserved

- **WHEN** the buffer is in `BUF_TRAILING` during trailing recovery
- **THEN** the existing collapse cap, ramp, and fault-hold behavior apply
  unchanged

### Requirement: FAULT_HOLD recovery does not re-arm into ADVANCE overshoot

On `FAULT_HOLD_RECOVERY` the controller SHALL reseed the virtual buffer
position to the reserve target in virtual-endstop mode, discarding the
fictional ADVANCE accumulated by dead reckoning while feed was zero during
hold. `sync_bootstrap_sps()` SHALL clamp its result to at most the baseline
control floor so a post-recovery start ramps from the learned baseline
rather than slamming ADVANCE.

#### Scenario: Recovery after repeated trailing fault-hold

- **WHEN** the controller exits `SYNC_FAULT_HOLD` after the recovery
  timeout in virtual-endstop mode
- **THEN** the virtual buffer position is reset to the reserve target and
  `AUTO_START` re-arms only on a genuine ADVANCE, not a dead-reckoned one

#### Scenario: Post-recovery bootstrap rate

- **WHEN** `sync_bootstrap_sps()` computes a start rate after recovery
- **THEN** the result does not exceed the baseline control floor, so the
  control law raises feed only as the real buffer demands
