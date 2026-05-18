## Why

The buffer-state rename (`rename-buffer-states-tension-compression`) makes
the physics explicit in every name. With `BUF_TENSION` = empty → feed and
`BUF_COMPRESSION` = full → back off stated literally, every control site
can be read as an assertion and checked for inversion. The historical
misnaming already planted at least one real polarity bug (`8f54bff`, relay
rates swapped) and the **PSF (analog) path is supported but rarely
exercised**, so latent inversions there are live bugs for analog users, not
dead code. This change audits every polarity-critical site in both paths
and fixes the inversions found.

Depends on `rename-buffer-states-tension-compression` (must land first) so
the audit reads the corrected vocabulary, not the misleading old names.

## What Changes

- Enumerate every polarity-critical site and classify each:
  `✅ correct` / `⚠ inverted-active-relay` / `⚠ inverted-active-analog` /
  `❓ needs-hardware-confirmation`.
- Audit site #1: the pin→state decode (`PIN_BUF_TENSION` read) — the
  original misnaming origin; verify pressed-tension-switch ⇒ `BUF_TENSION`.
- Audit the relay path (verify `8f54bff`/`1d9ebe5` read correctly in the
  new vocab) and the PSF/analog PI path (reserve target `RT` sign,
  `compression_floor`, `compression_recovery`/collapse,
  `neutral_anti_tension` floor, estimator-at-crossing sign, fast-brake on
  TENSION→COMPRESSION, AUTO_START gate state, `RE`/`BPV` sign).
- Ship the **relay** inversion fixes (hardware-verifiable). Each is
  behavior-changing and committed separately with its own justification
  (NOT mixed with the rename, NOT bundled).
- **Analog/PSF: no hardware rig exists (decision).** Analog findings are
  either (a) modelled on Happy Hare's analog sync controller as the
  reference implementation, or (b) left as a *basic spec only* (expected
  behavior documented, no code shipped). Analog fixes are NOT shipped
  blind/unverified. Implementer picks (a) or (b) per site; default (b)
  when Happy Hare gives no clear analogue.
- Record the findings table in this change as the audit artifact.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sync-refactor`: the sync control polarity invariants are stated
  explicitly in tension/compression vocabulary and enforced consistently
  across the relay and PSF paths and the pin→state decode.

## Impact

- Firmware: `firmware/src/sync.c`, the buffer-sensor decode, protocol sign
  fields — targeted polarity fixes only (post-rename).
- Analog/PSF path is the highest-risk area (supported, rarely run);
  relay path is mostly verification (already corrected once).
- Hardware: relay inversions retestable on the Pi; some analog sites are
  `❓ needs-hardware-confirmation` and flagged for an analog rig.
- No protocol/token rename here (that is the prerequisite change).
