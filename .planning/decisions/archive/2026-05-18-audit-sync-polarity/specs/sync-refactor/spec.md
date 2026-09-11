## ADDED Requirements

### Requirement: Sync control polarity matches the state contract

Sync control SHALL feed faster when the buffer is `BUF_TENSION` (empty,
printer pulling faster than the MMU) and back off when the buffer is
`BUF_COMPRESSION` (full, MMU pushing faster than the printer), in both the
relay and PSF control paths. No control site SHALL invert this
relationship.

#### Scenario: Tension feeds, compression backs off

- **WHEN** the buffer is `BUF_TENSION`
- **THEN** commanded feed increases (refill); **WHEN** `BUF_COMPRESSION`,
  commanded feed backs off — in both relay and PSF paths

#### Scenario: Audited sites classified

- **WHEN** the polarity audit completes
- **THEN** every site in the audit list is classified correct, fixed, or
  documented as needing hardware confirmation

#### Scenario: Analog handled without a rig

- **WHEN** an analog/PSF site is found inverted and no analog hardware
  exists
- **THEN** it is either implemented to match the Happy Hare reference
  controller or recorded as basic-spec-only `pending-analog-rig`, and is
  never blind-fixed from guesswork

### Requirement: Pin-to-state decode is verified non-inverted

The buffer-sensor decode SHALL map a pressed tension switch to
`BUF_TENSION` and a pressed compression switch to `BUF_COMPRESSION`. This
decode MUST be explicitly verified, as it is the origin of the historical
misnaming.

#### Scenario: Tension switch pressed

- **WHEN** the physical tension switch is engaged (buffer empty)
- **THEN** the firmware state is `BUF_TENSION`, not `BUF_COMPRESSION`

### Requirement: Polarity fixes are isolated from the rename

Behavior-changing polarity fixes SHALL be committed separately from the
prerequisite rename and from each other, each justified by the specific
contradiction it resolves. The rename change MUST remain behavior-preserving.

#### Scenario: Reviewable history

- **WHEN** a polarity fix is reviewed
- **THEN** it is a standalone commit citing the audit finding, with no
  rename churn mixed in
