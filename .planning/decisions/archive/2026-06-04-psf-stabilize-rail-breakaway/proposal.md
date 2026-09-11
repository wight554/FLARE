## Why

Type-P idle/boot buffer-stabilize cannot drive the buffer off a saturated rail.
On rig, a loaded buffer resting at the tension home rail (`BP −1.0`, `CF:0.50`
saturated) stays pinned: `BS` and boot-stab both emit `BUF_STAB:START →
BUF_STAB:STAGNANT_TIMEOUT` with `g_buf_pos` never leaving `−1.0`. The operator
must `MV` manually to recover.

Root cause (`sync.c:906`): the type-P stagnant guard aborts if `g_buf_pos` moves
`< 0.03` norm within 200 ms of start. `BUF_STAB_SPS = 4092` = 10 mm/s → 200 ms
feeds only **2 mm**, and the first ~mm at a hard rail is absorbed by the
mechanical deadband (the SSE trace shows zero ADC change across the full 200 ms).
So the guard fires before the arm ever breaks away. `MV:20` works because 20 mm
>> the rail deadband.

The 200 ms guard's real job (RIG FIX 8) is to stop **dry-spin when the buffer is
uncoupled / not tracking** — a valid protection. The bug is that it measures
wall-time from start, including the time spent still pinned at the rail, instead
of measuring progress *after* the buffer leaves saturation.

## What Changes

- Type-P stabilize stagnant check (`sync.c:906`): while the analog signal is
  saturated (`g_buf_analog_saturated_since_ms != 0`), do NOT run the
  `0.03`-norm/window abort — keep driving to break off the rail and hold the
  stagnant baseline (`g_boot_stabilize_start_pos = g_buf_pos`) so the check only
  judges motion once desaturated.
- Bound the breakaway with a cap `PSF_STAB_RAIL_BREAK_MS` (default 1500 ms ≈
  15 mm) measured from start: still saturated past the cap → genuinely
  stuck/uncoupled → `BUF_STAB:STAGNANT_TIMEOUT` + stop. Do NOT reset
  `g_boot_stabilize_started_ms` in the saturated branch, or the cap can never
  fire.
- Once desaturated, the existing fast stagnant check (now `PSF_STAB_STAGNANT_MS`
  / `PSF_STAB_STAGNANT_NORM`, default 200 ms / 0.03) resumes unchanged — the
  dry-spin protection is preserved for the off-rail case.
- Promote the magic literals `200` / `0.03f` to live-tunable params
  (`PSF_STAB_STAGNANT_MS`, `PSF_STAB_STAGNANT_NORM`) plus the new
  `PSF_STAB_RAIL_BREAK_MS`, so rig tuning needs no reflash. NVM persistence not
  required (SET each session; bake winning defaults into `tune.h`).
- Type-D unaffected (block is already `BUF_SENSOR_TYPE == 1`).

## Capabilities

### Modified Capabilities
- `psf-type-p-sensor`: idle/boot stabilize can drive the buffer off a saturated
  rail to goal; the stagnation/dry-spin guard measures progress after
  desaturation, bounded by a breakaway cap, instead of aborting on the rail's
  mechanical deadband.

## Impact

- `firmware/src/sync.c`: `buffer_stabilize_tick()` stagnant block (~L906).
- `firmware/include/tune.h`: `CONF_PSF_STAB_STAGNANT_MS=200`,
  `CONF_PSF_STAB_STAGNANT_NORM=0.03f`, `CONF_PSF_STAB_RAIL_BREAK_MS=1500`.
- `firmware/src/main.c`, `firmware/include/controller_shared.h`,
  `firmware/src/protocol.c`: var defs, externs, GET/SET (live-tunable).
- Rig: from `BP −1.0` loaded, `BS` drives to goal `≈ +0.40`; boot-stab same;
  uncoupled/jammed buffer still aborts at the cap, not 10 s.
- No NVM/host changes.
