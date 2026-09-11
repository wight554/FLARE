# Two-axis review of `79f7a952..2a24a33` (2026-09-11)

Method: `/code-review` — Standards axis + Spec axis (firmware, host) as parallel sub-agents,
validating against `.planning/specs/*/spec.md`, per-phase `*-SPEC.md`/`*-PLAN.md`, and the
restored decision notes in `.planning/decisions/archive/*/design.md`.

Gates at HEAD: `ninja -C build_local` PASS · 133 Python tests PASS · `test_persistence`,
`test_tmc_recovery` PASS · clang-format/ruff clean · clang-tidy 15 project warnings.

## Firmware spec/regression axis
| # | Sev | Finding | Code | Contract |
|---|---|---|---|---|
| 1 | HIGH | `TC_LOAD_PARK` forward `TASK_MOVE` after COMPRESSION-completed load → `FAULT:MOVE_COMPRESSION`, `FAULT_BUF` latched, sync refused rest of print | `toolchange.c:368`, `motion.c:501-505`, `sync.c:1233` | 06-SPEC §4.2, klipper-integration |
| 2 | HIGH | `cutter_abort`/`cutter_fail` set block then immediately `servo_idle()` (PWM off) — blade limp mid-stroke | `cutter.c:178-179,190-191` | cutter-feed-timeout |
| 3 | HIGH | Bare `BL:T/C` now arms full-travel follow + `SYNC_MAX_SPS` seed on both types | `sync.c:829-831,957,969-975` | buffer-state-lock D5, 02-SPEC |
| 4 | MED | `GET:BUF_POS_RAW` calls `buf_analog_update()` — mutates estimator, not type-gated | `protocol.c:509-512` | psf-type-p-sensor |
| 5 | MED | Heartbeat lockout misses `BL_LOCKED`/`SYNC_ACTIVE`/`RELIEF_PAUSE`/`FAULT_HOLD`; `sleep_ms` ×2 blocks loop; no fault latch → 1 Hz `stop_all` storm | `motion.c:684-707`, `protocol.c:204-217` | 10-SPEC §2.1 |
| 6 | MED | `STOP`/`PA` alias `ST` → `set_toolhead_filament(false)` | `protocol.c:1783-1791` | 06-SPEC §3.2 |
| 7 | LOW | `BS` no-op path calls `buf_force_stable_state(NEUTRAL)` → `g_buf_pos=0` | `sync.c:234` | fix-typep-relief-pause-rearm-strand D3 |
| 8 | LOW | Retry exhaustion emits `LOAD_TIMEOUT` not `TS_NOT_HIT`; retries on RUNOUT | `toolchange.c:382`, `motion.c:471-483` | 06-SPEC §4.1 |

Clean: persistence-contract/07/08 (64 TLV tags unique, v63 migration field-complete, clamps
identical, readback-verify before flip), relay-fallback-only, compression-overfeed-stop,
type-d-dynamic-flow, reserve-safety-floor, sync-feedback, psf-stale-fault-timers, BL goal
override reset, filament-bypass interlocks, klipper-mmu-config (purge M83), protocol prefixes.

## Host spec/regression axis
| # | Sev | Finding | Code | Contract |
|---|---|---|---|---|
| 1 | HIGH | `BYPASS` in `DUMP_PARAMS` → dumped config fails `gen_config` unknown-key check | `flare_cmd.py:185` | config-surface-tiers:85-89, tier-config-surface Migration |
| 2 | MED | Push skipped while Printing has no retry; mirror stale ≤10 s | `flare_daemon.py:1631-1636` | daemon-klipper-mirror |
| 3 | MED | `--trust-proxy` + loopback bind → no auth | `flare_daemon.py:851-870,905` | 09-SPEC |
| 4 | LOW | 4 non-TestCase test files not collected; runners hard-coded | `validate_regression.py:140-143` | docs-test-overhaul D1, prune-dead-diagnostic-scripts |
| 5 | LOW | `--chart` without `--out` bypasses safe-mode refusal | `flare_analyze.py:1266-1274` | analyzer-rigor |
| 6 | LOW | `lane*_task` not in `keys_to_check` | `flare_daemon.py:605-609` | daemon-klipper-mirror |
| 7 | LOW | No Claude `Co-Authored-By` on 21 commits | — | task-workflow:52 |
| D | MED | `--host` default `127.0.0.1` re-introduces REVERTED decision audit-hardening-fixes 4.1 | `flare_daemon.py:1671` | decision 4.1 |

Clean: loopback exemption path for Klipper shell → `flare_cmd.py`, rate limiter never touches
loopback, EV: → `trigger_klipper_sync`, gcode backpressure D1–D4, BL/BS wait semantics, live
tuner SV policy, calibrate wizard tier compliance, no gate loosened (stress margin tightened),
parity test stronger (64/64 tags).

## Standards axis
Hard: `s_tmc_heartbeat_*` prefix (STYLE §2); `settings_t_v63` typedef suffix; `<stdio.h>`
after project headers (`motion.c:19`); TC_TS clamp literals duplicated
`protocol.c:1183-1187`/`settings_store.c:560-562`; `toolchange.c:377` `50.0f`; `settings_store.c`
962 lines; MANUAL.md missing `BL:BREAK`, `TC:TS_PARKED`. Rules 4/7/8 parity OK.
Judgement: Shotgun Surgery in 4 parallel settings lists; duplicated hunks `sync.c:913/933`,
`968/1024`; Bearer header ×3 in `flare_cmd.py`; dead `settings_validate()`; `g_seq` name;
`g_tmc_health` int-as-bool; `is_motion_cmd` hand list; `validate_regression.py:68` writes
`config.ini` into repo.

## Migration audit (eb0a942)
37 specs + 9 open changes byte-identical in `.planning/`; 5 AI-meta specs dropped on purpose;
64 archived decision changes were lost → restored in `eeb6cff` as `.planning/decisions/archive/`.
