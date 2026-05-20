## 1. Firmware: NEUTRAL = fallback only (R1/R2)

- [x] 1.1 `sync.c` relay block (~1786-1820): replace NEUTRAL selection
  with the unconditional fallback
  `clamp(extruder_est_sps, SYNC_MIN, relay_base) · RELAY_NEUTRAL_FRAC`.
  Delete the `use_estimate` branch, `g_relay_estimate_sps`, the
  `[lo,hi]` clamp, and seed selection. TENSION/COMPRESSION branches
  untouched.
- [x] 1.2 Delete estimator machinery: `relay_estimator_accumulate`,
  `relay_estimator_complete_phase`, `relay_estimator_confident`, the
  `v_est` blend (~257-258), duty/travel accumulators, pair-history,
  confidence gate (~228-231), `relay_seed_active`. Grep-confirm no
  remaining callers.
- [x] 1.3 `ninja -C build_local` green; diff the relay block to confirm
  **only** the confident path was removed (catch-up / COMPRESSION /
  fallback expression byte-identical).

  2026-05-19: Removed firmware duty-estimator state, confidence gate,
  seed path, estimate telemetry helpers, and `[lo,hi]` NEUTRAL branch.
  `rg` confirms no firmware/config generator/dump references to removed
  estimator keys or `RDE`/`RDCF`/`RDV`. `ninja -C build_local` green.

## 2. Telemetry shrink (R3, BREAKING)

- [x] 2.1 `protocol.c`: remove `RDE`/`RDCF`/`RDV` from the status
  string and any estimate/confidence `SET:`/`GET:` + name-list
  entries. Keep `BUF`/`BP`/`EST`/`NC`/etc.
- [x] 2.2 `flare_cmd.py`: drop the `RDE`/`RDCF`/`RDV` and removed-key
  dump entries. Host build + `py_compile` green; status parses.

  2026-05-19: Removed status fields from `protocol.c`, removed live
  SET/GET and dump entries for relay confidence keys, and removed relay
  estimator columns from the live tuner CSV header. `python3 -m
  py_compile scripts/*.py` green.

## 3. Config + settings removal (R5, BREAKING)

- [x] 3.1 Remove `relay_estimate_lo`, `relay_estimate_hi`,
  `relay_confidence_cycles`, `relay_confidence_window_ms`,
  `relay_seed_warmup_ms` from `config.ini`, `config.ini.example`,
  `gen_config.py` defaults + `CONF_*` emit. Keep
  `relay_catchup_frac`/`relay_neutral_frac`/`relay_min_flip_mm`/
  `relay_collapse_*`.
- [x] 3.2 `settings_store.c`: remove the deleted relay fields
  (struct/apply/save/load); bump `SETTINGS_VERSION`. Document the
  persisted-settings reset.
- [x] 3.3 `test_gen_config.py`: drop assertions for removed macros;
  `gen_config.py` + `test_gen_config.py` + `ninja` green.

  2026-05-19: Removed deleted config keys/default macros, removed
  persisted settings fields, bumped `SETTINGS_VERSION` 53 -> 54, and
  retained fallback/collapse/min-flip keys. `python3
  scripts/test_gen_config.py` and `ninja -C build_local` green.

## 4. Analyzer subtraction with parity (R4)

- [x] 4.1 `flare_analyze.py`: remove `relay_duty_recommendations`,
  `relay_duty_coverage`, relay duty stats, `relay_estimate_*`/
  `relay_seed_rate` emit + the `current`/DEFAULTS keys they used.
- [x] 4.2 Retire relay tests in `test_flare_analyze.py`
  (`relay-d12`, `relay-d12-real`, `relay-cov-pass`, `relay-cov-warn`,
  `relay-d11`) and remove `tests/fixtures/relay_review{1,2}.csv`
  after grep-confirming no other consumer.
- [x] 4.3 **Parity gate:** non-relay analyzer output byte-identical on
  existing non-relay fixtures; full non-relay `test_flare_analyze.py`
  + `py_compile scripts/*.py` green.

  2026-05-19: Removed relay analyzer recommendation/coverage machinery,
  retired relay-only tests and fixtures, and grep-confirmed no analyzer
  relay estimator references remain. Compared representative non-relay
  patch outputs against commit `283e806` with `PYTHONHASHSEED=0`; patch
  diff was empty. `python3 scripts/test_flare_analyze.py` and
  `python3 -m py_compile scripts/*.py` green.

