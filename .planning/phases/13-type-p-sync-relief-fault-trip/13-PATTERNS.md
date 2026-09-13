# Phase 13: Type-P Sync Relief & Fault Trip - Pattern Map

**Mapped:** 2026-09-12
**Files analyzed:** 11 (all modified, no new files — firmware-only phase)
**Analogs found:** 11 / 11 (all in-tree; every mechanism this phase needs already has a working
analog in the same files it modifies — see RESEARCH.md "Don't Hand-Roll")

**Note on `firmware/include/tune.h` and `config.ini`:** both are gitignored, generator-produced
files (`.gitignore:22`, `.gitignore:19`). They are still real edit targets for this phase (`tune.h`
regenerates from `config.ini`/`config.ini.example` via `scripts/gen_config.py`), but PATTERNS.md
does not point plans at the untracked file's byte content — it points at the tracked generator
(`scripts/gen_config.py`) and the tracked example file (`config.ini.example`) as the source of
truth for what `tune.h`/`config.ini` will contain after regeneration.

## File Classification

| Modified File | Role | Data Flow | Closest Analog (same file, existing pattern) | Match Quality |
|---|---|---|---|---|
| `firmware/src/sync.c` (urgent-refill branch, D-01/D-02/D-22) | controller (real-time control loop) | event-driven (tick-driven state machine) | `firmware/src/sync.c:950-965` (`g_bl_lock_extreme`/`BL_BREAK_DELTA_NORM` rail-relative pattern) | exact |
| `firmware/src/sync.c` (ms+mm trip block, D-09) | controller | event-driven | `firmware/src/sync.c:1638-1672` (`sync_check_tension_dwell_and_ramp`, existing ms trip) | exact |
| `firmware/src/sync.c` (arm-flag/extreme reset sites, D-10/D-12/D-22) | controller | event-driven | `firmware/src/sync.c:1154,1192,1297,1305,1518` (`g_sync_tension_pin_since_ms` reset sites) | exact |
| `firmware/src/sync.c` (probe hook, D-15/D-16) | controller | event-driven | `firmware/src/sync.c:950-965` (extreme-tracking pattern, reused for "moved off rail") | exact |
| `firmware/src/sync_buf.c` (mm trip threshold reuse, D-07/D-16) | service (accumulator/state owner) | CRUD (accumulate/reset) | `firmware/src/sync_buf.c:963-977` + `:790,833` (`g_sync_refill_effort_mm` accumulate + `buf_update()` reset) | exact — reuse existing var, no new counter |
| `firmware/src/protocol.c` (SET/GET for 2 new knobs + `PROBE:` cmd) | controller (protocol/command dispatch) | request-response | `firmware/src/protocol.c:533-534,1066-1067` (`SYNC_KP_RATE` ungated persisted template) — NOT `:571-590,1098-1136` (`SYNC_TENSION_STOP_MS`, dev-tuning-gated, wrong template) | exact (persisted); anti-pattern flagged (dev-gated) |
| `firmware/src/protocol_status.c` (`TM:`/`ARM:`/`PR:` tail fields) | controller (telemetry serializer) | request-response (polled status line) | `firmware/src/protocol_status.c:68-80` (existing extended `ST:` tail: `SYNC_REFILL_MM:`, `TT:`, etc.) | exact |
| `firmware/src/settings_store.c` (TLV round-trip for 2 knobs) | model (persistence layer) | CRUD (flash read/write) | `firmware/src/settings_store.c:90,201,440,767-769,951` (`TAG_SYNC_KP_SPS`/`g_sync_kp_sps` full round-trip) | exact |
| `firmware/include/settings_store.h` (new `TAG_*` entries) | model (schema/enum) | CRUD | `firmware/include/settings_store.h` tag enum, ends `TAG_FLASH_ERASE_COUNT = 64` | exact |
| `config.ini.example` + `scripts/gen_config.py` (new `DEFAULTS` entries; `tune.h`/`config.ini` regenerate from these) | config | batch (codegen) | `scripts/gen_config.py:553` (`CONF_SYNC_KP_SPS` generation line) + `config.ini.example` existing `sync_kp_rate`-style rows | exact |
| `tests/host/sim_scenario.c` + `sim_plant.c` (new `sem_psf_*` scenarios, rail-scale twins) | test | event-driven (deterministic scenario replay) | `tests/host/sim_scenario.c:576-591` (`bl_retract_immediate`/`bl_retract_paused` + `type_p_rail_scale` twin pattern), `tests/host/sim_plant.c:178-179` (`type_p_rail_scale` application) | exact |
| `scripts/test_sync_sim.py` (new test methods) | test | event-driven | existing `sensor_type="p"` test-method pattern in same file (20 existing invocations, per RESEARCH.md A1) | exact |
| `MANUAL.md` (new knob rows + `PROBE:` doc) | config (docs) | — | existing knob-table rows + dev-tuning-gating note (`MANUAL.md:100-230`) | exact |

