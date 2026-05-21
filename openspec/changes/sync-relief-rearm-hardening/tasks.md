## 1. RELIEF_PAUSE re-arm (high — item 1)

- [x] 1.1 Add a `RELIEF_PAUSE → SYNC_ACTIVE` exit when the buffer reaches
  `BUF_NEUTRAL` (relieve service / `buf_update`), reseeding `g_buf_pos` to the
  reserve target and `sync_current_sps` to bootstrap — mirror `sync.c:1258-1262`.
- [ ] 1.2 Confirm idle still rests at NEUTRAL without oscillation; type-P path
  untouched.

## 2. Estimator overwrite (high — item 2)

- [x] 2.1 At `sync.c:801-802`, replace the hard `extruder_est_sps = est_sps`
  on TENSION→COMPRESSION with a blended / rate-capped update.
- [ ] 2.2 Verify a fast TENSION→COMPRESSION no longer spikes the next NEUTRAL feed.

## 3. Medium items (decide firmware vs config)

- [x] 3.1 Item 3 — steady-feed limit cycle: assess whether NEUTRAL hold needs a
  tighter band or it's `RELAY_NEUTRAL_FRAC`/baseline tuning guidance. (Evaluated: resolved via RELAY_NEUTRAL_FRAC baseline tuning.)
- [x] 3.2 Item 4 — reverse-relieve gating: consider a longer idle gate so relieve
  can't pull against a slow active extrusion. (Evaluated: handled via POST_PRINT_STAB_MS configuration.)
- [x] 3.3 Item 5 — per-lane: rescale `extruder_est_sps` on active-lane change when
  `MM_PER_STEP` differs (no-op for identical lanes).
- [x] 3.4 Item 12 (D6) — extend the type-D fast-brake (instant stop) to
  NEUTRAL→COMPRESSION, not only TENSION→COMPRESSION (`sync.c:1112`); or defer with
  rationale. Pairs with 2.x — the item-2 spike terminates in exactly this path.

## 4. Low items (notes; fix opportunistically)

- [x] 4.1 Fix stale `buf_read_stable` flip-guard comment (item 6).
- [x] 4.2 Optional div-by-zero guard on `MM_PER_STEP` (item 7); MV `g_buf_pos`
  re-anchor (item 8); est-update dwell-gate edge (item 9); `now_ms`/`g_now_ms`
  tidy (item 11). expf perf (item 10) — leave unless profiled.
- [x] 4.3 Item 13 — `mmu_sps_dwell_sum` overflow on a multi-hour no-crossing
  NEUTRAL ride; reset-on-crossing makes it practically unreachable. Leave unless
  a real long-print log shows it, or add a cheap clamp/decay if cheap.

## 5. Validation

- [x] 5.1 Build clean (`ninja -C build_local`).

### Testing tips (agent: read before HW runs)

All five fixes ship in ONE build, compile-time gated `BUF_SENSOR_TYPE == 0`
(type-P unchanged). There is no runtime toggle — to A/B a single fix you must
reflash without its commit (e.g. `git revert ecd3f5d` for D6). Testing all five
together in one session is fine: each maneuver below has a distinct event/maneuver
signature, so the `EV:` log attributes which fix acted. **Keep the full event log —
it is the attribution tool.** The only coupled pair is D2↔D6 (both on the
COMPRESSION-entry path); maneuvers M2 and M4 separate them.

Commands (run from repo root):
- Pre-flight settings: `python3 scripts/flare_cmd.py GET:BUF_SENSOR_TYPE GET:RELAY_MIN_FLIP_MM GET:SYNC_STATE`
- Live status + event capture: `python3 scripts/flare_cmd.py --poll 100 | tee hw_run.log`
- One-off cmd / GET / SET: `python3 scripts/flare_cmd.py GET:<PARAM>` · `SET:<PARAM>:<VAL>` · `?`
- Config dump: `python3 scripts/flare_cmd.py --dump` (`--raw` for terse)
- Telemetry + regression analyze: `python3 scripts/flare_purge_check.py --live --poll 100 --csv hw_run.csv --mode both`
  (offline: `python3 scripts/flare_purge_check.py --log hw_run.log --mode regression`)
- Build/regression suite (no HW): `bash scripts/validate_regression.sh`

Watch (from `?` status): `g_buf_pos`, `SYNC_STATE`, est (mm/min), reserve_error.
Correlate state transitions via `EV:SYNC,*` rather than the raw enum int.
Pre-flight assert: `BUF_SENSOR_TYPE`=0, `RELAY_MIN_FLIP_MM`=0.
**Global ABORT events** (stop + note which maneuver): `BS,FAULT,0`,
`SYNC,cannot_refill`, `SYNC,cannot_relieve`, `SYNC,TENSION_DWELL_WARN`,
`SYNC,FAULT_HOLD`.

### HW maneuvers (ordered safe→risky; fill result + log excerpt under each)

- [ ] 5.2 M1 — boot + idle (D1 boot guard + no spurious re-arm; covers 1.2).
  Power on with buffer at COMPRESSION; load; sit idle (no extrusion).
  PASS: boot → NEUTRAL with no early ACTIVE; idle shows `SYNC,RELIEF_PAUSE` then
  stays paused, motor quiet, **no** `SYNC,AUTO_START`. Result: __
- [ ] 5.3 M2 — steady print 10–15 min (D6 × item-3 + D2).
  PASS: NEUTRAL↔COMPRESSION crossing period/depth no worse than pre-fix; no
  periodic RELIEF_PAUSE limit cycle; est stable; no `NEUTRAL_CREEP_CAP` spam.
  FAIL (shrinking period / deeper lean) → revert `ecd3f5d`. Result: __
- [ ] 5.4 M3 — pause → high-flow resume (D1 positive).
  Pause extruder ~10s (buffer → COMPRESSION → RELIEF_PAUSE), then resume high flow.
  PASS: `SYNC,RELIEF_PAUSE` → `SYNC,AUTO_START` on the drain to NEUTRAL (no full
  TENSION drain), no grind / `cannot_refill`. Result: __
- [ ] 5.5 M4 — fast/partial TENSION→COMPRESSION disturbance (D2; covers 2.2).
  PASS: est (dump) does not jump to the rail; next NEUTRAL feed no overshoot
  straight back to COMPRESSION. Result: __
- [ ] 5.6 M5 — toolchange L1→L2 (D5) — ONLY if lanes differ in `rotation_distance`.
  PASS: `EV:ACTIVE,n` + est rescales; first post-swap crossings no over/under-feed.
  Skip (no-op) if identical `MM_PER_STEP`. Result: __
- [ ] 5.7 M6 — regression guard. End-of-feed true-stop (no -11 slam) +
  `flare_purge_check.py … --mode both` clean + purge + constant feed good
  (no `compression-overfeed-stop` regression). Result: __

## 6. Docs (rule 6)

- [ ] 6.1 Update `BEHAVIOR.md` sync state-machine: add the RELIEF_PAUSE → NEUTRAL
  re-arm exit (D1) and the type-D fast-brake on NEUTRAL→COMPRESSION (D6).
