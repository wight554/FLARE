## Context

Tuning today is documented only inside `MANUAL.md` (Calibration Workflow +
a stale Phase-2.7 quickstart) and assumes firmware fluency. The scripts it
drives are stable and their flags are known/verified:
`flare_analyze.py` exposes `--profile-fast/--profile-slow
/--emit-flow-schedule/--flow-schedule-cap/--acceptance-gate
/--commit-watermark`; `flare_baseline_recommender.py` is minimal
(`--port/--baud/--file` only — no machine-id, reads until EOF/interrupt);
`gen_config.py` takes no args; `gcode_marker.py --emit sidecar|mark`;
`flare_live_tuner.py --observe-daemon` with `--klipper-uds/--klipper-mode
/--sidecar` or shell-marker fallback. Docs-only change; scripts and
firmware are frozen here.

## Goals / Non-Goals

**Goals:**
- One self-contained `TUNING.md` a no-firmware-context user can follow.
- Every command copy-paste accurate against the current scripts.
- Zero internal phase jargon on the user path.

**Non-Goals:**
- No script/firmware/config-value/behavior change.
- Not rewriting `MANUAL.md`'s internal calibration detail — link to it.
- Not duplicating `WORKFLOW.md` git policy.

## Decisions

### D1 — Standalone `TUNING.md`, not a MANUAL rewrite

A new top-level `TUNING.md` owns the operator narrative; `MANUAL.md` keeps
the reference detail and is linked. Rationale: the operator guide needs a
different voice (zero-context, linear, copy-paste) than the reference
manual; interleaving them produces the current unusable mix. The stale
"Trailing-Bias Tuning Quickstart (Phase 2.7)" heading is replaced with a
one-line redirect to `TUNING.md` (no content duplication, no dangling
phase label).

### D2 — Structure: TL;DR first, then linear path

`TUNING.md` order: (0) simplest-path TL;DR for "just want defaults";
(1) what tuning does, plain; (2) prerequisites with exact commands;
(3) two-profile bracket explained (same model, fastest- vs
slowest-cubic-flow slicer profile, why a bracket → one deterministic
result); (4) capture — Klipper sidecar **and** shell-marker fallback,
both full command lines + expected output; (5) analyze →
`--emit-flow-schedule`, reading the output, sparse→one-point;
(6) review/apply → which `config.ini` keys, `gen_config.py`, build,
flash, watermark; (7) `flare_baseline_recommender.py` observe-only usage;
(8) verify via `STATUS` incl. `SYNC_REFILL_MM/SYNC_RELIEVE_MM` + operator
meaning of the events; (9) troubleshooting incl. determinism answer.
Rationale: a user must reach a working default before optional depth.

### D3 — Accuracy gate, not prose review

Implementation MUST execute each documented command's `--help` (or a dry
run) and match flags exactly. If a script cannot produce the
documented-friendly behavior copy-paste (e.g. recommender has no
end-of-print flag, no machine-id), the guide describes the script
**as-is** and the limitation is recorded as an open question — code is not
changed to fit the doc.

### D4 — Operator-language event mapping

`FAULT_HOLD`/`FAULT_HOLD_RECOVERY`/`cannot_refill`/`cannot_relieve` and
`SYNC_REFILL_MM/SYNC_RELIEVE_MM` are described only in observable operator
terms ("sync paused itself and will retry; check for a jam"), no internal
state-machine detail — that lives in `BEHAVIOR.md`, cross-linked.

## Risks / Trade-offs

- [Doc drifts from scripts later] → Spec requires command accuracy; a
  task verifies every command against `--help` at write time; future flag
  changes are the changer's responsibility (noted in guide footer).
- [Two docs (TUNING vs MANUAL) diverge] → TUNING owns the path, MANUAL the
  reference; explicit cross-links, no duplicated commands beyond the
  canonical operator path.
- [Klipper-less users informed] → Sidecar is the supported/recommended path.
  Amendment 2026-05-18: Capture Path B (shell-marker) removed entirely;
  sidecar is the single supported/recommended path.

## Open Questions

- `flare_baseline_recommender.py` has no machine-id and no explicit
  end-of-print stop (reads until EOF/Ctrl-C). Guide will document it
  as-is; if an operator-friendly stop/summary is wanted, that is a
  separate scripts change, not this docs change.
- Whether the slicer "fastest/slowest cubic-flow profile" can be given as
  concrete numbers or only as relative guidance — default: relative +
  one worked example; revisit if a canonical profile pair is defined.

## Implementation Notes (2026-05-18)

### Script surface verified
- `scripts/flare_analyze.py --help`: supports `--in`, `--out`,
  `--mode {safe,aggressive}`, `--state`, `--machine-id`, `--config`,
  `--profile-fast`, `--profile-slow`, `--emit-baseline`,
  `--emit-flow-schedule`, `--flow-schedule-cap`, `--acceptance-gate`,
  `--commit-watermark`, `--keys`, `--include-stale`, and `--force`.
- `scripts/flare_live_tuner.py --help`: supports `--port`, `--baud`,
  `--state`, `--machine-id`, `--csv-out`, `--observe-daemon`,
  `--commit-on-idle`, `--commit-on-finish`, `--klipper-uds`,
  `--klipper-mode {auto,on,off}`, `--sidecar`, `--marker-file`,
  `--keep-marker-file`, debug progress flags, and explicit experimental write
  flags.
- `scripts/gcode_marker.py --help`: supports `input`, `--output`,
  `--sidecar`, `--dia`, `--no-layer-markers`, `--emit
  {m118,mark,file,both,sidecar}`, and `--shell-cmd`.
- `scripts/flare_baseline_recommender.py --help`: supports only `--port`,
  `--baud`, and `--file`.
- `scripts/gen_config.py` has no argparse help/options; operator guide should
  use the default no-argument command `python3 scripts/gen_config.py`. Source
  also accepts optional positional config/output paths for tests.
- `scripts/flash_flare.sh` supports default clang build/flash and optional
  `--gcc`; guide should use `bash scripts/flash_flare.sh`.
- `scripts/flare_cmd.py --help` cannot print without pyserial installed because
  it imports serial before argparse; guide must list `python3 -m pip install
  pyserial` before any `flare_cmd.py` command.

### Config keys verified
- Scalar fallback keys in `config.ini.example`: `baseline_rate` and
  `sync_trailing_bias_frac`.
- Flow schedule keys in `config.ini.example`: `flow_schedule_cap` and optional
  `[flow_schedule.v1]` rows `pointN: flow_sps, baseline_sps,
  trailing_bias_frac`.

### Operator-friendly gaps
- `flare_baseline_recommender.py` has no `--machine-id` and no explicit
  end-of-print/stop option; it reads a serial stream until Ctrl-C, or reads a
  replay file to EOF. This guide documents that behavior as-is.
