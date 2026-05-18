## Why

The buffer states were historically misnamed. `BUF_ADVANCE` actually means
the filament is **tensioned** (buffer empty, printer pulling faster than
the MMU pushes — underextrusion risk); `BUF_TRAILING` means the filament is
**compressed** (buffer full, MMU pushing faster than the printer pulls).
The names imply the opposite of the physics, which has repeatedly caused
misunderstanding and a real polarity bug (corrected in `8f54bff`). Adopt
the Happy Hare community vocabulary so the name states the physics:

```
 OLD          NEW             meaning
 BUF_ADVANCE  BUF_TENSION     tensioned · empty · printer > MMU · feed
 BUF_TRAILING BUF_COMPRESSION compressed · full · MMU > printer · back off
 BUF_MID      BUF_NEUTRAL     neutral band
 BUF_FAULT    BUF_FAULT       unchanged
```

This change is a **pure, behavior-preserving rename**. It does not alter
control logic. Any polarity inversion the clearer names expose is fixed
separately in the dependent change `audit-sync-polarity`, so the rename
diff stays reviewable as "faithful rename only".

## What Changes

- Rename the C enum and all state-derived identifiers, constants, and
  comments: `*advance* → *tension*`, `*trailing* → *compression*`,
  state-derived `*mid* → *neutral*` (incl. `mid_creep_*`,
  `sync_overshoot_mid_extend`, `SYNC_RELAY_MID_FRAC`,
  `sync_mid_anti_advance_*`). `PIN_BUF_ADVANCE → PIN_BUF_TENSION`.
- Rename wire/telemetry tokens **and short field keys** (zero survivors,
  decision): `BUF:ADVANCE|MID|TRAILING → BUF:TENSION|NEUTRAL|COMPRESSION`,
  `EV:BS:*`, `EV:SYNC:ADV_RISK_HIGH → EV:SYNC:TENSION_RISK_HIGH`, and the
  old-state-derived status field keys `AD` (advance dwell), `TD` (trailing
  dwell), `APX` (advance-pin count) and any other such keys.
- Rename config keys: `sync_advance_dwell_stop_ms →
  sync_tension_dwell_stop_ms`, `sync_advance_ramp_delay_ms →
  sync_tension_ramp_delay_ms`, `sync_trailing_bias_frac →
  sync_compression_bias_frac`, `trailing_rate → compression_rate`,
  `mid_creep_* → neutral_creep_*`, `sync_overshoot_mid_extend →
  sync_overshoot_neutral_extend`. **Legacy keys are simply ignored by the
  existing unknown-key handling (no new hard-error logic); a stale
  `config.ini` silently falls back to defaults for renamed keys.** No
  migration guide — active dev, renames are safe; just zero old names.
- `mid → neutral` applies to **buffer-state-derived `mid` only**
  (`BUF_MID`, `mid_creep_*`, `sync_overshoot_mid_extend`,
  `SYNC_RELAY_MID_FRAC`, `sync_mid_anti_advance_*`). Unrelated arithmetic
  `mid`/`middle`/`midpoint` is left untouched (decision).
- Update token-parsing scripts in lockstep (`scripts/flare_cmd.py`,
  `scripts/gen_config.py`, and any analyzer/tuner that parses `BUF:`/
  `EV:`/config keys).
- Update live specs and docs to the new vocabulary: specs
  `sync-state-model`, `sync-refactor`, `toolchange-orchestration`,
  `motion-safety`, `live-tuner`; docs `BEHAVIOR.md`, `HARDWARE.md`,
  `CONTEXT.md`, `MANUAL.md`, `KLIPPER.md`, `TEST_CASES.md`.
- **No back-compat, no aliases, zero `advance`/`trailing` survivors and
  zero buffer-state `mid` survivors anywhere** (arithmetic `mid` exempt
  per the scope decision above). Archived OpenSpec changes are historical
  records and are left as-is (not referenced as a live contract).
- Both control paths stay supported (relay 2-endstop = current focus, PSF
  analog = supported) — the full surface is renamed, nothing deleted.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sync-refactor`: the buffer state vocabulary, serial protocol state
  tokens, and config key names are renamed to tension/compression/neutral;
  control behavior is unchanged by this change.

## Impact

- Firmware: enum + ~50 derived identifiers across `firmware/src/*.c`,
  `firmware/include/*`, hardware pin macro. Compiler verifies the enum
  rename; string tokens and config keys are NOT compiler-checked — an
  exhaustive grep inventory is a required deliverable.
- Protocol: breaking token rename; in-repo scripts updated in lockstep.
- Config: breaking key rename; users must update `config.ini`
  (acceptable — pre-stable active dev, no back-compat by decision).
- Specs/docs: 5 live specs + 6 docs reworded.
- Validation gate: host build + a status-line semantics snapshot must be
  identical pre/post (proves behavior-preserving). No on-hardware behavior
  change expected.
