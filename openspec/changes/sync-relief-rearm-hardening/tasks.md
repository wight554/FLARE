## 1. RELIEF_PAUSE re-arm (high — item 1)

- [ ] 1.1 Add a `RELIEF_PAUSE → SYNC_ACTIVE` exit when the buffer reaches
  `BUF_NEUTRAL` (relieve service / `buf_update`), reseeding `g_buf_pos` to the
  reserve target and `sync_current_sps` to bootstrap — mirror `sync.c:1258-1262`.
- [ ] 1.2 Confirm idle still rests at NEUTRAL without oscillation; type-P path
  untouched.

## 2. Estimator overwrite (high — item 2)

- [ ] 2.1 At `sync.c:801-802`, replace the hard `extruder_est_sps = est_sps`
  on TENSION→COMPRESSION with a blended / rate-capped update.
- [ ] 2.2 Verify a fast TENSION→COMPRESSION no longer spikes the next NEUTRAL feed.

## 3. Medium items (decide firmware vs config)

- [ ] 3.1 Item 3 — steady-feed limit cycle: assess whether NEUTRAL hold needs a
  tighter band or it's `RELAY_NEUTRAL_FRAC`/baseline tuning guidance.
- [ ] 3.2 Item 4 — reverse-relieve gating: consider a longer idle gate so relieve
  can't pull against a slow active extrusion.
- [ ] 3.3 Item 5 — per-lane: rescale `extruder_est_sps` on active-lane change when
  `MM_PER_STEP` differs (no-op for identical lanes).
- [ ] 3.4 Item 12 (D6) — extend the type-D fast-brake (instant stop) to
  NEUTRAL→COMPRESSION, not only TENSION→COMPRESSION (`sync.c:1112`); or defer with
  rationale. Pairs with 2.x — the item-2 spike terminates in exactly this path.

## 4. Low items (notes; fix opportunistically)

- [ ] 4.1 Fix stale `buf_read_stable` flip-guard comment (item 6).
- [ ] 4.2 Optional div-by-zero guard on `MM_PER_STEP` (item 7); MV `g_buf_pos`
  re-anchor (item 8); est-update dwell-gate edge (item 9); `now_ms`/`g_now_ms`
  tidy (item 11). expf perf (item 10) — leave unless profiled.
- [ ] 4.3 Item 13 — `mmu_sps_dwell_sum` overflow on a multi-hour no-crossing
  NEUTRAL ride; reset-on-crossing makes it practically unreachable. Leave unless
  a real long-print log shows it, or add a cheap clamp/decay if cheap.

## 5. Validation

- [ ] 5.1 Build clean.
- [ ] 5.2 HW: relief → high-flow resume — no empty-wall/grind, sync re-arms from
  NEUTRAL.
- [ ] 5.3 HW: fast TENSION→COMPRESSION disturbance — no NEUTRAL over-feed spike.
- [ ] 5.4 HW: long steady print — no periodic reverse-relieve limit cycle (or
  acceptable); `flare_purge_check.py --mode regression` clean.
- [ ] 5.5 Confirm purge + constant feed + stop still good (no regression of
  `compression-overfeed-stop`).
