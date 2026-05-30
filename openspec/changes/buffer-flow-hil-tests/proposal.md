## Why

The buffer control flows — sync, idle stabilize, buffer-lock, flow-load, and
unload — are firmware state machines (`sync.c`, `motion.c`, `toolchange.c`) with
**zero automated test coverage**. They are validated only by the manual,
operator-facing `TEST_CASES.md` checklist and on-rig observation. The recent
type-P tension-safety work (D18 auto-sync gating, D19/D20 buffer-lock, D21
reload contact, D22 unload over-tension, D23 idle stabilize) shipped with the
logic rig-blocked and no repeatable regression check.

There is no host build for the firmware (it is a Pico cross-build) and no
injection seam in the protocol, so a deterministic host harness is not available
without firmware changes. What *is* available: the firmware emits every flow
transition as an `EV:` event, and `flare_daemon` already owns the serial port,
parses those events, and re-publishes them over HTTP (`/telemetry` SSE,
`/status`) plus accepts commands (`POST /cmd`).

## What Changes

- Add an **operator-assisted hardware-in-the-loop (HIL) test suite** for all
  five buffer flows, driven through the running daemon's telemetry — no second
  serial reader, no port conflict.
- Add a reusable HIL harness (`flare_hil_harness.py`): daemon-backed command
  send, SSE event capture on a background thread, and `wait/refute/expect`
  event assertions plus operator prompts.
- Add a runner (`flare_hil.py`) with a registry of flow cases: `sync`, `stab`,
  `buflock`, `load`, `unload`, selectable by `--flow` and sensor `--type`.
- Unit-test the harness's pure parsing/matching logic (`event_from_sse`,
  `parse_event`, `wait/refute`) with no hardware so it runs in CI.
- Scope clarifications baked in: **load = `FL` flow-load only** (preload/autoload
  do not use the buffer); the **auto-sync toggle (D18)** lives under the sync
  flow; type-P is the focus, with type-D parity cases gated to `--type d`.

The HIL harness drives the board type-agnostically (`SET:BUF_SENSOR` flips
D/P), so it **covers both sensor types** and is therefore a **separate change**
from the type-P-specific `psf-analog-rig`.

## Capabilities

### New Capabilities
- `buffer-flow-hil-tests`: operator-assisted HIL test infrastructure for the
  buffer flows — daemon-telemetry event capture, wait/refute assertions,
  per-flow case registry (sync/stab/buflock/load/unload), and CI-runnable unit
  tests for the harness's pure logic.

## Impact

- `scripts/flare_hil_harness.py` (new): daemon HTTP transport + event capture +
  assertions.
- `scripts/flare_hil.py` (new): flow-case registry + runner CLI.
- `scripts/test_flare_hil_harness.py` (new): unit tests for the pure logic.
- No firmware change (operator moves the buffer; events asserted via daemon).
- No change to `flare_daemon` (reuses existing `/cmd`, `/telemetry`, `/status`).
