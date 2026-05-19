## Context

Two cutter safety timeouts exist in the state machine (`firmware/src/cutter.c`):

| Runtime global | Used in states | Hardcoded default | Source |
|---|---|---|---|
| `CUT_TIMEOUT_FEED_MS` | `CUT_FEED_WAIT` | 5 000 ms | `config.h:CONF_CUT_FEED_MS` |
| `CUT_TIMEOUT_SETTLE_MS` | `CUT_OPEN_WAIT`, `CUT_CLOSE_WAIT`, `CUT_REOPEN_WAIT` | 1 500 ms | `config.h:CONF_CUT_SETTLE_MS` |

Both constants are defined only in `firmware/include/config.h` and are not generated through the `config.ini` → `gen_config.py` → `tune.h` pipeline. Neither has a `settings_t` field, a `GET:`/`SET:` handler, or a `--dump` entry.

`CUT_TIMEOUT_FEED_MS = 5000 ms` is the active bug: with `CUT_FEED_MM = 145 mm` at `cut_feed_rate = 600 mm/min`, `feed_initial_ms ≈ 14 500 ms`. The timeout fires at 5 s → `cutter_abort()` → `CUT:ERROR ABORTED`, no physical cut.

`CUT_TIMEOUT_SETTLE_MS = 1500 ms` is structurally identical and wired the same way. Both are fixed in this change to avoid reopening the same files later.

## Goals / Non-Goals

**Goals:**
- `cut_feed_timeout_ms` and `cut_settle_timeout_ms` become first-class runtime tunables: `config.ini` → `gen_config.py` → `CONF_CUT_FEED_MS` / `CONF_CUT_SETTLE_MS` → `settings_t` → `GET:`/`SET:` → `--dump`.
- Default `cut_feed_timeout_ms = 30000` ms (covers ≤300 mm feeds at ≥60 mm/min with margin).
- Default `cut_settle_timeout_ms = 3000` ms (doubled from current 1500 ms; gives headroom if `SERVO_SETTLE_MS` is raised near its 2000 ms cap).
- `SETTINGS_VERSION` bumped; existing flash settings re-default cleanly.
- No change to `cutter.c` logic — only the constants it reads change.

**Non-Goals:**
- Dynamic timeout derived from `feed_active_ms` — keeps the explicit safety cap model already established by `TC_CUT_MS`.
- Cutter state-machine redesign.
- Changing any other cutter parameter.

## Decisions

### Wire both timeouts, not just `CUT_TIMEOUT_FEED_MS`

`CUT_TIMEOUT_SETTLE_MS` has the exact same structural gap. If `SERVO_SETTLE_MS` is raised to 2000 ms (its clamp max), a 1500 ms settle timeout aborts every wait state — a latent bug. Fixing both in one pass eliminates the need to revisit.

### Use `config.ini` pipeline (not dynamic computation)

Alternative: derive timeout as `feed_active_ms + constant_margin` inside `cutter_tick`. Rejected: loses explicit operator control, diverges from the `TC_CUT_MS` precedent already established in the codebase, and makes the "why did it abort?" debugging harder.

### Protocol token: `CUT_FEED_MS` / `CUT_SETTLE_MS`

Mirrors `TC_CUT_MS`. Short, unambiguous. GET/SET clamps: `CUT_FEED_MS` → `[1000, 120000]`; `CUT_SETTLE_MS` → `[500, 10000]`.

### Default `cut_feed_timeout_ms = 30000` ms

Covers `CUT_FEED_MM` up to ~300 mm at minimum feed rate (100 mm/min → 180 s is beyond physical reality; 30 s at 600 mm/min covers 300 mm). Large enough to never spuriously fire in practice, small enough to not hang the cutter indefinitely on a real jam.

## Risks / Trade-offs

- `SETTINGS_VERSION` bump resets all flash settings to defaults → user must re-apply tuning after reflash. Same risk as every settings-struct change; documented in migration plan.
- Raising `cut_settle_timeout_ms` default from 1500 → 3000 ms means a genuinely stuck servo waits 3 s before abort instead of 1.5 s. Acceptable: the extra 1.5 s is invisible in normal operation; the abort is still deterministic.

## Migration Plan

1. Flash new firmware → settings version mismatch → `settings_defaults()` runs → globals set from `CONF_CUT_FEED_MS = 30000` and `CONF_CUT_SETTLE_MS = 3000`.
2. User re-applies any custom tuning via `SET:` commands and `SV:`.
3. No rollback risk: old firmware ignores unknown config.ini keys; new firmware falls back to `config.h` constants if tune.h is regenerated without the new keys (but gen_config.py will always emit them after this change).

## Implementation Plan

### `config.ini` + `config.ini.example`
- Add `cut_feed_timeout_ms` and `cut_settle_timeout_ms` under Cutter / Servo so operator defaults live in the config source of truth.
- Risk: keep local config default aligned with generated fallback defaults.

### `scripts/gen_config.py` + `firmware/include/config.h`
- Add generator defaults and emit `CONF_CUT_FEED_MS` / `CONF_CUT_SETTLE_MS` from config.
- Keep `config.h` values as fallback defaults only because it still includes board/static constants.
- Risk: generated `tune.h` must contain both macros before firmware compile.

### `firmware/src/settings_store.c`
- Replace the old `cutter_settle_ms` persisted field with explicit `cutter_feed_timeout_ms` and `cutter_settle_timeout_ms`, wire defaults/save/load, and bump `SETTINGS_VERSION`.
- Risk: settings version mismatch intentionally resets flash settings; static assert must stay under flash buffer size.

### `firmware/src/protocol.c` + `scripts/flare_cmd.py`
- Add `SET:` / `GET:` handlers for `CUT_FEED_MS` and `CUT_SETTLE_MS`; add both to host `--dump`.
- Risk: lane-suffixed `GET:` must still behave like other global cutter params.

### `MANUAL.md`
- Document both parameters with range, default, and cutter-state-machine effect.
- Risk: docs must match protocol names, config keys, and clamp ranges exactly.
