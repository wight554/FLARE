// FLARE WebUI Application Script

// State cache
let boardOnline = false;
let activeLane = 0;
let bufferHistory = []; // circular buffer of {time, pos}
const MAX_HISTORY = 300;

// Canvas setup
const canvas = document.getElementById('buffer-chart');
const ctx = canvas.getContext('2d');

// Fit canvas to its container
function resizeCanvas() {
    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width = rect.width * window.devicePixelRatio;
    canvas.height = rect.height * window.devicePixelRatio;
    ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

// Connection setup
let eventSource = null;

function connectSSE() {
    const statusDot = document.getElementById('connection-dot');
    const statusLabel = document.getElementById('connection-status');
    
    addLogEntry('System', 'Connecting to FLARE telemetry stream...', 'system');
    
    // Create EventSource to relative stream endpoint
    eventSource = new EventSource('/telemetry');
    
    eventSource.onopen = () => {
        boardOnline = true;
        statusDot.className = 'status-dot online';
        statusLabel.textContent = 'CONNECTED';
        addLogEntry('System', 'Connected successfully to daemon proxy', 'info');
    };
    
    eventSource.onerror = (err) => {
        boardOnline = false;
        statusDot.className = 'status-dot offline';
        statusLabel.textContent = 'DISCONNECTED';
        addLogEntry('System', 'Telemetry stream disconnected. Reconnecting...', 'system');
        
        // SSE automatically reconnects, but let's clear the online indicator
        updateUIOffline();
    };
    
    eventSource.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            
            // Handle regular status update frame
            if (data.timestamp) {
                updateUIState(data);
                
                // Add position to history buffer
                const pos = data.g_buf_pos !== undefined ? data.g_buf_pos : 0.0;
                bufferHistory.push(pos);
                if (bufferHistory.length > MAX_HISTORY) {
                    bufferHistory.shift();
                }
            }
            
            // Handle async events
            if (data.event_type) {
                const details = data.event_data ? `: ${data.event_data}` : '';
                addLogEntry('Event', `${data.event_type}${details}`, 'event');
            }
            
        } catch (e) {
            console.error('Error parsing SSE payload:', e);
        }
    };
}

function updateUIOffline() {
    document.getElementById('connection-dot').className = 'status-dot offline';
    document.getElementById('connection-status').textContent = 'DISCONNECTED';
    document.getElementById('badge-sync-enabled').className = 'badge';
    document.getElementById('badge-sync-enabled').textContent = 'SYNC DISABLED';
    document.getElementById('badge-reload-mode').className = 'badge';
    document.getElementById('badge-reload-mode').textContent = 'MMU MODE';
    document.getElementById('val-tc-state').textContent = 'UNKNOWN';

    // Disable every control while disconnected
    ['btn-lane-1', 'btn-lane-2', 'btn-preload', 'btn-eject',
     'btn-checkgate', 'btn-unload', 'btn-load'].forEach((id) => setBtnEnabled(id, false));
}

function updateUIState(data) {
    // 1. Board status & headers
    const statusDot = document.getElementById('connection-dot');
    const statusLabel = document.getElementById('connection-status');
    if (data.board_online) {
        statusDot.className = 'status-dot online';
        statusLabel.textContent = 'CONNECTED';
    } else {
        statusDot.className = 'status-dot offline';
        statusLabel.textContent = 'BOARD OFFLINE';
    }
    
    // 2. Badges
    const syncBadge = document.getElementById('badge-sync-enabled');
    if (data.sync_enabled === 1 || data.sync_drive) {
        syncBadge.className = 'badge active';
        syncBadge.textContent = 'SYNC ACTIVE';
    } else {
        syncBadge.className = 'badge';
        syncBadge.textContent = 'SYNC DISABLED';
    }
    
    const reloadBadge = document.getElementById('badge-reload-mode');
    if (data.reload_mode === 1) {
        reloadBadge.className = 'badge active';
        reloadBadge.textContent = 'AUTO-RELOAD';
    } else {
        reloadBadge.className = 'badge';
        reloadBadge.textContent = 'MMU MODE';
    }
    
    // 3. Stats Numbers
    document.getElementById('val-buf-pos').innerHTML = `${data.g_buf_pos.toFixed(2)} <span class="unit">mm</span>`;
    document.getElementById('val-sps').innerHTML = `${Math.round(data.sps)} <span class="unit">sps</span>`;
    document.getElementById('val-est-sps').innerHTML = `${Math.round(data.extruder_est_sps)} <span class="unit">sps</span>`;
    document.getElementById('val-reserve-error').innerHTML = `${data.reserve_error_mm.toFixed(1)} <span class="unit">mm</span>`;
    
    document.getElementById('val-tc-state').textContent = data.tc_state;
    
    // 4. Lane Selectors
    activeLane = data.active_lane;
    const btnL1 = document.getElementById('btn-lane-1');
    const btnL2 = document.getElementById('btn-lane-2');
    if (activeLane === 1) {
        btnL1.className = 'btn-lane active';
        btnL2.className = 'btn-lane';
    } else if (activeLane === 2) {
        btnL1.className = 'btn-lane';
        btnL2.className = 'btn-lane active';
    } else {
        btnL1.className = 'btn-lane';
        btnL2.className = 'btn-lane';
    }
    
    // 5. Sensors
    updateSensor('sensor-in1', data.in1);
    updateSensor('sensor-out1', data.out1);
    updateSensor('sensor-in2', data.in2);
    updateSensor('sensor-out2', data.out2);
    updateSensor('sensor-y-split', data.y_split);
    updateSensor('sensor-toolhead', data.toolhead);

    // 6. Action button states (mirror Fluidd MMU widget gating)
    updateButtonStates(data);

    // 7. Usage statistics
    if (data.mmu_stats) updateStats(data.mmu_stats);
}

