#!/usr/bin/env python3
"""
flare_daemon.py — persistent background serial multiplexer, telemetry caching, 
and EventSource (SSE) host server for FLARE.

Exposes:
  - GET /status : latest cached telemetry from RP2040 (JSON)
  - POST /cmd  : sends command to board, blocks, returns board response (OK: / ER:)
  - GET /telemetry : Server-Sent Events (SSE) 20Hz real-time telemetry stream
  - GET / : Serves the integrated HTML5/Canvas dashboard UI
"""

import os
import sys
import time
import argparse
import threading
import queue
import json
import glob
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

# Add scripts directory to path to import serial_utils
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
try:
    import serial_utils
except ImportError:
    # Inline fallback if executed in isolated environments
    class serial_utils:
        @staticmethod
        def find_port(pref=None):
            candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*") + glob.glob("/dev/cu.usbmodem*")
            if pref in candidates:
                return pref
            return candidates[0] if candidates else None

try:
    import serial
except ImportError:
    print("flare_daemon error: 'pyserial' not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# Global runtime state
serial_port = None
serial_lock = threading.Lock()
command_event = threading.Event()
command_reply = None
current_executing_command = None

# Thread-safe status cache
status_lock = threading.Lock()
status_cache = {
    "board_online": False,
    "active_lane": 0,
    "tc_state": "UNKNOWN",
    "g_buf_pos": 0.0,
    "buf_state": "NEUTRAL",
    "sync_feedback": 0.0,
    "sync_feedback_state": "neutral",
    "sync_drive": False,
    "sps": 0.0,
    "baseline_sps": 0.0,
    "extruder_est_sps": 0.0,
    "reserve_error_mm": 0.0,
    "in1": 0, "out1": 0,
    "in2": 0, "out2": 0,
    "toolhead": 0, "y_split": 0,
    "sync_enabled": 0,
    "reload_mode": 0,
    "timestamp": 0.0
}

# Real-time EventSource (SSE) queues
sse_queues_lock = threading.Lock()
active_sse_queues = set()

# Event history
event_history = []
event_history_lock = threading.Lock()

def add_event_to_history(evt_type, evt_data):
    with event_history_lock:
        event_history.append({
            "time": time.time(),
            "type": evt_type,
            "data": evt_data
        })
        if len(event_history) > 100:
            event_history.pop(0)

def parse_status_line(line):
    """
    Parse a raw serial status line (e.g. 'OK:LN:1,TC:IDLE,L1T:NONE,...')
    and update status_cache.
    """
    if line.startswith("OK:"):
        line = line[3:]
    
    parts = line.strip().split(",")
    new_data = {}
    raw_fields = {}
    
    for part in parts:
        if ":" not in part:
            continue
        # Split at first colon only
        key, val = part.split(":", 1)
        raw_fields[key.strip()] = val.strip()
        
        try:
            if key == "LN":
                new_data["active_lane"] = int(val)
            elif key == "TC":
                new_data["tc_state"] = val
            elif key == "BP":
                new_data["g_buf_pos"] = float(val)
                # Rescale to Happy Hare -1.0 to 1.0 (assuming 15.0mm max travel)
                new_data["sync_feedback"] = max(-1.0, min(1.0, float(val) / 15.0))
            elif key == "BUF":
                new_data["buf_state"] = val
                new_data["sync_feedback_state"] = val.lower()
            elif key == "SM":
                new_data["sync_enabled"] = int(val)
                new_data["sync_drive"] = (int(val) == 1)
            elif key == "MM":
                new_data["sps"] = float(val)
            elif key == "BL":
                new_data["baseline_sps"] = float(val)
            elif key == "EST":
                new_data["extruder_est_sps"] = float(val)
            elif key == "RE":
                new_data["reserve_error_mm"] = float(val)
            elif key == "I1":
                new_data["in1"] = int(val)
            elif key == "O1":
                new_data["out1"] = int(val)
            elif key == "I2":
                new_data["in2"] = int(val)
            elif key == "O2":
                new_data["out2"] = int(val)
            elif key == "TH":
                new_data["toolhead"] = int(val)
            elif key == "YS":
                new_data["y_split"] = int(val)
            elif key == "RELOAD":
                new_data["reload_mode"] = int(val)
            elif key == "CU":
                new_data["enable_cutter"] = int(val)
            elif key == "UC":
                new_data["unload_cut"] = int(val)
        except ValueError:
            pass # ignore malformed metrics
            
    if new_data:
        new_data["raw_status"] = raw_fields
        new_data["board_online"] = True
        new_data["timestamp"] = time.time()
        with status_lock:
            status_cache.update(new_data)
        
        # Broadcast to all active SSE queues
        broadcast_telemetry(new_data)

def broadcast_telemetry(data):
    payload = json.dumps(data)
    with sse_queues_lock:
        for q in list(active_sse_queues):
            try:
                q.put_nowait(payload)
            except queue.Full:
                pass

def serial_reader(port_name, baud):
    global serial_port, command_reply, current_executing_command
    
    while True:
        print(f"flare_daemon: connecting to {port_name}...")
        try:
            with serial_lock:
                serial_port = serial.Serial(port_name, baud, timeout=1.0)
                serial_port.reset_input_buffer()
            
            print(f"flare_daemon: connected to {port_name} successfully")
            with status_lock:
                status_cache["board_online"] = True
            
            while True:
                line_bytes = serial_port.readline()
                if not line_bytes:
                    continue
                
                line = line_bytes.decode("utf-8", errors="ignore").strip()
                if not line:
                    continue
                
                # Check for asynchronous Event stream
                if line.startswith("EV:"):
                    evt_body = line[3:]
                    evt_type = evt_body.split(",")[0] if "," in evt_body else evt_body
                    evt_data = evt_body.split(",", 1)[1] if "," in evt_body else ""
                    print(f"flare_daemon Event: {line}")
                    add_event_to_history(evt_type, evt_data)
                    broadcast_telemetry({"event_type": evt_type, "event_data": evt_data})
                    
                # Check for command reply
                elif line.startswith("OK:") or line.startswith("ER:") or line == "OK":
                    # If it's a status dump response (starts with OK:LN: or OK:LN=)
                    if "LN:" in line:
                        parse_status_line(line)
                        if current_executing_command and not current_executing_command.startswith("?"):
                            continue
                    
                    command_reply = line
                    command_event.set()
                
                # Raw status dump line (safety fallback)
                elif "LN:" in line and "TC:" in line:
                    parse_status_line(line)
                    
        except (serial.SerialException, OSError) as e:
            print(f"flare_daemon connection error: {e}", file=sys.stderr)
            with status_lock:
                status_cache["board_online"] = False
            broadcast_telemetry({"board_online": False})
            
            with serial_lock:
                if serial_port:
                    try:
                        serial_port.close()
                    except Exception:
                        pass
                    serial_port = None
            
            time.sleep(2.0)

def status_poller():
    """Background status poller (requests status updates at 5Hz)."""
    while True:
        time.sleep(0.2)
        
        # Only write status poll command if no command execution is active
        with serial_lock:
            if serial_port and serial_port.is_open:
                try:
                    # Write status request to board
                    serial_port.write(b"?: \n")
                    serial_port.flush()
                except Exception:
                    pass

# ---------------------------------------------------------------------------
# HTTP Handler & Server Mixins
# ---------------------------------------------------------------------------
class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    """Handle requests in separate threads."""
    allow_reuse_address = True
    daemon_threads = True

class FlareHTTPHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Suppress spammy log dumps for telemetry requests
        if args and isinstance(args[0], str) and ("GET /telemetry" in args[0] or "GET /status" in args[0]):
            return
        super().log_message(format, *args)

    def do_GET(self):
        if self.path == "/status":
            with status_lock:
                snapshot = dict(status_cache)
            with event_history_lock:
                snapshot["events"] = list(event_history)
            res = json.dumps(snapshot)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(res.encode("utf-8"))
            
        elif self.path == "/telemetry":
            # Server-Sent Events (SSE) Stream
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            
            # Create a queue for this stream connection
            q = queue.Queue(maxsize=50)
            with sse_queues_lock:
                active_sse_queues.add(q)
                
            print(f"flare_daemon: SSE telemetry stream client connected (total active: {len(active_sse_queues)})")
            
            try:
                # Send current initial state immediately
                with status_lock:
                    initial_payload = json.dumps(status_cache)
                self.wfile.write(f"data: {initial_payload}\n\n".encode("utf-8"))
                self.wfile.flush()
                
                while True:
                    try:
                        # Wait for next broadcast frame
                        payload = q.get(timeout=5.0)
                        self.wfile.write(f"data: {payload}\n\n".encode("utf-8"))
                        self.wfile.flush()
                    except queue.Empty:
                        # Heartbeat frame to keep connection alive
                        self.wfile.write(b": keepalive\n\n")
                        self.wfile.flush()
            except Exception:
                # Client disconnected
                pass
            finally:
                with sse_queues_lock:
                    active_sse_queues.discard(q)
                print(f"flare_daemon: SSE telemetry stream client disconnected (remaining: {len(active_sse_queues)})")
                
        elif self.path == "/" or self.path == "/index.html":
            self.serve_static_file("index.html", "text/html")
        elif self.path == "/app.js":
            self.serve_static_file("app.js", "application/javascript")
        elif self.path == "/style.css":
            self.serve_static_file("style.css", "text/css")
        else:
            self.send_error(404, "File Not Found")

    def do_POST(self):
        if self.path == "/cmd":
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            
            try:
                body = json.loads(post_data.decode("utf-8"))
                cmd_str = body.get("cmd", "").strip()
            except Exception:
                self.send_error(400, "Invalid JSON payload")
                return
            
            if not cmd_str:
                self.send_error(400, "Missing cmd parameter")
                return
            
            # Send command directly to board with lock
            response = self.execute_serial_command(cmd_str)
            
            if response is None:
                self.send_response(504) # Gateway Timeout
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(json.dumps({"error": "command_timeout"}).encode("utf-8"))
            else:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(json.dumps({"response": response}).encode("utf-8"))
        else:
            self.send_error(404, "Not Found")

    def do_OPTIONS(self):
        # Support CORS pre-flight requests
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def serve_static_file(self, filename, content_type):
        webui_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "webui")
        filepath = os.path.join(webui_dir, filename)
        
        if not os.path.exists(filepath):
            # Fallback inline creation for first boot / recovery
            self.send_error(404, f"{filename} not found")
            return
            
        try:
            with open(filepath, "rb") as f:
                content = f.read()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        except Exception as e:
            self.send_error(500, f"Error reading file: {e}")

    def execute_serial_command(self, cmd_str):
        global command_reply, current_executing_command
        
        # Formatting check
        if not cmd_str.endswith("\n"):
            cmd_str += "\n"
            
        with serial_lock:
            if not serial_port or not serial_port.is_open:
                return "ER:BOARD_OFFLINE"
                
            command_event.clear()
            command_reply = None
            current_executing_command = cmd_str.strip()
            
            try:
                serial_port.write(cmd_str.encode("utf-8"))
                serial_port.flush()
            except Exception as e:
                current_executing_command = None
                return f"ER:WRITE_ERROR:{e}"
                
            # Block until event is fired (timeout 10.0s for typical moves)
            # Long commands (FL, UL, TC) execute async and return OK immediately.
            success = command_event.wait(timeout=10.0)
            current_executing_command = None
            
            if success:
                return command_reply
            else:
                return None


