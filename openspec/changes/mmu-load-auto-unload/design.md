# Design: MMU_LOAD Auto-Unload & Lane Switch

## Implementation Details

### File: `klipper/mmu.py`

#### Modify `cmd_MMU_LOAD`
In `cmd_MMU_LOAD(self, gcmd)`:

```python
        other_gate = 1 - gate
        if other_gate < len(self.gate_status):
            if self.gate_status[other_gate] == 2:
                gcmd.respond_info(f"FLARE: Gate {other_gate} is currently loaded. Performing auto-unload and switching to lane {lane} (Gate {gate})")
                self.gcode.run_script_from_command(f"_FLARE_CHANGE_LANE LANE={lane}")
                return
```

### File: `scripts/webui/app.js`

#### Add global `lastTelemetryData` cache
```javascript
let lastTelemetryData = null;
```

#### Update `updateUIState`
Inside `updateUIState(data)`:
```javascript
    lastTelemetryData = data;
```

#### Add `loadActiveLane` helper
```javascript
function loadActiveLane() {
    if (lastTelemetryData) {
        const lane = lastTelemetryData.active_lane;
        const otherLane = lane === 1 ? 2 : (lane === 2 ? 1 : 0);
        if (otherLane > 0) {
            const otherLoaded = gateStatusForLane(otherLane, lastTelemetryData) === 2;
            if (otherLoaded) {
                if (!confirm("Make sure you unloaded the current lane from the toolhead. Proceed?")) {
                    return;
                }
            }
        }
    }
    sendCustomCommand('FL:');
}
```

#### Add `unloadActiveLane` helper
```javascript
function unloadActiveLane() {
    if (lastTelemetryData) {
        const gateStatus = gateStatusForLane(activeLane, lastTelemetryData);
        if (gateStatus === 2) {
            if (!confirm("Make sure you unloaded the current lane from the toolhead before unloading. Proceed?")) {
                return;
            }
        }
    }
    sendCustomCommand('UL:');
}
```

#### Modify `ejectActiveLane` helper
```javascript
function ejectActiveLane() {
    if (lastTelemetryData) {
        const gateStatus = gateStatusForLane(activeLane, lastTelemetryData);
        if (gateStatus === 2) {
            if (!confirm("Make sure you unloaded the current lane from the toolhead before ejecting. Proceed?")) {
                return;
            }
        }
    }
    if (activeLane === 1 || activeLane === 2) {
        sendCustomCommand('UM:' + activeLane);
    } else {
        sendCustomCommand('UM:');
    }
}
```

### File: `scripts/webui/index.html`
Change click handler for the load and unload buttons:
```html
<button class="btn btn-secondary" id="btn-unload" onclick="unloadActiveLane()">Unload</button>
<button class="btn btn-primary" id="btn-load" onclick="loadActiveLane()">Load</button>
```

### Risk / Invariants
- `_FLARE_CHANGE_LANE` runs synchronously, blocking further commands until done.
- Avoid duplicate loading or double-load situations since `_FLARE_CHANGE_LANE` unloads first, then loads.
- Ensure python syntax is 100% correct.
