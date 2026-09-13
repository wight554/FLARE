# Phase 16: TMC Tension Current Boost - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-13
**Phase:** 16-tmc-tension-current-boost
**Areas discussed:** Firmware module & call placement, TMC UART apply & shadow register coupling, Protocol ST: budget & telemetry structure, Host sim test suite & fault edge coverage

---

## Firmware Module & Call Placement

| Option | Description | Selected |
|--------|-------------|----------|
| Dedicated sync_check_tension_boost() in sync_tick() | Called from sync_tick() during SYNC_ACTIVE (handles both ON edge and OFF release cleanly) | ✓ |
| Inline in sync_check_tension_dwell_and_ramp() | Runs only in tension zone; OFF release requires special check outside | |
| Inside sync_analog.c | Keeps Type-P logic grouped, but mixes motor register UART writes into math module | |

**User's choice:** Dedicated sync_check_tension_boost() called from sync_tick() during SYNC_ACTIVE (handles both ON edge and OFF release cleanly)
**Notes:** Evaluated after g_buf_signal is published; handles both ON and OFF edges in one unified location.

| Option | Description | Selected |
|--------|-------------|----------|
| Centralized in sync_set_state() + stop_all() | Centralized in sync_set_state() whenever leaving SYNC_ACTIVE, plus defense-in-depth call in stop_all() | ✓ |
| Distributed across individual exit handlers | Distributed across sync_disable(), sync_fault_hold(), stop_all(), lane_swap() | |

**User's choice:** Centralized in sync_set_state() whenever leaving SYNC_ACTIVE, plus defense-in-depth call in stop_all() (guarantees no exit path leaks boost)
**Notes:** Unconditional safety guarantee that boost cannot leak across any state change or stop.

---

## TMC UART Apply & Shadow Register Coupling

| Option | Description | Selected |
|--------|-------------|----------|
| Dedicated tmc_apply_active_run_current() + shadow update | Dedicated helper tmc_apply_active_run_current(lane, ma) that updates TMC driver, g_shadow_vsense, and g_shadow_ihold_irun atomically; sync_tmc_settings() resolves current via active boost getter | ✓ |
| Direct tmc_set_run_current_ma() with manual shadow sync | Direct tmc_set_run_current_ma() call in sync.c with manual shadow register manipulation inline | |

**User's choice:** Dedicated helper tmc_apply_active_run_current(lane, ma) that updates TMC driver, g_shadow_vsense, and g_shadow_ihold_irun atomically; sync_tmc_settings() resolves current via active boost getter
**Notes:** Keeps Phase 10 heartbeat recovery shadow registers consistent so simulated or idle recoveries never clobber active current.

| Option | Description | Selected |
|--------|-------------|----------|
| Only engage if boost > base run current | Only engage if SYNC_TENSION_BOOST_IRUN > base run current; treat boost <= base (or 0) as inactive/no-op | ✓ |
| Allow any non-zero value up to 1200 mA | Allow any non-zero value up to 1200 mA (even if lower than base current) | |
| Reject SET if boost <= base with ER:INVALID_PARAM | Strict configuration validation | |

**User's choice:** Only engage if SYNC_TENSION_BOOST_IRUN > base run current; treat boost <= base (or 0) as inactive/no-op (avoids accidental current drops or spurious UART traffic)
**Notes:** Avoids accidental torque reduction and unnecessary UART bus transactions.

---

## Protocol ST: Budget & Telemetry Structure

| Option | Description | Selected |
|--------|-------------|----------|
| Append ,TB:%c to extended status tail | Append ,TB:%c to extended status tail in protocol_status.c; parse as tension_boost (bool) in daemon & flare_cmd.py; update test_status_line_budget.py (fits 759/760 worst-case) | ✓ |
| Replace or reuse existing debug token | Replace or reuse an existing debug token in ST: to maintain larger headroom margin | |

**User's choice:** Append ,TB:%c to extended status tail in protocol_status.c; parse as tension_boost (bool) in daemon & flare_cmd.py; update test_status_line_budget.py (fits 759/760 worst-case)
**Notes:** %c consumes 1 char on wire; test_status_line_budget.py verifies 759/760 char worst-case headroom.

---

## Host Sim Test Suite & Fault Edge Coverage

| Option | Description | Selected |
|--------|-------------|----------|
| Dedicated test_tmc_boost.c + sim_scenario.c plant test | Dedicated test_tmc_boost.c for unit & edge-invariants + integration scenario in sim_scenario.c (clean separation between exact register assertions and closed-loop plant physics) | ✓ |
| Add all tests into test_tmc_recovery.c and sim_scenario.c | Keeps TMC tests in existing files | |

**User's choice:** Dedicated test_tmc_boost.c for unit & edge-invariants + integration scenario in sim_scenario.c (clean separation between exact register assertions and closed-loop plant physics)
**Notes:** Fast unit test for register states + realistic plant simulation under spool drag.

| Option | Description | Selected |
|--------|-------------|----------|
| Ship with default boost = 0; PO validates code and HW rig thermal check | Ship with default SYNC_TENSION_BOOST_IRUN = 0 (disabled by default); full sim test suite passes in CI; PO validates code and performs HW rig thermal check before considering default-on | ✓ |
| Ship with conservative default (e.g. 800 mA) out of box | Enable boost out of the box | |

**User's choice:** Ship with default SYNC_TENSION_BOOST_IRUN = 0 (disabled by default); full sim test suite passes in CI; PO validates code and performs HW rig thermal check before considering default-on
**Notes:** Product Owner (PO) directly validates code and verifies real motor thermal performance on physical test rig before default-on activation.

---

## Builder's Discretion

- Internal variable naming (`g_sync_tension_boost_active[NUM_LANES]`, `g_sync_tension_boost_on`, `g_sync_tension_boost_off`).
- Exact formatting of `EV:TMC:BOOST:<lane>` and `EV:TMC:NORMAL:<lane>` event helper calls.
- Selection of settings TLV tag IDs in `firmware/include/settings_store.h`.

## Deferred Ideas

- None.
