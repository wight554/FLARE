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

### Requirement: FAULT_HOLD recovery resumes active sync without an ADVANCE event

On `FAULT_HOLD_RECOVERY` the controller SHALL reseed both the virtual
buffer position and `g_buf.state` to a MID state at the reserve target and
re-enter `SYNC_ACTIVE` directly at the bootstrap rate, without waiting for
any ADVANCE event. Recovery MUST NOT depend on a buffer-state classification
that survived the hold, so it can neither slam from a fictional ADVANCE nor
deadlock when a real ADVANCE never occurs mid-print.

#### Scenario: Recovery mid-print with extruder pulling

- **WHEN** the controller exits `SYNC_FAULT_HOLD` while the extruder is
  consuming and the buffer would otherwise never reach ADVANCE
- **THEN** sync resumes in `SYNC_ACTIVE` at the baseline-capped bootstrap
  with the model reseeded to the reserve target, not stalled OFF

### Requirement: MID stalled-trailing estimator recovery targets the baseline floor

The MID stalled-trailing estimator recovery SHALL bleed the estimator
toward at least the baseline control floor while the dead-reckoned model is
pinned at the trailing wall, not merely toward current lane motion plus a
small margin. The bleed MUST remain gated to the pinned condition so it
self-terminates the instant the buffer leaves the trailing wall, preventing
ADVANCE overshoot.

#### Scenario: Pinned at trailing wall with collapsed lane motion

- **WHEN** the model is pinned at the trailing wall in MID and current lane
  motion is at or below the collapsed estimate
- **THEN** the estimator is bled toward the baseline control floor so the
  commanded feed exceeds real demand and the buffer climbs off the wall

#### Scenario: Buffer leaves the wall

- **WHEN** the buffer position rises off the trailing wall
- **THEN** the stalled-trailing bleed no longer applies and normal reserve
  control resumes without overshoot