## Pattern Assignments

### `firmware/src/sync.c` — bounded relief (D-01/D-02), replacing the urgent-refill branch

**Analog:** same file, `sync.c:2009-2025` (branch to replace) + `sync.c:950-965` (rail-relative pattern to clone) + `sync.c:1857-1906` (`sync_apply_type_p_smoothing`, the path to route through)

**Current code being replaced** (`sync.c:2009-2025`, verbatim):
```c
if (fast_brake_active) {
    g_sync_current_sps = 0;
    g_psf_target_filt = 0.0f;
} else if (g_buf_sensor_type == BUF_SENSOR_TYPE_P &&
           buf_pos_norm() < -CONF_PSF_SOFT_WALL_START && target_sps > g_sync_current_sps) {
    g_sync_current_sps = target_sps;          // <-- D-01 replaces: snap to target_sps (trends to max_sps)
    g_psf_target_filt = g_extruder_est_sps;
} else if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
    float dt_s = (float)g_sync_tick_ms / MS_PER_SECOND_F;
    g_sync_current_sps = sync_apply_type_p_smoothing(target_sps, dt_s);
}
```
New shape (per D-01/D-02/D-22): compute `relief_target = min(max_sps, max(est * SYNC_PSF_RELIEF_MULT, baseline_sps))`, feed it through `sync_apply_type_p_smoothing()` with a doubled `g_sync_psf_slew_per_mm` while pegged, and gate entry on the rail-relative extreme (`g_buf_pos <= g_sync_tension_extreme + SOFT_WALL_MARGIN_NORM`) instead of `buf_pos_norm() < -CONF_PSF_SOFT_WALL_START`.

**Rail-relative extreme pattern to clone** (`sync.c:950-965`, verbatim):
```c
if (g_buf_sensor_type == BUF_SENSOR_TYPE_P) {
    if (g_bl_target_state == BUF_TENSION) {
        if (g_buf_pos < g_bl_lock_extreme)
            g_bl_lock_extreme = g_buf_pos;
        lock_broken = (g_buf_pos > g_bl_lock_extreme + BL_BREAK_DELTA_NORM);
    } else if (g_bl_target_state == BUF_COMPRESSION) {
        if (g_buf_pos > g_bl_lock_extreme)
            g_bl_lock_extreme = g_buf_pos;
        lock_broken = (g_buf_pos < g_bl_lock_extreme - BL_BREAK_DELTA_NORM);
    }
}
```
`BL_BREAK_DELTA_NORM` definition (`firmware/include/sync_internal.h:33-35`):
```c
#define PSF_BREAK_THRESHOLD_NORM 0.75f
#define BL_BREAK_DELTA_NORM (1.0f - PSF_BREAK_THRESHOLD_NORM)   /* = 0.25f */
```
D-22's new `g_sync_tension_extreme` is a direct clone of this variable/comparison shape; D-23 reuses `BL_BREAK_DELTA_NORM` itself rather than a new constant.

---

### `firmware/src/sync.c` — mm trip alongside ms trip (D-07/D-09)

**Analog:** same file, `sync.c:1638-1672` (existing ms trip block, verbatim)
```c
static int sync_check_tension_dwell_and_ramp(lane_t *lane, buf_state_t s, int target_sps,
                                             uint32_t now_ms) {
    if (s == BUF_TENSION && g_sync_tension_pin_since_ms != 0) {
        uint32_t tension_dwell_ms = now_ms - g_sync_tension_pin_since_ms;
        if (g_buf_sensor_type != BUF_SENSOR_TYPE_D && g_sync_tension_dwell_stop_ms > 0 &&
            tension_dwell_ms >= (uint32_t)g_sync_tension_dwell_stop_ms) {
            if (sync_try_runout_escalation(lane, now_ms)) {
                return -1;
            }
            sync_fault_hold();
            g_extruder_est_last_update_ms = now_ms;
            sync_apply_to_active();
            cmd_event("SYNC", "FAULT_HOLD");
            return -1;
        }
        if (g_sync_tension_ramp_delay_ms > 0 &&
            tension_dwell_ms >= (uint32_t)g_sync_tension_ramp_delay_ms) {
            int max_sps = sync_clamp_max_sps(g_sync_max_sps);
            if (target_sps < max_sps)
                target_sps = max_sps;
        }
    }
    return target_sps;
}
```
Add the mm condition in the **same block**, reading `g_sync_refill_effort_mm` (declared/updated in
`sync_buf.c`, see below) against `SYNC_TENSION_STOP_MM`; on trip, emit `EV:SYNC:TENSION_STOP:MM` vs
`:MS` per D-09, keep the `g_buf_sensor_type != BUF_SENSOR_TYPE_D` guard (D-14) on both. The
`g_sync_tension_ramp_delay_ms` branch (lines 1664-1669) is the one D-04 requires be capped by the
same relief bound — currently raises `target_sps` unconditionally to `max_sps`.

