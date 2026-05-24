// FLARE WebUI Application Script

// State cache
let boardOnline = false;
let activeLane = 0;
let lastTelemetryData = null;
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
        fetchGateMap();
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
    ['btn-preload', 'btn-eject', 'btn-checkgate',
     'btn-unload', 'btn-load', 'btn-extrude', 'btn-retract'].forEach((id) => setBtnEnabled(id, false));
}

function updateUIState(data) {
    lastTelemetryData = data;
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
    
    // 4. Active lane highlight on the spool cards
    activeLane = data.active_lane;
    updateLaneHighlight();

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

// ---- Spool cards (gate map): display + lane select + inline edit ----
let gateMap = null;

function fetchGateMap() {
    fetch('/gatemap')
        .then((r) => (r.ok ? r.json() : null))
        .then((d) => { if (d && d.gates) { gateMap = d; renderSpoolCards(); } })
        .catch(() => {});
}

// Effective display values: local gate-map value wins, else Spoolman enrichment.
function spoolDisplay(g) {
    const sp = g.spool || {};
    const color = (g.color && g.color.trim()) ? g.color : (sp.color_hex || '');
    const material = (g.material && g.material.trim()) ? g.material : (sp.material || '');
    const localName = (g.name && g.name.trim() && !/^Gate \d+$/.test(g.name)) ? g.name : '';
    const name = localName || sp.name || g.name || '';
    return {
        color, material, name,
        remaining: sp.remaining_weight,
        used: g.used,
        spool_id: g.spool_id,
    };
}

function renderSpoolCards() {
    const wrap = document.getElementById('spool-cards');
    if (!wrap || !gateMap) return;
    wrap.innerHTML = '';
    gateMap.gates.forEach((g, i) => {
        const lane = i + 1;
        const d = spoolDisplay(g);
        const colorCss = d.color ? ('#' + d.color.replace(/^#/, '')) : '#5a5f6a';
        
        const subParts = [];
        if (d.material) subParts.push(escapeHtml(d.material));
        subParts.push('Lane ' + lane);
        subParts.push('<span class="sub-separator" style="display: none;"> · </span><span class="spool-card-status"></span>');
        
        if (typeof d.remaining === 'number') subParts.push(Math.round(d.remaining) + 'g');
        else if (d.used && d.used.used_g) subParts.push('used ' + Math.round(d.used.used_g) + 'g');
        else if (d.spool_id >= 0) subParts.push('#' + d.spool_id);

        const card = document.createElement('div');
        const active = activeLane === lane;
        const loaded = isLaneLoaded(lane, lastTelemetryData);
        card.className = 'spool-card' + (active ? ' active' : '') + (loaded ? ' loaded' : '');
        card.dataset.lane = String(lane);
        card.innerHTML =
            '<div class="spool-card-main" onclick="selectLane(' + lane + ')">' +
              '<span class="spool-icon" style="--c:' + colorCss + '">' +
                '<svg viewBox="0 0 48 48"><circle cx="24" cy="24" r="20" fill="var(--c)"/>' +
                '<circle cx="24" cy="24" r="7" fill="#0a0d16"/>' +
                '<circle cx="24" cy="24" r="20" fill="none" stroke="rgba(0,0,0,0.35)" stroke-width="2"/></svg>' +
              '</span>' +
              '<span class="spool-info">' +
                '<span class="spool-card-name">' + (escapeHtml(d.name) || ('Gate ' + i)) + '</span>' +
                '<span class="spool-card-sub">' + subParts.join(' · ') + '</span>' +
              '</span>' +
            '</div>' +
            '<button class="spool-edit" title="Edit spool" onclick="openSpoolEdit(' + i + ')">✎</button>' +
            '<div class="spool-edit-form" id="spool-edit-' + i + '" hidden>' +
              '<label>Material<input type="text" id="se-material-' + i + '"></label>' +
              '<label>Color<input type="color" id="se-color-' + i + '"></label>' +
              '<label>Name<input type="text" id="se-name-' + i + '"></label>' +
              '<label>Spool ID<input type="number" id="se-spool-' + i + '" min="-1" step="1"></label>' +
              '<div class="spool-edit-actions">' +
                '<button class="btn btn-primary" onclick="saveSpoolEdit(' + i + ')">Save</button>' +
                '<button class="btn btn-secondary" onclick="closeSpoolEdit(' + i + ')">Cancel</button>' +
              '</div>' +
            '</div>';
        wrap.appendChild(card);
    });
}

function updateLaneHighlight() {
    document.querySelectorAll('#spool-cards .spool-card').forEach((c) => {
        const lane = Number(c.dataset.lane);
        const isActive = (lane === activeLane);
        const loaded = isLaneLoaded(lane, lastTelemetryData);

        c.classList.toggle('active', isActive);
        c.classList.toggle('loaded', loaded);

        // Update status text inline
        const statusEl = c.querySelector('.spool-card-status');
        if (statusEl) {
            let statusHtml = '';
            if (isActive && loaded) {
                statusHtml = '<span class="status-active">Active</span> &amp; <span class="status-loaded">Loaded</span>';
            } else if (isActive) {
                statusHtml = '<span class="status-active">Active</span>';
            } else if (loaded) {
                statusHtml = '<span class="status-loaded">Loaded</span>';
            }
            statusEl.innerHTML = statusHtml;
            statusEl.style.display = statusHtml ? 'inline' : 'none';
            
            // Toggle separator before the status element
            const sepEl = statusEl.previousElementSibling;
            if (sepEl && sepEl.classList.contains('sub-separator')) {
                sepEl.style.display = statusHtml ? 'inline' : 'none';
            }
        }
    });
}

function selectLane(lane) {
    sendCustomCommand('T:' + lane);
}

function openSpoolEdit(i) {
    if (!gateMap) return;
    const g = gateMap.gates[i];
    const d = spoolDisplay(g);
    document.getElementById('se-material-' + i).value = g.material || '';
    document.getElementById('se-color-' + i).value = d.color
        ? ('#' + d.color.replace(/^#/, '').slice(0, 6).padStart(6, '0')) : '#888888';
    document.getElementById('se-name-' + i).value =
        (g.name && !/^Gate \d+$/.test(g.name)) ? g.name : '';
    document.getElementById('se-spool-' + i).value = (g.spool_id >= 0) ? g.spool_id : '';
    document.getElementById('spool-edit-' + i).hidden = false;
}

function closeSpoolEdit(i) {
    const el = document.getElementById('spool-edit-' + i);
    if (el) el.hidden = true;
}

function saveSpoolEdit(i) {
    const spoolRaw = document.getElementById('se-spool-' + i).value.trim();
    const body = {
        gate: i,
        material: document.getElementById('se-material-' + i).value.trim(),
        color: document.getElementById('se-color-' + i).value.replace(/^#/, ''),
        name: document.getElementById('se-name-' + i).value.trim(),
    };
    if (spoolRaw !== '') body.spool_id = parseInt(spoolRaw, 10);
    fetch('/gatemap', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
    })
        .then((r) => (r.ok ? r.json() : null))
        .then((d) => { if (d && d.gates) { gateMap = d; renderSpoolCards(); } })
        .catch(() => closeSpoolEdit(i));
}

function escapeHtml(s) {
    if (s == null) return '';
    return String(s).replace(/[&<>"']/g, (c) =>
        ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// Happy Hare gate_status for a lane: 0 = empty, 1 = available (preloaded),
// 2 = loaded/buffer (loaded past the gate and occupying splitter).
function gateStatusForLane(lane, data) {
    const inSw = lane === 1 ? data.in1 : (lane === 2 ? data.in2 : 0);
    const outSw = lane === 1 ? data.out1 : (lane === 2 ? data.out2 : 0);
    if (outSw && data.y_split) return 2;
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

    // Preload (LO:): only when the gate is empty (no IN). LO: spins the gear
    // and grabs filament as it is inserted (the manual preload flow for setups
    // with AUTO_PRELOAD disabled). Disabled once filament is present.
    setBtnEnabled('btn-preload', online && hasLane && gateStatus === 0);
    // Eject (UM:): only when the active gate holds filament
    setBtnEnabled('btn-eject', online && hasLane && gateStatus !== 0);
    // Check Gate (?:): always available while online
    setBtnEnabled('btn-checkgate', online);
    // Unload (UL:): only when filament is loaded to the toolhead
    setBtnEnabled('btn-unload', online && hasLane && loaded);
    // Load (FL:): only when not already loaded
    setBtnEnabled('btn-load', online && hasLane && !loaded);

    // Manual move (MV:) acts on the active lane; needs one selected
    setBtnEnabled('btn-extrude', online && hasLane);
    setBtnEnabled('btn-retract', online && hasLane);
}

function updateSensor(id, state) {
    const el = document.getElementById(id);
    if (state === 1) {
        el.className = 'sensor-indicator active';
    } else {
        el.className = 'sensor-indicator';
    }
}

// Check if a lane is loaded, judged by OUT sensor and y_split combiner sensor (YS).
function isLaneLoaded(lane, data) {
    if (!data) return false;
    const outSw = lane === 1 ? data.out1 : (lane === 2 ? data.out2 : 0);
    return !!(outSw && data.y_split);
}

// Load the active lane. Prompts the user with a confirmation dialog if the other lane is loaded (judged by OUT and YS).
function loadActiveLane() {
    if (lastTelemetryData) {
        const lane = lastTelemetryData.active_lane;
        const otherLane = lane === 1 ? 2 : (lane === 2 ? 1 : 0);
        if (otherLane > 0 && isLaneLoaded(otherLane, lastTelemetryData)) {
            if (!confirm("Make sure you unloaded current lane from toolhead. Proceed?")) {
                return;
            }
        }
    }
    sendCustomCommand('FL:');
}

// Unload the active lane. Prompts the user with a confirmation dialog if the lane is loaded (judged by OUT and YS).
function unloadActiveLane() {
    if (lastTelemetryData && isLaneLoaded(activeLane, lastTelemetryData)) {
        if (!confirm("Make sure you unloaded current lane from toolhead. Proceed?")) {
            return;
        }
    }
    sendCustomCommand('UL:');
}

// Eject the lane currently selected in the UI. Sends an explicit UM:<lane>
// (matching Fluidd's explicit-gate eject) so the target is pinned to the
// active lane rather than relying on the board's bare-UM active default.
// Prompts the user with a confirmation dialog if the lane is loaded (judged by OUT and YS).
function ejectActiveLane() {
    if (lastTelemetryData && isLaneLoaded(activeLane, lastTelemetryData)) {
        if (!confirm("Make sure you unloaded current lane from toolhead. Proceed?")) {
            return;
        }
    }
    if (activeLane === 1 || activeLane === 2) {
        sendCustomCommand('UM:' + activeLane);
    } else {
        sendCustomCommand('UM:');
    }
}

// Manual filament move on the active lane via MV:<mm>:<feed_mm_min>.
// Inputs are mm and mm/s; firmware wants mm/min, so speed is multiplied by 60.
// Positive distance = extrude (forward), negative = retract.
function moveFilament(dir) {
    const len = parseFloat(document.getElementById('mv-length').value);
    const spd = parseFloat(document.getElementById('mv-speed').value);
    const feedback = document.getElementById('cmd-feedback');
    if (!(len > 0) || !(spd > 0)) {
        feedback.className = 'cmd-feedback error';
        feedback.textContent = 'Manual move: length and speed must be > 0';
        return;
    }
    const mm = (dir < 0 ? -len : len).toFixed(2);
    const feedMmMin = Math.round(spd * 60);
    sendCustomCommand(`MV:${mm}:${feedMmMin}`);
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
fetchGateMap();
// Refresh spool data periodically for live Spoolman remaining-weight updates
setInterval(fetchGateMap, 30000);
