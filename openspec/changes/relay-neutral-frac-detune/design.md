# Design — relay-neutral-frac-detune

## The limit cycle, quantified

Type-D has only two microswitches: no analog position, no extruder feedback.
`relay_control_law` (`sync.c:1627`) is a hysteretic relay on the switch state:

| State | Feed target |
|-------|-------------|
| TENSION (empty) | `baseline × relay_catchup_frac` (fast refill) |
| NEUTRAL | `clamp(extruder_est_sps, SYNC_MIN, baseline) × relay_neutral_frac` |
| COMPRESSION (full) | `0` (true-stop, `sync.c:2097`) |

`extruder_est_sps` ≈ true demand `D` (measured each crossing). In NEUTRAL the
net buffer fill rate is `(neutral_frac − 1) · D`. With `neutral_frac = 1.25`
that is `0.25·D` — the arm climbs into COMPRESSION quickly, the relay stops to
`0`, the extruder drains it at `−D`, it re-enters NEUTRAL, and the slew-limited
feed (`SYNC_RAMP_UP/DN`) ramps back up. The audible "ramp → stop → ramp" is
this cycle; its frequency ∝ `(neutral_frac − 1)`.

Lowering `neutral_frac` to `1.10` cuts the overfeed to `0.10·D`, ~2.5× longer
climb time, ~2.5× lower cycle frequency, and a longer dwell parked in the
quiet NEUTRAL mid-band — i.e. "neutral, slightly compressed," which is the
goal. It stays `> 1.0`, so the lean is still toward COMPRESSION and the buffer
does not starve to TENSION; if a machine *does* drift to TENSION under steady
demand, `TUNING.md` already prescribes nudging `neutral_frac` back up.

## Why not other levers

- **`sync_kp_rate` / autotune** — not in the type-D path; `relay_control_law`
  ignores it. Only `psf_control_law` (analog type-P) consumes kp. Tuning it on
  type-D is a no-op (this is exactly how the prior session was misled).
- **`sync_ramp_accel/decel`** — only the slew rate between the relay's discrete
  levels. Raising it makes the motor chase the jumps harder = a tighter, more
  violent cycle (observed: accel 300/500 → unstable). 150 is validated; leave
  it.
- **COMPRESSION feed (the `0` true-stop)** — must stay `0`. Feeding `SYNC_MIN`
  into a full buffer was the bowden-overfill / purge-grind cause fixed by
  `compression-overfeed-stop`. The fix is to *reach* COMPRESSION less often
  (lower overfeed), not to soften the stop.
- **`sync_compression_bias_frac`** — a position-setpoint bias, inert for the
  type-D relay (it only sees switch state); irrelevant here.

## Why 1.10

`1.10` is the "gentle full lean" documented in both the `sync.c` architecture
comment (`~1.0 = match demand, >1 = gentle lean`) and the
`sync-observer-root-cause` design history (`MID_FRAC ~ 1.10`). It is the
defensible code-grounded default; the exact on-hardware optimum is expected in
`1.05–1.15` and is confirmed via the runtime A/B below. `relay_neutral_frac`
is `SET:`/`GET:`-tunable and clamped `[0.5, 3.0]` (`protocol.c:978`), so this
needs no reflash to validate.

## On-hardware validation (A/B, no reflash)

```
GET:RELAY_NEUTRAL_FRAC            # confirm the live value (persisted; may not be 1.25)
SET:RELAY_NEUTRAL_FRAC:1.10       # print infill; listen + watch BS compression%
SET:RELAY_NEUTRAL_FRAC:1.05       # if still cycling audibly
# if it drifts to TENSION / starves under steady demand, raise toward 1.15
```

Pass bar after the new default + threshold revert: `flare_sync_check.py
--mode stability` peak `< 1.0` cycles/s and combined endstop `< 30 %` on a
typical infill soak, with TENSION still ≈ 0 (no starvation).

## Persistence note

No `SETTINGS_VERSION` bump: this is a value-only default change and a bump
would reset every persisted setting (TMC currents, PSF/baseline calibration).
Already-flashed units therefore keep their stored `relay_neutral_frac` until
the operator `SET:`s the new value or factory-resets; fresh flashes pick up
`1.10`. The A/B step above is the migration path for existing units.

## Open follow-up (not in this change)

`flare_sync_check.py`'s `tune` mode autotunes `SYNC_KP_RATE`, which a type-D
relay ignores — the tool will happily "converge" on a meaningless knob and
report PASS, which is how the prior session was fooled. A future change should
have the tool read `BUF_SENSOR_TYPE` (via `GET:`) and refuse/redirect kp
autotune on type-D (point at `relay_neutral_frac` instead). Documented here
rather than implemented to keep this change value-only.
