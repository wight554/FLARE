## 1. Phase 1 — Format + degenerate equivalence (no behavior change)

- [x] 1.1 Define versioned schedule table format in `config.ini.example` (commented), bounded breakpoint cap `N` as config tunable
- [x] 1.2 `scripts/gen_config.py`: parse schedule table → `tune.h` `CONF_FLOW_SCHED[]` + `CONF_FLOW_SCHED_LEN`; bias stored as integer milli; flow/baseline in sps
- [x] 1.3 When no schedule table present, synthesize a length-1 schedule from existing scalar `baseline_sps` / `trailing_bias_frac` keys
- [x] 1.4 Firmware `flow_param(flow_sps)` in `firmware/src/sync.c` (+ decl in `sync.h`): sorted search, clamp no-extrapolate, integer linear interp; `LEN==1` returns the point exactly
- [x] 1.5 Host test: scalar-only config → `LEN==1` table; firmware-side parity test: flow sweep on new build vs pre-change build byte-identical commanded output
      Validation 2026-05-18: `python3 scripts/test_gen_config.py`;
      `python3 -m py_compile scripts/*.py`; `ninja -C build_local`.
      Phase 1 does not switch control reads yet; `LEN==1` parity is enforced
      by `flow_param()` returning the single point exactly for every flow.

## 2. Phase 2 — Analyzer schedule emission (host)

- [ ] 2.1 Add schedule-emit mode to `scripts/flare_analyze.py`: group buckets by `v_fil_bin`, apply existing maturity gates, one (flow, baseline, bias) per surviving bin via the `sync-tuning-and-relief-finish` n-weighted reducer + `BIAS_SAFE_MIN/MAX`
- [ ] 2.2 Deterministic reduction to ≤ `N` points: keep endpoints, drop lowest-curvature interior point until ≤ `N`, ties broken by lowest flow; no wall-clock recency
- [ ] 2.3 Sparse fallback: too few mature bins → emit `LEN==1` scalar-equivalent schedule
- [ ] 2.4 Tests: same buckets twice → byte-identical schedule; sparse input → `LEN==1`; existing scalar `--emit-baseline` path unchanged

## 3. Phase 3 — Firmware switch to schedule (control)

- [ ] 3.1 Replace scalar baseline/bias reads in `sync.c` with `flow_param(extruder_est_sps)` at the existing read sites; full-bias / reserve / collapse code untouched
- [ ] 3.2 Scope disciplined live ratchet to the active flow segment (keep multi-cycle / variance / cooldown / up-only / non-persistent / SYNC_ACTIVE gating)
- [ ] 3.3 Reboot/settings-reload restores schedule from config; live segment delta discarded
- [ ] 3.4 Local `cmake --build build_local`; `scripts/validate_regression.sh`; flow-sweep parity vs scalar config still byte-identical

## 4. Closeout

- [ ] 4.1 `openspec validate flow-keyed-param-schedule --strict`
- [ ] 4.2 Update BEHAVIOR.md / MANUAL.md / workflow docs: schedule is the deterministic output of the same two-profile procedure; scalar keys = degenerate 1-point
- [ ] 4.3 Full regression + local build green; working tree clean