**Reset-site pattern for the new arm flag / extreme (D-10/D-12/D-22)** — five verified sites in
`sync.c`:
```c
// :78  declaration
uint32_t g_sync_tension_pin_since_ms = 0;
// :1154 sync_disable()
    g_sync_tension_pin_since_ms = 0;
// :1192 sync_rearm_active()
    g_sync_tension_pin_since_ms = was_tension ? now_ms : 0;
// :1297,:1305 sync_on_transition()
    if (now_state == BUF_TENSION) { g_sync_tension_pin_since_ms = now_ms; ... }
    else if (prev == BUF_TENSION) { g_sync_tension_pin_since_ms = 0; g_bl_autostart_suppressed = false; }
// :1518 sync_tick_auto_start_stop()
    g_sync_tension_pin_since_ms = (g_buf.state == BUF_TENSION) ? now_ms : 0;
```
The new arm flag and `g_sync_tension_extreme` must be (re)set at every one of these five sites so
mm/ms/probe never drift apart (D-12).

---

### `firmware/src/sync_buf.c` — reuse existing accumulator (D-07/D-16), do NOT add a parallel counter

**Analog:** same file, `sync_buf.c:963-977` (accumulate) + `:790,833` (reset), verbatim:
```c
if (g_buf.state == BUF_TENSION) {
    g_sync_refill_effort_mm += delta_mm;
    if (!g_sync_cannot_refill_warned &&
        g_sync_refill_effort_mm >= CONF_SYNC_CANNOT_REFILL_MM) {
        g_sync_cannot_refill_warned = true;
        cmd_event("SYNC", "cannot_refill");
    }
} else if (g_buf.state == BUF_COMPRESSION) {
    g_sync_relieve_effort_mm += delta_mm;
    ...
}
```
```c
void buf_update(buf_state_t new_state, uint32_t now_ms) {
    if (new_state == g_buf.state) return;
    ...
    g_sync_refill_effort_mm = 0.0f;
    g_sync_relieve_effort_mm = 0.0f;
    g_sync_cannot_refill_warned = false;
    g_sync_cannot_relieve_warned = false;
```
**Flag for the plan (RESEARCH.md Pitfall 1):** `CONF_SYNC_CANNOT_REFILL_MM` (50 mm warn-only) and
the new `SYNC_TENSION_STOP_MM` (32 mm default, hard trip) read the same accumulator; at defaults the
new trip fires first and resets the accumulator, so the 50 mm warn becomes practically unreachable
for type-P. The plan must explicitly document this as accepted (or scope `cannot_refill` to type-D)
— do not leave it silent.

---

### `firmware/src/protocol.c` — persisted knob template (D-28), avoid the dev-tuning-gated template

**Analog (correct template):** `protocol.c:533-534` (GET) + `:1066-1067` (SET), verbatim, `SYNC_KP_RATE` — **ungated**:
```c
else if (!strcmp(param, "SYNC_KP_RATE"))
    snprintf(out, out_len, "SYNC_KP_RATE:%.1f", (double)sps_to_mm_per_min(g_sync_kp_sps));
else if (!strcmp(base_param, "SYNC_KP_RATE"))
    g_sync_kp_sps = clamp_i(mm_per_min_to_sps(fv), 0, MAX_RUN_RATE_SPS);
```
**Anti-pattern to avoid (RESEARCH.md Pitfall 2):** `protocol.c:571-590` (GET) and `:1098-1136` (SET)
wrap `SYNC_TENSION_STOP_MS`/`SYNC_TENSION_RAMP_MS` in `#ifdef FLARE_DEV_TUNING` and have no TLV tag
— this is the "nearest by proximity" pattern but is explicitly the wrong shape for
`SYNC_PSF_RELIEF_MULT`/`SYNC_TENSION_STOP_MM` per D-28 (must be release-build SET/GET, flash-persisted).
Every SET handler in this file clamps via `clamp_i`/`clamp_f` before storing (e.g.
`g_sync_tension_dwell_stop_ms = clamp_i(iv, 0, 30000);` at `protocol.c:1115`) — apply the same
clamp-on-set discipline to the two new knobs (relief mult should never exceed 1.0 per D-01's outer
`min(max_sps, ...)` already providing a hard ceiling regardless).

