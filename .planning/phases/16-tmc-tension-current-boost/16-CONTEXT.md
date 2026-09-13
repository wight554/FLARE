# Phase 16: TMC Tension Current Boost - Context

**Gathered:** 2026-09-13
**Status:** Ready for planning

<domain>
## Phase Boundary

Type-P analog buffer tension boost during active sync (`SYNC_ACTIVE`) only: dynamically raise gear motor `IRUN` to a dedicated safe limit (`SYNC_TENSION_BOOST_IRUN`) when severe tension is detected (`g_buf_pos <= SYNC_TENSION_BOOST_ON`), release back to baseline run current via hysteresis (`g_buf_pos >= SYNC_TENSION_BOOST_OFF`), and guarantee immediate unconditional de-boost on any sync exit, stop, or fault trip without desynchronizing Phase 10 TMC heartbeat shadow registers.

Out of scope: Type-D microswitch buffers (cannot sense proportional tension depth), extruder motor currents (not driven by FLARE), non-sync operations (pre-load, toolchange, cutting run at baseline current), and complex thermal modeling (governed by conservative 1200 mA hardware ceiling and Phase 13 fault timeouts).

</domain>

<spec_lock>
## Requirements (locked via SPEC.md)

**7 requirements are locked.** See `16-SPEC.md` for full requirements, boundaries, and acceptance criteria.

Downstream agents MUST read `16-SPEC.md` before planning or implementing. Requirements are not duplicated here.

**In scope (from SPEC.md):**
- Type-P analog buffer tension boost during `SYNC_ACTIVE` only.
- Per-lane boost current target `SYNC_TENSION_BOOST_IRUN` in mA (clamped to 1200 mA ceiling).
- Hysteresis thresholds `SYNC_TENSION_BOOST_ON` (on) and `SYNC_TENSION_BOOST_OFF` (off).
- Edge-triggered UART writes to TMC `IHOLD_IRUN` register and shadow state synchronization.
- Unconditional reset of boost on all exits from `SYNC_ACTIVE`.
- Telemetry integration: `ST:` status token (`TB:0`/`TB:1`) and `EV:TMC:BOOST`/`EV:TMC:NORMAL` events.
- Configuration in `config.ini`, `scripts/gen_config.py`, SET/GET commands, `--dump`, and host sim tests.

**Out of scope (from SPEC.md):**
- Type-D microswitch buffer boost — Type-D cannot measure proportional tension severity.
- Extruder motor boost — FLARE does not control the toolhead extruder motor.
- Non-sync operations — Preload, load, unload, cutting, and toolhead parking run at standard currents.
- Thermal motor modeling — Motor thermal protection is enforced via conservative hard mA ceiling (1200 mA) and existing Phase 13 fault timeouts.

</spec_lock>

<decisions>
## Implementation Decisions

### Firmware Module & Call Placement
- **D-01:** Dedicated `sync_check_tension_boost(uint32_t now_ms)` function implemented in `firmware/src/sync.c` and executed directly from `sync_tick()` during `SYNC_ACTIVE`. This cleanly checks both the activation threshold (`g_buf_pos <= g_sync_tension_boost_on`) and the hysteresis release threshold (`g_buf_pos >= g_sync_tension_boost_off`) after `g_buf_signal` is published.
- **D-02:** Centralized unconditional boost unwind in `sync_set_state()` whenever transitioning out of `SYNC_ACTIVE`, with defense-in-depth safety cleanup calls in `stop_all()` and `sync_disable()`. Guaranteed zero current leakage across any exit path (pause, stop, toolchange, fault hold, lane switch).

### TMC UART Apply & Shadow Register Coupling
- **D-03:** Dedicated helper `tmc_apply_active_run_current(int lane_num, int current_ma)` in `firmware/src/motion.c` that clamps current to 1200 mA, calls `tmc_set_run_current_ma()`, and atomically updates `g_shadow_vsense[idx]` and `g_shadow_ihold_irun[idx]` via `build_ihold_irun_reg()`.
- **D-04:** Heartbeat recovery consistency: `sync_tmc_settings(lane)` resolves the active current through a boost query helper (`sync_get_active_run_current_ma(lane)`), ensuring simulated or idle brownout recovery re-applies the true active current (boosted if active, base if inactive).
- **D-05:** Boost activation qualification: Boost only engages if configured `SYNC_TENSION_BOOST_IRUN[lane] > g_tmc_run_current_ma[lane]`. If boost is 0 (default) or less than/equal to base run current, the feature is treated as disabled/no-op, preventing unwanted current reductions or spurious UART writes.

