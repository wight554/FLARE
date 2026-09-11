# Delta: toolchange-orchestration

## MODIFIED Requirements

### Requirement: RELOAD Buffer-Driven Contact
During runout RELOAD, the new lane SHALL approach until physical buffer contact is detected. Contact SHALL mean the buffer arm is pushed to the compression side for both sensor types: `BUF_COMPRESSION` for type-D, and analog position above `PSF_LOAD_CONTACT_THRESHOLD_NORM` (compression-side, sign convention +compression/−tension) for type-P. A type-P buffer resting at or drifting toward its tension/home rail SHALL NOT register as contact.

#### Scenario: RELOAD Approach
- **WHEN** the old lane clears the Y-splitter and `RELOAD_JOIN_MS` elapses
- **THEN** the new lane starts `TASK_FEED` at `JOIN_SPS`
- **AND** waits for the buffer to hit `BUF_COMPRESSION`
- **AND** aborts if the configured travel limit or physical timeout is reached before contact

#### Scenario: Type-P RELOAD Approach contact
- **WHEN** RELOAD approach runs with `BUF_SENSOR_TYPE=1` (type-P)
- **THEN** contact is detected only when `g_buf_pos > PSF_LOAD_CONTACT_THRESHOLD_NORM` (arm pushed compression-side by the new filament)
- **AND** the at-rest home position (−1.0) does not satisfy the contact condition

#### Scenario: Y gate disabled does not insta-fail tail-clear wait
- **WHEN** `RELOAD_Y_TIMEOUT_MS` is configured `0` and the old lane tail has not yet cleared
- **THEN** the Y-splitter gate is skipped but the tail-clear wait continues without raising `RELOAD_Y_TIMEOUT` immediately

### Requirement: RELOAD Bang-Bang Pressure Cycle
During the RELOAD follow phase, the new lane SHALL over-feed to close the gap and maintain pressure on the old tail. The follow success signal SHALL preempt the control law's tension refill response on the same trigger for both sensor types: the physical `BUF_TENSION` switch for type-D, and the tension-zone crossing for type-P. A type-P tension-zone crossing SHALL NOT complete the follow during the touch-settle/boost window (`RELOAD_TOUCH_SETTLE_MS + RELOAD_TOUCH_BOOST_MS` after contact), where the deliberately suppressed feed transiently dips the arm below the zone edge without an extruder grab. Success SHALL NOT require a position deeper than the zone edge, because the law's `JOIN_SPS` refill makes deeper positions unreachable.

Tension-based success and the compression-jam timeout both assume a consumer: an extruder drawing the old tail fast enough to pull the new filament to `BUF_TENSION`. The follow phase SHALL fork on the estimated extruder draw (`g_extruder_est_sps` vs `RELOAD_CONSUMER_MIN_SPS`):
- With a consumer, the follow behaves as above (over-feed, success on the grab-driven tension crossing).
- Without a consumer (paused print, manual `RL:` retrigger after the print stopped, or bench with an idle extruder), the buffer cannot reach `BUF_TENSION` by construction, so completion SHALL instead be sustained `BUF_COMPRESSION` contact held past the touch-settle/boost window — the new filament staged at the extruder mouth, ready for grab on resume. While no consumer is present the compression-jam detectors (hard-push wall and "stuck in compression" timeout) SHALL be suppressed, because a full buffer with nothing draining it is the normal resting state, not a jam. The absolute follow timeout SHALL still backstop a genuinely wedged feed. The fork is re-evaluated each tick, so a consumer appearing mid-follow (print resumes) reverts to tension-based success.

#### Scenario: Follow Phase
- **WHEN** physical contact is established (`BUF_COMPRESSION`)
- **THEN** the motor target becomes `extruder_est_sps * RELOAD_LEAN_FACTOR` (over-feeding)
- **AND** drops to `COMPRESSION_RATE` if the physical arm hits the `COMPRESSION` wall
- **AND** repeats this cycle until `LOADED` (toolhead sensor triggered or `BUF_TENSION` sustained)

#### Scenario: Type-P follow success requires extruder grab
- **WHEN** RELOAD follow runs with `BUF_SENSOR_TYPE=1` and the buffer crosses into the tension zone within `RELOAD_TOUCH_SETTLE_MS + RELOAD_TOUCH_BOOST_MS` of contact (entry dip, feed still suppressed)
- **THEN** the follow phase continues (no `RELOAD:LOADED`)
- **AND** `RELOAD:LOADED` is emitted on a tension-zone crossing after that window, a toolhead sensor trigger at any time, or the `LOAD_MAX` distance fallback

#### Scenario: No-consumer follow completes on staged compression
- **WHEN** RELOAD follow runs with the estimated extruder draw below `RELOAD_CONSUMER_MIN_SPS` (idle extruder — paused print, manual `RL:` retrigger, or bench)
- **THEN** the compression-jam detectors do not fire (`FOLLOW_JAM` is not raised on sustained compression)
- **AND** `RELOAD:LOADED` is emitted once `BUF_COMPRESSION` contact has held past `RELOAD_TOUCH_SETTLE_MS + RELOAD_TOUCH_BOOST_MS`
- **AND** if the extruder begins drawing above the threshold before then, success reverts to the tension-crossing rule

### Requirement: Manual RL State-Aware Resume
The manual `RL:` command SHALL resume the auto-runout RELOAD sequence from the current physical lane state rather than running a fixed same-lane routine, so it can retrigger a reload that the automatic trigger missed or that aborted mid-sequence. When the active lane has run out but the other lane holds filament, `RL:` SHALL run the full runout flow (swap to the other lane, Y-clear wait, approach, follow). When the active lane still holds filament — the already-swapped fresh lane, or a lane that never ran out — `RL:` SHALL resume approach/follow on the active lane with no second swap and no Y wait. `RL:` SHALL return `ER:NO_FILAMENT` only when neither lane holds filament.

#### Scenario: RL resumes a missed swap
- **WHEN** `RL:` is issued with the active lane's input sensor clear (`IN=0`) and the other lane loaded (`IN=1`)
- **THEN** the reload swaps to the other lane via the runout flow (`RELOAD:SWITCHING`), not a same-lane reload

#### Scenario: RL resumes on the already-swapped lane
- **WHEN** `RL:` is issued with the active lane loaded (`IN=1`) and the other lane empty
- **THEN** the reload runs approach/follow on the active lane with no swap (`RELOAD:JOINING`)

#### Scenario: RL with both lanes empty
- **WHEN** `RL:` is issued and neither lane holds filament
- **THEN** the command returns `ER:NO_FILAMENT` and no motion starts

#### Scenario: RL on an already-loaded lane is a no-op
- **WHEN** `RL:` is issued with the active lane loaded (`IN=1`) and the toolhead already confirms filament (a prior reload already completed, or this lane never ran out)
- **THEN** the command emits `RELOAD:LOADED` immediately and does not restart `RELOAD_APPROACH`/`RELOAD_FOLLOW` or reset the toolhead-filament flag
- **AND** no `FOLLOW_JAM` can be raised, because no motion is started into an already-seated buffer