def klipper_syncer(moonraker_url):
    """Background thread to push status updates to Moonraker at 4Hz."""
    last_sync = {}
    backoff = 0.0
    last_force_sync = 0.0
    was_online = False

    while True:
        time.sleep(0.25)
        
        # Check backoff timer
        if backoff > time.time():
            continue

        # Get copy of current cache
        with status_lock:
            state = dict(status_cache)

        # Detect changes in values of interest
        keys = [
            "board_online", "active_lane", "tc_state", "g_buf_pos", 
            "buf_state", "sps", "in1", "out1", "in2", "out2", 
            "toolhead", "y_split", "reload_mode"
        ]
        
        changed = False
        for k in keys:
            if state.get(k) != last_sync.get(k):
                changed = True
                break

        # Force full sync every 10 seconds to recover if Klipper/Moonraker restarted
        if time.time() - last_force_sync > 10.0:
            changed = True

        board_online = state.get("board_online", False)
        trigger_board_sync = False
        if board_online and not was_online:
            trigger_board_sync = True
            changed = True

        if not changed:
            continue

        # Build SET_GCODE_VARIABLE commands
        lines = []
        for k in keys:
            val = state.get(k)
            if isinstance(val, bool):
                val_str = "1" if val else "0"
            elif isinstance(val, str):
                val_str = f"'\"{val}\"'"
            elif isinstance(val, float):
                val_str = f"{val:.3f}"
            else:
                val_str = str(val)
            lines.append(f"SET_GCODE_VARIABLE MACRO=_FLARE_STATE VARIABLE={k} VALUE={val_str}")

        # Add SET_MMU command to update Klipper native mmu object variables (Mainsail/Fluidd)
        active_lane = state.get("active_lane", 0)
        active_gate = active_lane - 1  # 0-indexed: L1 -> 0, L2 -> 1, none -> -1
        out1 = state.get("out1", 0)
        out2 = state.get("out2", 0)
        in1 = state.get("in1", 0)
        in2 = state.get("in2", 0)
        toolhead = state.get("toolhead", 0)
        g_buf_pos = state.get("g_buf_pos", 0.0)
        sync_feedback = max(-1.0, min(1.0, g_buf_pos / 15.0))
        buf_state = state.get("buf_state", "NEUTRAL").lower()
        tc_state = state.get("tc_state", "UNKNOWN")
        board_online = 1 if state.get("board_online", False) else 0
        sps = state.get("sps", 0.0)
        reload_mode = state.get("reload_mode", 0)
        enable_cutter = state.get("enable_cutter", 0)
        unload_cut = state.get("unload_cut", 0)

        # Map print job state and print state
        if tc_state in ["FOLLOW", "APPROACH"]:
            print_job_state = "printing"
            print_state = "printing"
        elif tc_state == "IDLE":
            print_job_state = "standby"
            print_state = "ready"
        else:
            print_job_state = "standby"
            print_state = "ready"

        y_split = state.get("y_split", 0)
        if in1 and out1 and y_split and toolhead:
            gate_status_1 = 2
        elif in1:
            gate_status_1 = 1
        else:
            gate_status_1 = 0

        if in2 and out2 and y_split and toolhead:
            gate_status_2 = 2
        elif in2:
            gate_status_2 = 1
        else:
            gate_status_2 = 0

        # Determine if any gate is actually fully loaded to the toolhead
        loaded_gate = -1
        if gate_status_1 == 2:
            loaded_gate = 0
        elif gate_status_2 == 2:
            loaded_gate = 1

        # Align klipper_tool and klipper_gate to active_gate so that UI highlights the selected card
        # and displays its spool details correctly.
        klipper_tool = active_gate
        klipper_gate = active_gate

        # Physical sensor states for active gate and combiner
        gate_sensor_active = out1 if active_gate == 0 else (out2 if active_gate == 1 else 0)
        extruder_sensor_active = y_split
        pre_gate_sensor_active = in1 if active_gate == 0 else (in2 if active_gate == 1 else 0)
        hub_sensor_active = y_split

        mmu_cmd = (
            f"SET_MMU NUM_GATES=2 ACTIVE_GATE={active_gate} GATE={klipper_gate} TOOL={klipper_tool} "
            f"GATE_STATUS='{gate_status_1},{gate_status_2}' GATE_SENSOR='{in1},{in2}' "
            f"TOOLHEAD_SENSOR={toolhead} SYNC_FEEDBACK={sync_feedback:.3f} "
            f"SYNC_FEEDBACK_STATE='{buf_state}' PRINT_JOB_STATE='{print_job_state}' "
            f"PRINT_STATE='{print_state}' BOARD_ONLINE={board_online} "
            f"SPS={sps:.3f} RELOAD_MODE={reload_mode} ENABLE_CUTTER={enable_cutter} "
            f"UNLOAD_CUT={unload_cut} GATE_SENSOR_ACTIVE={gate_sensor_active} "
            f"EXTRUDER_SENSOR_ACTIVE={extruder_sensor_active} "
            f"PRE_GATE_SENSOR_ACTIVE={pre_gate_sensor_active} HUB_SENSOR_ACTIVE={hub_sensor_active}"
        )

        lines.append(mmu_cmd)

        if trigger_board_sync:
            lines.append("_FLARE_SYNC_BOARD")

        gcode_script = "\n".join(lines)
        payload = json.dumps({"script": gcode_script}).encode("utf-8")

        # Make request to Moonraker
        try:
            req = urllib.request.Request(
                f"{moonraker_url}/printer/gcode/script",
                data=payload,
                headers={"Content-Type": "application/json"},
                method="POST"
            )
            with urllib.request.urlopen(req, timeout=1.0) as resp:
                if resp.status == 200:
                    last_sync = state
                    last_force_sync = time.time()
                    was_online = board_online
        except Exception:
            # Moonraker offline, backoff for 5.0 seconds
            last_sync = {} # Clear cache to force push on recovery
            backoff = time.time() + 5.0


