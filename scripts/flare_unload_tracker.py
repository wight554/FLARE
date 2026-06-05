#!/usr/bin/env python3
"""
flare_unload_tracker.py — Live high-frequency diagnostic tracker for manual Unload (UL) tasks.
Polls status (?:) at 50Hz, records buffer position/velocity telemetry,
computes variance/vibration metrics, and renders a visual ASCII plot of retract travel.

Usage:
    python3 scripts/flare_unload_tracker.py [--port PORT] [--trigger LANE]
"""

import argparse
import glob
import statistics
import sys
import time

try:
    import serial
except ImportError:
    print("flare_unload_tracker: 'pyserial' not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def find_serial_port():
    ports = glob.glob('/dev/tty.usbmodem*') + glob.glob('/dev/ttyACM*')
    if not ports:
        print("flare_unload_tracker: no serial port found", file=sys.stderr)
        sys.exit(1)
    return ports[0]


def open_port(port):
    try:
        return serial.Serial(port, 115200, timeout=0.5)
    except Exception as e:
        print(f"flare_unload_tracker: failed to open port {port}: {e}", file=sys.stderr)
        sys.exit(1)


def send_cmd(ser, cmd):
    ser.reset_input_buffer()
    ser.write(f"{cmd}\n".encode())


def parse_status(line):
    """Parse ?: response string into a key-value dictionary."""
    if not line.startswith("OK:") and not line.startswith("EV:"):
        # Strip potential garbage
        if "OK:" in line:
            line = line[line.find("OK:"):]
        else:
            return None

    parts = line.strip().split(":")
    if len(parts) < 2:
        return None

    payload = parts[1]
    kv_pairs = payload.split(",")
    data = {}
    for pair in kv_pairs:
        if "=" in pair:
            # handle nested equals if any
            sub_parts = pair.split("=")
            data[sub_parts[0]] = sub_parts[1]
        elif ":" in pair:
            sub_parts = pair.split(":")
            data[sub_parts[0]] = sub_parts[1]
        else:
            # Standard comma-separated K:V pairs
            pair.split(":") if ":" in pair else pair.split("=")
            # standard parsing
            if len(pair.split(":")) == 2:
                k, v = pair.split(":")
                data[k] = v
            else:
                # search for first colon or comma
                pass

    # Clean fallback standard parsing
    data_clean = {}
    for item in kv_pairs:
        # Split on first occurrence of ':'
        if ':' in item:
            k, v = item.split(':', 1)
            data_clean[k.strip()] = v.strip()
        elif '=' in item:
            k, v = item.split('=', 1)
            data_clean[k.strip()] = v.strip()
    return data_clean


def draw_ascii_plot(positions, timestamps):
    if not positions:
        return

    print("\n" + "="*80)
    print(" LIVE BUFFER RETRACT PATH GRAPH (-1.0 = Compression, 1.0 = Tension)")
    print("="*80)

    width = 60
    min_pos = -1.0
    max_pos = 1.0
    span = max_pos - min_pos

    for t, pos in zip(timestamps, positions):
        # Normalize position to width
        norm_pos = (pos - min_pos) / span
        norm_pos = max(0.0, min(1.0, norm_pos))
        col = int(norm_pos * (width - 1))

        # Build line
        line = [" "] * width
        line[width // 2] = "|" # Center neutral marker

        if col < width // 2:
            for i in range(col, width // 2):
                line[i] = "-"
            line[col] = "<"
        elif col > width // 2:
            for i in range(width // 2 + 1, col + 1):
                line[i] = "-"
            line[col] = ">"
        else:
            line[col] = "O"

        rel_time = t - timestamps[0]
        print(f"[{rel_time:5.2f}s] {''.join(line)}  ({pos:+.2f})")
    print("="*80)


def main():
    parser = argparse.ArgumentParser(description="FLARE Unload Telemetry Tracker")
    parser.add_argument("--port", type=str, default=None, help="Serial port (auto-detects if omitted)")
    parser.add_argument("--trigger", type=int, choices=[1, 2], default=None, help="Trigger unload on Lane 1 or Lane 2")
    args = parser.parse_args()

    port = args.port if args.port else find_serial_port()
    print(f"# Connecting to FLARE on {port}...")
    ser = open_port(port)
    time.sleep(0.5) # Wait for serial stabilize

    if args.trigger:
        lane = args.trigger
        print(f"# Triggering unload on Lane {lane}...")
        send_cmd(ser, f"UL:{lane}")
        # Read the immediate response
        deadline = time.time() + 1.0
        while time.time() < deadline:
            if ser.in_waiting:
                resp = ser.readline().decode(errors='ignore').strip()
                if resp.startswith("OK"):
                    print(f"# Unload trigger accepted: {resp}")
                    break
                elif resp.startswith("ER"):
                    print(f"Error triggering unload: {resp}", file=sys.stderr)
                    sys.exit(1)

    print("# Waiting for UNLOAD task to begin... (Trigger via UI/Klipper if not using --trigger)")
    active_lane = 1
    recording = False

    positions = []
    velocities = []
    timestamps = []

    poll_interval_s = 0.02 # 50Hz

    try:
        while True:
            now = time.time()
            # Send status command ?: at high frequency
            send_cmd(ser, "?:")

            # Read response
            resp_line = ""
            deadline = time.time() + 0.1
            while time.time() < deadline:
                if ser.in_waiting:
                    resp_line = ser.readline().decode(errors='ignore').strip()
                    break

            if not resp_line:
                continue

            data = parse_status(resp_line)
            if not data:
                continue

            # Extract active lane and its task
            lane_str = data.get("LN", "1")
            active_lane = int(lane_str)
            task_key = "L1T" if active_lane == 1 else "L2T"
            task = data.get(task_key, "IDLE")

            g_buf_pos = float(data.get("BP", "0.0"))
            arm_vel = float(data.get("AV", "0.0"))

            if task == "UNLOAD":
                if not recording:
                    print("\n# >>> UNLOAD TASK STARTED! Recording telemetry... Press Ctrl+C to abort.")
                    recording = True
                    positions.clear()
                    velocities.clear()
                    timestamps.clear()

                positions.append(g_buf_pos)
                velocities.append(arm_vel)
                timestamps.append(now)

                # Live single-line status
                sys.stdout.write(f"\rRecording... Time: {now - timestamps[0]:5.2f}s | Pos: {g_buf_pos:+.2f} | Vel: {arm_vel:+.2f}  ")
                sys.stdout.flush()

            else:
                if recording:
                    print(f"\n# <<< UNLOAD TASK FINISHED! (Status: {task})")
                    break

            time.sleep(poll_interval_s)

    except KeyboardInterrupt:
        print("\n# Stopped by user.")
        if recording:
            print("# Processing collected data up to abort...")

    if not positions:
        print("# No telemetry data was collected.")
        ser.close()
        sys.exit(0)

    # Statistical analysis
    duration = timestamps[-1] - timestamps[0]
    p_mean = statistics.mean(positions)
    p_min = min(positions)
    p_max = max(positions)

    # Calculate position vibration (Standard Deviation)
    if len(positions) > 1:
        p_std = statistics.stdev(positions)
        v_std = statistics.stdev(velocities)
    else:
        p_std = 0.0
        v_std = 0.0

    print("\n" + "="*80)
    print(" TELEMETRY RETRACT REPORT")
    print("="*80)
    print(f"Total Samples:      {len(positions)}")
    print(f"Duration:           {duration:.2f} seconds")
    print(f"Arm Position Range: {p_min:+.2f} to {p_max:+.2f}")
    print(f"Arm Position Mean:  {p_mean:+.2f}")
    print(f"Position Jitter (StdDev):  {p_std:5.4f}  <-- MAIN RESEARCH METRIC")
    print(f"Velocity Jitter (StdDev):  {v_std:5.4f}")

    # Diagnostic recommendation
    print("-"*80)
    print("DIAGNOSTIC RECOMMENDATION:")
    if p_std > 0.025:
        print(f" -> Position Jitter is HIGH ({p_std:.4f}). Filament is sliding freely with normal friction wiggles.")
        print(" -> RETRACT STATUS: FREE / SUCCESS")
    else:
        print(f" -> Position Jitter is EXTREMELY LOW ({p_std:.4f}). Arm is held completely rigid.")
        print(" -> RETRACT STATUS: POTENTIAL JAM / EXTRUDER GRIP BLOCK")
    print("="*80)

    # Render ASCII graph
    # Downsample points to avoid drawing thousands of lines
    step = max(1, len(positions) // 40)
    draw_ascii_plot(positions[::step], timestamps[::step])

    ser.close()


if __name__ == '__main__':
    main()
