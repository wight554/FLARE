## ADDED Requirements

### Requirement: Type-P Feed Quality and Reliable Stabilize

Type-P feed control SHALL track extruder demand on a real print without sustained
buffer hunting or end-of-move overshoot that produces print artifacts, and a manual
`BS` SHALL drive the buffer to goal in a single invocation from any non-saturated
position. Acceptance is measured against a real print, not isolated bench bursts.

#### Scenario: Steady print feed stays near goal

- **WHEN** `BUF_SENSOR_TYPE == 1` and the printer extrudes continuously
- **THEN** the buffer holds a band near goal without a sustained tension↔compression
  limit cycle, and no `SYNC:FAULT_HOLD` / `SYNC:cannot_refill` fires mid-print

#### Scenario: Single-shot BS recovers from mid-tension

- **WHEN** `BUF_SENSOR_TYPE == 1`, the buffer rests in the control tension zone but
  is not saturated (`CF == 1.0`), and a manual `BS` is issued
- **THEN** the buffer is driven to goal on that single `BS` (no silent no-op
  requiring a second invocation)