function updateStats(stats) {
    const total = stats.swaps_total || 0;
    const rate = total ? (100 * (stats.swaps_success || 0) / total) : 100;
    document.getElementById('stat-swaps-total').textContent = total;
    document.getElementById('stat-success-rate').innerHTML = `${rate.toFixed(0)} <span class="unit">%</span>`;
    document.getElementById('stat-loads').textContent = stats.loads_success || 0;
    document.getElementById('stat-unloads').textContent = stats.unloads_success || 0;
    document.getElementById('stat-last-error').textContent = stats.last_error || 'None';
}

// Happy Hare gate_status for a lane: 0 = empty, 1 = available (preloaded),
// 2 = loaded/buffer (fully loaded to toolhead). Matches the daemon's
// klipper-syncer derivation; out-switch gates the shared y_split/toolhead so a
// non-active lane never reads loaded.
function gateStatusForLane(lane, data) {
    const inSw = lane === 1 ? data.in1 : (lane === 2 ? data.in2 : 0);
    const outSw = lane === 1 ? data.out1 : (lane === 2 ? data.out2 : 0);
    if (inSw && outSw && data.y_split && data.toolhead) return 2;
    if (inSw) return 1;
    return 0;
}

function setBtnEnabled(id, enabled) {
    const el = document.getElementById(id);
    if (el) el.disabled = !enabled;
}

function updateButtonStates(data) {
    const online = !!data.board_online;
    const lane = data.active_lane;
    const hasLane = lane === 1 || lane === 2;
    const gateStatus = hasLane ? gateStatusForLane(lane, data) : -1;
    const loaded = gateStatus === 2;

    // Lane selection always available while online
    setBtnEnabled('btn-lane-1', online);
    setBtnEnabled('btn-lane-2', online);

    // Preload (LO:): only when the active gate is empty
    setBtnEnabled('btn-preload', online && hasLane && gateStatus === 0);
    // Eject (UM:): only when the active gate holds filament
    setBtnEnabled('btn-eject', online && hasLane && gateStatus !== 0);
    // Check Gate (?:): always available while online
    setBtnEnabled('btn-checkgate', online);
    // Unload (UL:): only when filament is loaded to the toolhead
    setBtnEnabled('btn-unload', online && hasLane && loaded);
    // Load (FL:): only when not already loaded
    setBtnEnabled('btn-load', online && hasLane && !loaded);
}

function updateSensor(id, state) {
    const el = document.getElementById(id);
    if (state === 1) {
        el.className = 'sensor-indicator active';
    } else {
        el.className = 'sensor-indicator';
    }
}

// REST Command execution helper
function sendCustomCommand(cmdString) {
    const feedback = document.getElementById('cmd-feedback');
    feedback.className = 'cmd-feedback';
    feedback.textContent = `Executing: ${cmdString}...`;
    
    fetch('/cmd', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ cmd: cmdString }),
    })
    .then(response => {
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return response.json();
    })
    .then(data => {
        if (data.response) {
            const isError = data.response.startsWith('ER:');
            feedback.className = `cmd-feedback ${isError ? 'error' : 'success'}`;
            feedback.textContent = data.response;
            addLogEntry('Command', `${cmdString} → ${data.response}`, isError ? 'event' : 'info');
        } else if (data.error) {
            feedback.className = 'cmd-feedback error';
            feedback.textContent = `Error: ${data.error}`;
        }
    })
    .catch(error => {
        feedback.className = 'cmd-feedback error';
        feedback.textContent = `Failed: ${error.message}`;
        addLogEntry('Error', `Cmd connection failed: ${error.message}`, 'event');
    });
}