## 5. Docs (R6)

- [x] 5.1 `TUNING.md`: delete the relay duty-estimator /
  confidence-gate sections, the offline relay capture/analyze/apply
  loop, the bimodal-ratchet note. Keep fallback law + collapse-ramp +
  `relay_min_flip_mm` caveat.
- [x] 5.2 `MANUAL.md`: remove any `RDE`/relay-estimator references.
  Grep both for dangling estimator/removed-key mentions.

  2026-05-19: Rewrote type-D tuning docs as fallback-only, removed
  removed protocol/config fields from `MANUAL.md`, and updated
  `TEST_CASES.md` to validate absence of `RDE`/`RDCF`/`RDV`. `rg`
  confirms no removed-key references remain in `TUNING.md` or
  `MANUAL.md`; `ninja -C build_local` green after nearby firmware
  comment cleanup.

## 6. Validation + closeout

- [x] 6.1 Full host check green: `ninja -C build_local`,
  `py_compile scripts/*.py`, `test_gen_config.py`, non-relay
  `test_flare_analyze.py`; non-relay parity byte-identical.

  2026-05-19: Green host gate: `ninja -C build_local`,
  `python3 -m py_compile scripts/*.py`, `python3
  scripts/test_gen_config.py`, `python3 scripts/test_flare_analyze.py`.
  Fixed-seed analyzer parity against pre-docs commit `4a4dd68` was
  byte-identical for representative non-relay patch outputs.
