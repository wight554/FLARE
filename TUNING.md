# FLARE Tuning Guide

This guide describes how to tune your FLARE controller so that filament feed rates and buffer response are perfectly matched to your printer.

---

## ⏱️ 5-Minute Quick Start Tuning

### Step 1: Check your sensor type
Open your `config.ini` and locate the `buf_sensor_type` key:
- `buf_sensor_type: 0` → **Type-D (Dual-switch limit switches)**
- `buf_sensor_type: 1` → **Type-P (Hall-effect or analog proportional)**

---

### 🟢 Path A: Tuning Type-D (Switch-based Relay Buffer)

Type-D buffers use simple limit switches to trigger feeding. They do not require complex flow schedules or mathematical analyzer scripts.

1. **Verify sensor triggers**:
   Run `scripts/flare_cmd.py "?:"` and physically pull/push the buffer trolley:
   - When pulling filament tight: `BP` state should report `TENSION` (T).
   - When releasing filament (trolley compresses spring): `BP` state should report `COMPRESSION` (C).
   - When floating in the middle: `BP` state should report `NEUTRAL` (0).
2. **Tune fallback feed rates**:
   If the motor feeds too slow or pulls too hard, edit these keys under the `[sync]` section of `config.ini`:
   - `baseline_rate`: The speed (in mm/s) the motor feeds when `TENSION` is triggered. Increase this if the extruder is starving during fast prints.
   - `sync_compression_bias_frac`: The fraction of baseline speed the motor runs when returning to neutral.
3. **Re-flash**:
   ```bash
   python3 scripts/gen_config.py
   python3 scripts/flash_flare.py
   ```

---

### 🔵 Path B: Tuning Type-P (Hall-effect Proportional Buffer)

Type-P buffers read the exact position of the trolley and adjust the motor speed continuously using the PSF control law.

1. **Calibrate Sensor Limits (Automatic Wizard)**:
   Before printing, measure and calibrate ADC thresholds across Neutral, Compression, and Tension stops:
   ```bash
   python3 scripts/flare_calibrate.py --interactive --port /dev/ttyACM0 --write-firmware --write-config
   ```
   *The wizard averages multiple ADC samples, verifies noise stability and sensor directionality, updates `config.ini`, and persists values directly to RP2040 flash.*

2. **Adjust PSF Proportional Gain Live**:
   Tweak proportional tracking rate or damping over USB CDC on the fly:
   ```bash
   python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 --set-kp 1200
   python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 --dump-psf
   ```

3. **Run a test print with the live tuner**:
   Start a normal print, then observe the buffer in real-time:
   ```bash
   python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 --observe-daemon --csv-out run_data.csv
   ```

4. **Analyze and Chart**:
   Generate the recommended flow schedule and inspect the step-rate vs buffer displacement chart:
   ```bash
   python3 scripts/flare_analyze.py --in run_data.csv --chart displacement_chart.svg --out flow_patch.ini
   ```
   Open `displacement_chart.svg` in any browser to inspect the trolley deflection curves across zones.
   Copy the recommended parameters into your `config.ini` file, regenerate config, and flash.

---

## 📖 Under the Hood: How FLARE Sync Works

FLARE keeps a small reserve of filament in the buffer spring so that the printer's extruder doesn't have to pull against the heavy inertia of the filament spool.

- **Tension Zone**: When the trolley moves toward the tension switch (trolley pulled forward), the motor speeds up to feed filament.
- **Compression Zone**: When the trolley compresses the spring (too much filament fed), the motor slows down or stops to let the extruder catch up.
- **Neutral Zone**: The motor matches the estimated print speed.

---

## 🔬 Appendix: Advanced Calibration & Mathematical Analyzer

> [!NOTE]
> This section is for developers and advanced users who want to use offline data collection and mathematical modeling (regression, standard deviation, and centroid calculation) to tune high-speed multi-material printing.

### Dynamic Calibration Script
To build a highly accurate flow schedule profile, you can inject markers into your G-code and analyze the execution logs.

1. **Tag your G-code**:
   ```bash
   python3 scripts/gcode_marker.py my_print.gcode --output my_print.flare.gcode
   ```
2. **Record a calibration run**:
   Start the print and log the telemetry:
   ```bash
   python3 scripts/flare_live_tuner.py --port /dev/ttyACM0 --csv-out run_data.csv --klipper-uds ~/printer_data/comms/klippy.sock
   ```
3. **Analyze flow rates**:
   ```bash
   python3 scripts/flare_analyze.py --profile-slow run_data.csv --emit-flow-schedule --out flow-schedule.ini
   ```
   *The analyzer processes the CSV, clusters data points using weighted centroids, filters noise outside the target sigma threshold, and outputs a refined mathematical flow curve.*