function sendCustomCommandFromInput() {
    const input = document.getElementById('input-command');
    const cmd = input.value.trim();
    if (cmd) {
        sendCustomCommand(cmd);
        input.value = '';
    }
}

// Log view management
function addLogEntry(source, text, className) {
    const logsContainer = document.getElementById('logs-container');
    const entry = document.createElement('div');
    const timeStr = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    
    entry.className = `log-entry ${className || ''}`;
    entry.innerHTML = `<span class="log-time" style="color: var(--text-muted); margin-right: 8px;">[${timeStr}]</span><strong>${source}:</strong> ${text}`;
    
    logsContainer.appendChild(entry);
    
    // Auto-scroll to bottom
    logsContainer.scrollTop = logsContainer.scrollHeight;
    
    // Limit log count
    while (logsContainer.children.length > 80) {
        logsContainer.removeChild(logsContainer.firstChild);
    }
}

// 60FPS Oscilloscope Chart Rendering
function drawChart() {
    requestAnimationFrame(drawChart);
    
    const w = canvas.width / window.devicePixelRatio;
    const h = canvas.height / window.devicePixelRatio;
    
    // Clear and draw grid background
    ctx.fillStyle = '#0a0d16';
    ctx.fillRect(0, 0, w, h);
    
    // Grid Lines
    ctx.lineWidth = 1;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.025)';
    
    // Vertical grid lines
    const gridSpacing = 40;
    for (let x = 0; x < w; x += gridSpacing) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, h);
        ctx.stroke();
    }
    
    // g_buf_pos is signed: 0 = neutral, + = tension (up), - = compression (down).
    // Clamps to +/- BUF_MAX_TRAVEL_MM/2 (default 12.5mm); state thresholds at
    // +/- BUF_SWITCH_SPAN_MM/2 (default 5.0mm). Map 0 to vertical center.
    const HALF_RANGE_MM = 12.5;
    const THRESHOLD_MM = 5.0;
    const mmToY = (mm) => {
        const padding = 30;
        const usable = (h - padding * 2) / 2;
        const clamped = Math.max(-HALF_RANGE_MM, Math.min(HALF_RANGE_MM, mm));
        return h / 2 - (clamped / HALF_RANGE_MM) * usable;
    };

    // Draw boundary markers
    ctx.lineWidth = 1;

    // Neutral center (0mm)
    ctx.strokeStyle = 'rgba(0, 242, 195, 0.08)';
    ctx.beginPath();
    ctx.moveTo(0, mmToY(0));
    ctx.lineTo(w, mmToY(0));
    ctx.stroke();

    // Tension boundary (top, +threshold)
    ctx.strokeStyle = 'rgba(255, 74, 96, 0.08)';
    ctx.beginPath();
    ctx.moveTo(0, mmToY(THRESHOLD_MM));
    ctx.lineTo(w, mmToY(THRESHOLD_MM));
    ctx.stroke();

    // Compression boundary (bottom, -threshold)
    ctx.strokeStyle = 'rgba(255, 74, 96, 0.08)';
    ctx.beginPath();
    ctx.moveTo(0, mmToY(-THRESHOLD_MM));
    ctx.lineTo(w, mmToY(-THRESHOLD_MM));
    ctx.stroke();
    
    // If no history, don't draw line
    if (bufferHistory.length < 2) return;
    
    // Draw dynamic glow trace
    ctx.lineWidth = 2.5;
    ctx.strokeStyle = 'hsl(174, 90%, 46%)';
    ctx.shadowBlur = 8;
    ctx.shadowColor = 'hsla(174, 90%, 46%, 0.4)';
    
    ctx.beginPath();
    
    const step = w / MAX_HISTORY;
    const startX = w - (bufferHistory.length * step);
    
    for (let i = 0; i < bufferHistory.length; i++) {
        const x = startX + i * step;
        const y = mmToY(bufferHistory[i]);
        
        if (i === 0) {
            ctx.moveTo(x, y);
        } else {
            ctx.lineTo(x, y);
        }
    }
    
    ctx.stroke();
    
    // Reset shadow values for other drawing
    ctx.shadowBlur = 0;
    ctx.shadowColor = 'transparent';
    
    // Draw active point
    if (bufferHistory.length > 0) {
        const lastVal = bufferHistory[bufferHistory.length - 1];
        ctx.fillStyle = 'hsl(174, 90%, 46%)';
        ctx.beginPath();
        ctx.arc(w - 2, mmToY(lastVal), 4, 0, Math.PI * 2);
        ctx.fill();
    }
}

// Start execution
connectSSE();
drawChart();