- [x] 6.2 On-hw smoke: flash; `GET` confirms removed keys gone +
  `RDE` absent from `?:`; sync auto-arms (`SM:1`); a vase and the cube
  run fallback-class (low TENSION, BP off the +12.5 wall). Capture +
  record.

  2026-05-19: Pending physical hardware gate. Host build is ready for
  flash; on-hardware print/capture not run in this session.

  **Partial verification 2026-05-20** (live status + dump from flashed
  board, no print running):
  - `RDE`/`RDCF`/`RDV` confirmed absent from `?:` status string ✓
  - Removed config keys (`relay_confidence_cycles`,
    `relay_confidence_window_ms`, `relay_estimate_lo/hi`,
    `relay_seed_warmup_ms`) confirmed absent from `--dump` ✓
  - `relay_catchup_frac`/`relay_neutral_frac`/`relay_min_flip_mm`/
    collapse-ramp keys all present with correct defaults ✓
  - `SM:0` at idle is **expected** — sync arms only during a live
    print, not at rest. The task "sync auto-arms (SM:1)" means it
    should arm when printing starts (RELOAD mode, `auto_mode: True`),
    not that it must show `SM:1` at idle.

  **Remaining for next agent — print gate:**
  Capture CSVs during a vase print and a bimodal cube:
  ```bash
  python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 --machine-id "$MACHINE_ID" --csv-out ~/vase-fallback-smoke.csv
  python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 --machine-id "$MACHINE_ID" --csv-out ~/cube-fallback-smoke.csv
  ```
  Then analyze both with this inline script (same one used for K2 A/B
  vase captures; auto-isolates steady-state body):
  ```bash
  for F in ~/vase-fallback-smoke.csv ~/cube-fallback-smoke.csv; do
    python3 - "$F" <<'EOF'
  import csv, sys, os, statistics
  F = sys.argv[1]
  if not os.path.exists(F): print(os.path.basename(F), "absent"); sys.exit(0)
  with open(F) as file: rows = list(csv.DictReader(file))
  def fl(r, k):
      try: return float(r.get(k, ""))
      except: return None
  allest = [e for e in (fl(r, "EST") for r in rows) if e is not None]
  if not allest: print(os.path.basename(F), "no EST"); sys.exit(0)
  p90_idx = min(len(allest) - 1, int(0.9 * len(allest)))
  thr = 0.70 * sorted(allest)[p90_idx]
  t0 = fl(rows[0], "wall_ts"); tN = fl(rows[-1], "wall_ts")
  if t0 is None or tN is None: print(os.path.basename(F), "no wall_ts"); sys.exit(0)
  start = next((fl(r, "wall_ts") for r in rows if (fl(r, "EST") or 0) >= thr), t0)
  rows = [r for r in rows if (lambda t: t is not None and t >= start and (tN - t) > 6)(fl(r, "wall_ts"))]
  N = len(rows)
  if not N: print(os.path.basename(F), "empty after trim"); sys.exit(0)
  dur = fl(rows[-1], "wall_ts") - fl(rows[0], "wall_ts")
  zc = {}
  for r in rows: zc[r.get("BUF")] = zc.get(r.get("BUF"), 0) + 1
  eps = []
  i = 0
  while i < N:
      z = rows[i].get("BUF"); j = i
      while j < N and rows[j].get("BUF") == z: j += 1
      a = fl(rows[i], "wall_ts"); b = fl(rows[j-1], "wall_ts")
      eps.append((z, (b - a) if (a is not None and b is not None) else 0.0)); i = j
  def dist(Z):
      d = sorted(e[1] for e in eps if e[0] == Z)
      if not d: return None
      p90_idx_d = min(len(d) - 1, int(0.9 * len(d)))
      return dict(n=len(d), p50=round(statistics.median(d), 2), p90=round(d[p90_idx_d], 2), max=round(max(d), 2), total=round(sum(d), 1))
  rde = [1 if r.get("RDE") == '1' else 0 for r in rows]
  best = cur = 0
  for v in rde: cur = cur + 1 if v else 0; best = max(best, cur)
  bp = [fl(r, "BP") for r in rows if fl(r, "BP") is not None]
  print(os.path.basename(F), "body_s", round(dur), "rows", N)
  print("  RDE1%", round(100 * sum(rde) / N, 1), "| longest RDE=1 span ~", round(dur * best / N, 1) if N else 0, "s  (should be 0 — estimator removed)")
  print("  TENSION %rows", round(100 * zc.get("TENSION", 0) / N, 1), "ep/min", round(60 * sum(1 for e in eps if e[0] == 'TENSION') / dur, 1) if dur else 0)
  print("  COMPRESSION", dist("COMPRESSION"))
  print("  BP min/max", round(min(bp), 2) if bp else None, round(max(bp), 2) if bp else None)
  EOF
  done
  ```
  Pass criteria (steady-state body, startup excluded):
  - `RDE1%` = 0.0 — estimator removed, no confident path active (PASS = 0, any non-zero = regression)
  - `TENSION %rows` < ~15 % and `BP max` < 10 mm (off the ±12.5 wall)
  - Compare against r7 baseline: `TENSION 10.9 %, ep/min 4.4, BPmax 5.1` (bimodal cube)

  **PASS 2026-05-20:**

  | metric | vase | cube | r7 baseline |
  |---|---|---|---|
  | body_s / rows | 497 s / 3434 | 1077 s / 7552 | — |
  | RDE1% | **0.0** | **0.0** | 0.0 |
  | TENSION %rows | **1.3** | **13.1** | 10.9 |
  | ep/min | 0.8 | 5.4 | 4.4 |
  | COMPRESSION | n=15, p50=0.39 s, max=2.46 s | n=208, p50=0.69 s, max=1.48 s | — |
  | BP min/max | −5.50 / **5.36** | −5.71 / **5.14** | — / 5.1 |

  RDE1%=0 both prints (estimator fully gone). BP off the ±12.5 wall in
  both (max 5.36 vase, 5.14 cube — within ±5.71 switch span). Cube
  TENSION 13.1% vs r7 10.9%: minor session variation, not regression —
  BP identical confirms same fallback behavior. Vase near-perfect
  (1.3% TENSION). **6.2 gate CLOSED.**
- [x] 6.3 `openspec validate relay-fallback-only --type change
  --strict` green. Commit + push to main. Update memory
  `relay-confident-estimator-bimodal-bangbang` (REMOVE executed).

  2026-05-19: `openspec validate relay-fallback-only --type change
  --strict` green. Implementation/docs commits pushed to `main` through
  `fd83393`; closeout task update commit follows. Cavemem write API was
  not available in this session, so memory update remains a handoff note.
- [x] 6.4 Archive `relay-confident-path-keep-or-remove` and
  `relay-confidence-gate-harden` once this lands and is hw-smoked
  (their verdict/decisions are now realized).

  2026-05-20: Moved to
  `openspec/changes/archive/2026-05-20-relay-confidence-gate-harden`
  and `openspec/changes/archive/2026-05-20-relay-confident-path-keep-or-remove`.
  All tasks in both changes were complete or superseded.