### Protocol ST: Budget & Telemetry Structure
- **D-06:** Telemetry token `,TB:%c` appended to the extended status tail in `firmware/src/protocol_status.c` (rendering `'1'` if active lane is boosted, `'0'` otherwise). This uses exactly 5 chars, fitting within `STATUS_LINE_MAX` (759/760 chars worst-case). Update `scripts/test_status_line_budget.py` to maintain mathematical proof of buffer headroom.
- **D-07:** Host parity: `flare_daemon.py` and `flare_cmd.py` parse `TB` into `tension_boost` boolean; `scripts/flare_cmd.py --dump` includes boost configuration keys and live state.

### Host Sim Test Suite & Gating Strategy
- **D-08:** Structured host testing:
  1. Dedicated unit test file `tests/host/test_tmc_boost.c` verifying activation/release hysteresis, 1200 mA clamping, prohibition on Type-D and compression, edge-only UART writes (no per-tick writes), and unconditional exit unwind.
  2. Integration plant simulation scenario in `tests/host/sim_scenario.c` (`sem_psf_tension_boost`) asserting boost response under physical spool drag and recovery.
- **D-09:** Release & HW gating policy: Shipped with `SYNC_TENSION_BOOST_IRUN = 0` by default (disabled). Full CI/sim test suite passes before merge. Product Owner (PO) validates code and performs physical test rig thermal validation (`HW:` task) before considering default-on activation. — **Reversibility:** reversible.

### Builder Discretion
- Internal variable naming (`g_sync_tension_boost_active[NUM_LANES]`, `g_sync_tension_boost_on`, `g_sync_tension_boost_off`).
- Exact formatting of `EV:TMC:BOOST:<lane>` and `EV:TMC:NORMAL:<lane>` event helper calls.
- Selection of settings TLV tag IDs in `firmware/include/settings_store.h`.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Specifications & Research
- `.planning/phases/16-tmc-tension-current-boost/16-SPEC.md` — Locked functional requirements, edge coverage, and prohibitions.
- `.planning/research/2026-09-11-happy-hare-borrow-scan.md` §1.5 — Happy-Hare v4 `_check_tangle_prevention` analysis.
- `.planning/ROADMAP.md` § Phase 16 — Milestone goals and success criteria.

### Architecture & Standards
- `AGENTS.md` — Build rules, commit conventions, and `SETTINGS_VERSION` bump requirements.
- `STYLE.md` — C coding standards and naming conventions.
- `firmware/include/controller_shared.h` — Shared globals, lane state, and sync definitions.

### Subsystem Code References
- `firmware/src/sync.c` — Sync controller FSM and tick loop.
- `firmware/src/motion.c` — Motor control, TMC UART writes, and heartbeat recovery.
- `firmware/src/settings_store.c` — TLV settings persistence and `sync_tmc_settings()`.
- `firmware/src/protocol_status.c` — Status dump line formatting and budget constraints.
- `scripts/test_status_line_budget.py` — Automated verification of `STATUS_LINE_MAX` budget.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `build_ihold_irun_reg(run_ma, hold_ma, vsense)` (`firmware/src/main.c:309`): Reusable helper for computing the 32-bit `IHOLD_IRUN` register payload.
- `tmc_set_run_current_ma()` (`firmware/src/motion.c`): Existing driver UART write function.
- `g_shadow_ihold_irun[]` & `g_shadow_vsense[]`: Shadow tracking variables maintained for Phase 10 heartbeat recovery.
- `tests/host/sim_fakes.h`: Fakes for tracking TMC register write counts (`sim_tmc_get_write_count()`).

### Established Patterns
- Settings tunables live in `config.ini` -> `tune.h` -> runtime globals -> TLV tags in `settings_store.c` -> SET/GET in `protocol.c` -> `--dump` in `flare_cmd.py`.
- Any addition of fields to `settings_t` requires incrementing `SETTINGS_VERSION`.
- Status fields use single-character formatting (`%c`) for tight-budget flags (precedent: `ARM:`, `PR:`, `YS:`).
- Edge events emit via `cmd_event()` on state entry/exit only, avoiding continuous UART bus flooding.

### Integration Points
- `sync_tick()` in `firmware/src/sync.c`: Calls `sync_check_tension_boost()`.
- `sync_set_state()` in `firmware/src/sync.c`: Centralized reset hook.
- `stop_all()` in `firmware/src/motion.c`: Safety de-boost hook.
- `cmd_handle_status_dump()` in `firmware/src/protocol_status.c`: Adds `TB:%c`.

</code_context>

<specifics>
## Specific Ideas

- Product Owner (PO) validation role: PO validates code implementation directly and conducts hardware rig thermal validation tests before enabling boost by default.
- Zero UART overhead during normal printing: Writes occur strictly on threshold crossing edges.

</specifics>

<deferred>
## Deferred Ideas

- None — discussion stayed strictly within Phase 16 scope.

</deferred>

---

*Phase: 16-tmc-tension-current-boost*
*Context gathered: 2026-09-13*
