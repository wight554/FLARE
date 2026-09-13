---
schema_version: 1
open_count: 2
waived_count: 0
fixed_count: 0
total_count: 2
last_updated: 2026-09-13T07:24:42.793Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 13 | deviation | scripts/test_sync_sim.py |  | 13-01 Task 1 acceptance grep for a removed 'g_sync_current_sps = target_sps' literal also matches two unrelated pre-existing type-D ramp-clamp lines in sync.c; verified via RED/GREEN sim evidence instead | open |  | 2026-09-13T07:24:42.673Z |  |
| 2 | 13 | deviation | firmware/src/sync.c |  | 13-01 Task 2 ramp-cap for type-P tension-dwell escalation could not be empirically differentiated from apply-side-cap-alone via sim scenario within reasonable effort; retained on structural/code-review grounds | open |  | 2026-09-13T07:24:42.793Z |  |

````json
[
  {
    "id": 1,
    "kind": "deviation",
    "phase": "13",
    "file": "scripts/test_sync_sim.py",
    "line": null,
    "description": "13-01 Task 1 acceptance grep for a removed 'g_sync_current_sps = target_sps' literal also matches two unrelated pre-existing type-D ramp-clamp lines in sync.c; verified via RED/GREEN sim evidence instead",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-13T07:24:42.673Z",
    "resolved_at": null
  },
  {
    "id": 2,
    "kind": "deviation",
    "phase": "13",
    "file": "firmware/src/sync.c",
    "line": null,
    "description": "13-01 Task 2 ramp-cap for type-P tension-dwell escalation could not be empirically differentiated from apply-side-cap-alone via sim scenario within reasonable effort; retained on structural/code-review grounds",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-13T07:24:42.793Z",
    "resolved_at": null
  }
]
````