**`PROBE:` command wiring:** use the `new-cmd` project skill pattern (per CONTEXT.md Claude's
Discretion) — locate existing single-shot command handlers in `protocol.c` for the scaffold shape
rather than copying the SET/GET pattern above (which is param-value, not a fire-once command).

---

### `firmware/src/protocol_status.c` — extended `ST:` tail additions (D-11/D-19)

**Analog:** same file, `protocol_status.c:68-80`, existing tail fields (verbatim shape):
```c
"SYNC_REFILL_MM:%.1f", ...   // one of RT:,TT:,CT:,SK:,CF:,ES:,TPX:,CB:,BPV:,MK:,
                             // SYNC_REFILL_MM:,SYNC_RELIEVE_MM:,TF:,FL_RATE:,UL_RATE:
```
Append `TM:<mm>`, `ARM:<0|1>`, `PR:<0|1|2|3>` in the same tail style, next to `TT:` per D-11.

**Flag for the plan (RESEARCH.md Pitfall 3):** `STATUS_LINE_MAX = CMD_LINE_MAX - 8`
(`protocol_status.c:16`). The tail is already long; adding 3 more fields risks silent `snprintf`
truncation with no compile/runtime error. The plan should verify formatted worst-case line length
against `STATUS_LINE_MAX` (e.g., a test that asserts `snprintf`'s return `< sizeof(buf)`).

---

### `firmware/src/settings_store.c` + `firmware/include/settings_store.h` — TLV persistence (D-28)

**Analog:** verified full round-trip for `TAG_SYNC_KP_SPS`/`g_sync_kp_sps`:
```c
// settings_store.h:59
TAG_SYNC_KP_SPS = 39,
// settings_store.c:90 (struct field), :201 (default), :440 (emit), :767-769 (load), :951 (apply)
int sync_kp_sps;
g_sync_kp_sps = CONF_SYNC_KP_SPS;                   // settings_defaults()
tlv_emit_i32(&w, TAG_SYNC_KP_SPS, g_sync_kp_sps);   // settings_save()
case TAG_SYNC_KP_SPS:
    if (len == sizeof(int)) memcpy(&g_sync_kp_sps, val, sizeof(int));
    break;                                            // settings_load_tlv_tag()
g_sync_kp_sps = s->sync_kp_sps;                      // settings_apply()
```
**Flag for the plan (RESEARCH.md Pitfall 4):** tag enum currently ends `TAG_FLASH_ERASE_COUNT = 64`.
Re-grep `TAG_` in `settings_store.h` immediately before assigning `65`/`66` to the two new tags —
other in-flight work may have appended tags since this research. `scripts/test_settings_parity.py`
catches missing pairings but NOT a numeric collision between two otherwise-correct tags.

---

### `config.ini.example` + `scripts/gen_config.py` — codegen source for `tune.h`/`config.ini` (both gitignored)

**Analog:** `scripts/gen_config.py:553` (verbatim):
```python
f"#define CONF_SYNC_KP_SPS        {mm_min_to_sps(get('sync_kp_rate'), l1)}",
```
Add matching `DEFAULTS`/generation lines for `sync_psf_relief_mult` and `sync_tension_stop_mm`, plus
corresponding rows in the tracked `config.ini.example` (mirroring the existing `sync_kp_rate` row
shape). Do NOT hand-edit `firmware/include/tune.h` or `config.ini` directly — both regenerate from
these tracked sources via `scripts/gen_config.py`.

---

### `tests/host/sim_scenario.c` + `sim_plant.c` — new scenarios, rail-scale twins (D-25, SC#4)

**Analog:** `sim_scenario.c:576-591`, the `bl_retract_immediate`/`bl_retract_paused` +
`type_p_rail_scale` twin pattern (existing, verbatim structure — paired scenario names differing
only by rail-scale plant knob), and `sim_plant.c:178-179` (`type_p_rail_scale` application in the
plant model). Clone this shape for the new `sem_psf_*` family: every new scenario runs at
`type_p_rail_scale` 1.0 and 0.7 (D-25), plus a dedicated 0.5 tripwire asserting
`EV:SYNC:RELIEF_ON` still fires (the exact `51bdca8` failure class).

**Test runner analog:** `scripts/test_sync_sim.py`, existing `sensor_type="p"` invocation pattern
(20 existing call sites) — add new test methods following the same shape.

---

### `MANUAL.md` — knob documentation (D-28)

**Analog:** existing knob-table rows + the dev-tuning-gating note at `MANUAL.md:100-230` (which
currently lists `SYNC_TENSION_STOP_MS`/`SYNC_TENSION_RAMP_MS` as dev-build-only — do not add the two
new knobs to that list; they belong in the release-build persisted table instead, per D-28).

## Shared Patterns

### Rail-relative comparison (applies to relief entry, mm-trip pinning check, and probe "moved off rail")
**Source:** `firmware/src/sync.c:950-965` + `firmware/include/sync_internal.h:33-35`
**Apply to:** every new threshold in this phase (D-22, D-23, D-25). Never use an absolute
`buf_pos_norm() < -0.8`-style literal — this is the exact `51bdca8` root cause the phase's carried
caveat exists to prevent. Clone `g_bl_lock_extreme`'s "deepen while active, reset on rearm, compare
via `BL_BREAK_DELTA_NORM`" shape.

### Single accumulator, no parallel counters
**Source:** `firmware/src/sync_buf.c:963-977,790,833`
**Apply to:** mm trip (D-07) and probe (D-15/D-16) both — read `g_sync_refill_effort_mm` at two
thresholds (16 mm probe, 32 mm trip); do not introduce a second counter that could drift.

### Persisted knob 7-touchpoint path (D-28)
**Source:** `SYNC_KP_RATE`/`g_sync_kp_sps` full chain — `settings_store.h` tag enum →
`settings_store.c` (struct field, default, emit, load, apply) → `protocol.c` ungated SET/GET →
`scripts/gen_config.py` + `config.ini.example` → `MANUAL.md` row.
**Apply to:** `SYNC_PSF_RELIEF_MULT`, `SYNC_TENSION_STOP_MM`.
**Explicitly do NOT copy:** `SYNC_TENSION_STOP_MS`'s `#ifdef FLARE_DEV_TUNING`-gated, non-persisted
shape (`protocol.c:571-590,1098-1136`) — wrong template for these two knobs.

### Type-D exclusion guard
**Source:** `firmware/src/sync.c:1641` — `g_buf_sensor_type != BUF_SENSOR_TYPE_D`
**Apply to:** every new mm-trip/probe/relief code path (D-14, D-17) — type-D relay TENSION contact
is a normal refill signal, not a fault; all new behavior gates on `BUF_SENSOR_TYPE_P` explicitly.

### Edge-only events, no per-tick spam
**Source:** `cmd_event("SYNC", "FAULT_HOLD")` / `cmd_event("SYNC", "cannot_refill")` pattern used
throughout `sync.c`/`sync_buf.c`
**Apply to:** `EV:SYNC:RELIEF_ON`/`RELIEF_OFF` (D-06), `EV:SYNC:TENSION_STOP:MM`/`:MS` (D-09),
`EV:SYNC:PROBE:CONSUMER`/`:NO_CONSUMER` (D-19) — fire once on the edge/completion, not per tick.

## No Analog Found

None — RESEARCH.md's "Don't Hand-Roll" section confirms every mechanism this phase needs (mm
accumulator, extreme-tracking, distance-EMA smoothing, TLV persistence) already has a working,
hardware-validated analog in the exact files being modified. This phase is additive threshold/
comparison logic wired into existing machinery, not new subsystems.

## Metadata

**Analog search scope:** `firmware/src/{sync,sync_buf,sync_analog,protocol,protocol_status,
settings_store}.c`, `firmware/include/{settings_store,tune_internal,sync_internal,
controller_shared}.h`, `tests/host/{sim_scenario,sim_plant}.c`, `scripts/{gen_config,
test_sync_sim}.py`, `config.ini.example`, `MANUAL.md`
**Files scanned:** 17 (all confirmed git-tracked via `git ls-files`; `firmware/include/tune.h` and
`config.ini` confirmed gitignored generator outputs, substituted with their tracked generator
sources per the tracked-source gate)
**Pattern extraction date:** 2026-09-12
**Primary source:** `.planning/phases/13-type-p-sync-relief-fault-trip/13-RESEARCH.md` (all excerpts
independently re-verified as git-tracked paths this session; no additional codebase search was
needed since RESEARCH.md already contains this-session verbatim, line-cited reads of every
integration point)