# ---------------------------------------------------------------------------
# Main Execution
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="FLARE persistent host proxy daemon")
    parser.add_argument("--port", help="Serial port connection path (e.g. /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--host", default="0.0.0.0", help="HTTP server bind host (default: 0.0.0.0)")
    parser.add_argument("--api-port", type=int, default=8088, help="HTTP/SSE API server port (default: 8088)")
    parser.add_argument("--no-klipper", action="store_true", help="Bypass Moonraker/Klipper telemetry synchronization")
    parser.add_argument("--moonraker-url", default="http://localhost:7125", help="Moonraker base URL (default: http://localhost:7125)")
    args = parser.parse_args()
    
    # 1. Resolve preferred serial port candidate
    port_name = serial_utils.find_port(args.port)
    if not port_name:
        print("flare_daemon error: no serial devices found matching candidate patterns", file=sys.stderr)
        sys.exit(1)
        
    print(f"flare_daemon: resolved active port candidate -> {port_name}")
    
    # 2. Launch persistent background serial worker thread
    reader_t = threading.Thread(target=serial_reader, args=(port_name, args.baud), daemon=True)
    reader_t.start()
    
    # 3. Launch background status poller thread
    poller_t = threading.Thread(target=status_poller, daemon=True)
    poller_t.start()
    
    # 3.5 Launch Klipper telemetry syncer if enabled
    if not args.no_klipper:
        print(f"flare_daemon: Klipper telemetry syncer enabled targeting {args.moonraker_url}")
        syncer_t = threading.Thread(target=klipper_syncer, args=(args.moonraker_url,), daemon=True)
        syncer_t.start()
    
    # 4. Start HTTP & SSE proxy web server
    try:
        server = ThreadedHTTPServer((args.host, args.api_port), FlareHTTPHandler)
        print(f"flare_daemon: HTTP and SSE server running on http://{args.host}:{args.api_port}")
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nflare_daemon: shutting down...")
    except Exception as e:
        print(f"flare_daemon server error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
