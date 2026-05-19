#!/usr/bin/env python3
"""flare_analyze.py - offline calibration analyzer for FLARE.

Reads one or more CSV captures from flare_live_tuner.py, optionally
combines them with bucket state, and writes a review-only config patch.
Pure stdlib only.
"""

import argparse
import csv
import errno
import glob
import json
import math
import os
import statistics as stats
import sys
from collections import defaultdict

from path_utils import normalize_output, resolve_input, expand_input_paths, PathError

try:
    from flare_live_tuner import migrate_state_data
except ImportError:
    migrate_state_data = None


NOISE_RATIO_THR = 0.25
V_NOISE_FLOOR = 100.0
MIN_LEARN_EST_SPS = 100.0
MIN_COMPARABLE_BUCKETS = 3  # Per-run gate estimates need at least this many contributors.
MIN_RUN_BUCKET_ROWS = 50  # At least one contributing bucket must have this many rows in the run.
DENOMINATOR_MIN_BUCKET_N = 50  # Denominator in contributor_mass ignores buckets with n below this.
CONTRIBUTOR_MASS_PASS = 0.40  # Hard gate: below this, contributors are too small a mature-state minority.
CONTRIBUTOR_MASS_WARN = 0.65  # Soft warning below this contributor mass.
RAW_COVERAGE_WARN = 0.80  # Raw NEUTRAL-row coverage is diagnostic only, not a hard failure.
BIAS_SAFE_MIN = 0.05
BIAS_SAFE_MAX = 0.65
SIGMA_HARDWARE_CEILING_MM = 5.0  # Absolute FAIL floor: sensor/buffer mechanical failure.
DEFAULT_FLOW_SCHEDULE_CAP = 8
DEFAULTS = {
    "baseline_rate": 1600.0,
    "sync_compression_bias_frac": 0.4,
    "neutral_creep_timeout_ms": 4000.0,
    "neutral_creep_rate_sps_per_s": 5.0,
    "neutral_creep_cap_frac": 10.0,
    "buf_variance_blend_frac": 0.5,
    "buf_variance_blend_ref_mm": 1.0,
}
RELAY_DEFAULTS = {
    "relay_catchup_frac": 1.30,
    "relay_neutral_frac": 1.25,
    "relay_estimate_lo": 100.0,
    "relay_estimate_hi": 1600.0,
    "relay_confidence_cycles": 8.0,
    "relay_confidence_window_ms": 60000.0,
    "relay_seed_rate": 1600.0,
}
CONFIG_DEFAULTS = {**DEFAULTS, **RELAY_DEFAULTS}


def bin_v_fil(v):
    return int(round(float(v) / 25.0)) * 25


def bucket_label(feature, v_fil):
    return f"{feature}_v{bin_v_fil(v_fil)}"


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def percentile(values, pct):
    if not values:
        return 0.0
    vs = sorted(values)
    if len(vs) == 1:
        return vs[0]
    pos = (len(vs) - 1) * (pct / 100.0)
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return vs[lo]
    return vs[lo] + (vs[hi] - vs[lo]) * (pos - lo)


def median(values):
    return stats.median(values) if values else 0.0


def stdev(values):
    return stats.stdev(values) if len(values) > 1 else 0.0


def read_config(path):
    values = CONFIG_DEFAULTS.copy()
    if not path or not os.path.exists(path):
        return values
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line or ":" not in line:
                continue
            key, raw = line.split(":", 1)
            key = key.strip()
            if key not in values:
                continue
            try:
                values[key] = float(raw.strip().split()[0])
            except (ValueError, IndexError):
                pass
    return values


def to_float(raw, default=0.0):
    try:
        return float(raw)
    except (TypeError, ValueError):
        return default


def read_csv_runs(paths):
    paths = expand_input_paths(paths)
    runs = []
    for idx, path in enumerate(paths, 1):
        with open(path, newline="") as fh:
            rows = list(csv.DictReader(fh))

        normalized = []
        for row in rows:
            # Map flare_live_tuner.py CsvEmitter output
            zone = row.get("BUF") or row.get("zone")
            if zone == "MID":
                zone = "NEUTRAL"
            norm = {
                "_run": idx,
                "_path": path,
                "ts_ms": row.get("wall_ts") or row.get("ts_ms"),
                "est_mm_min": row.get("EST") or row.get("est_mm_min"),
                "mm_rate": row.get("MM") or row.get("mm_rate") or row.get("sync_mm_min"),
                "v_fil": row.get("v_fil") or row.get("v_fil"),
                "bp_mm": row.get("BP") or row.get("bp_mm"),
                "bpv_frac": row.get("BPV") or row.get("bpv"),
                "rt_mm": row.get("RT") or row.get("rt_mm"),
                "zone": zone,
                "feature": row.get("feature") or row.get("feature", ""),
                "sigma_mm": row.get("sigma_mm"),
                "nc": row.get("nc") or row.get("nc", "0"),
            }
            if norm["ts_ms"] and "." in norm["ts_ms"] and "wall_ts" in row:
                # Convert wall_ts (seconds) to ms for older logic compatibility
                try:
                    norm["ts_ms"] = float(norm["ts_ms"]) * 1000.0
                except ValueError:
                    pass
            normalized.append(norm)
        runs.append({"path": path, "rows": normalized})
    rows = [row for run in runs for row in run["rows"]]
    return runs, rows


def load_state(path):
    if not path:
        return {}
    with open(path) as fh:
        data = json.load(fh)
    if migrate_state_data is not None:
        data = migrate_state_data(data)
    schema = data.get("_schema")
    if schema not in (1, 2, 3, 4):
        raise ValueError(f"unsupported state schema {schema!r}")
    buckets = {}
    for machine, machine_buckets in data.items():
        if machine.startswith("_") or not isinstance(machine_buckets, dict):
            continue
        for label, raw in machine_buckets.items():
            if label.startswith("_"):
                continue
            buckets[label] = raw
    return buckets


def locked_bucket_labels(state_buckets):
    return {
        label for label, raw in state_buckets.items()
        if raw.get("locked") or raw.get("state") == "LOCKED"
    }


def bucket_noise_ratio(raw):
    sigma2 = max(to_float(raw.get("resid_var_ewma"), V_NOISE_FLOOR), V_NOISE_FLOOR)
    x = max(to_float(raw.get("x"), MIN_LEARN_EST_SPS), MIN_LEARN_EST_SPS)
    return math.sqrt(sigma2) / x


def force_qualifying_labels(state_buckets, include_stale=False):
    import time
    now_ts = time.time()
    stale_cutoff = now_ts - 60 * 86400
    labels = set()
    for label, raw in state_buckets.items():
        if label.startswith("_"):
            continue
        if not include_stale and to_float(raw.get("last_seen"), now_ts) < stale_cutoff:
            continue
        if int(raw.get("n", 0)) < 50:
            continue
        neutral_s = to_float(raw.get("cumulative_neutral_s"), 10.0)
        if neutral_s < 10.0:
            continue
        if bucket_noise_ratio(raw) > NOISE_RATIO_THR:
            continue
        labels.add(label)
    return labels


def neutral_rows(rows):
    return [
        r for r in rows
        if r.get("zone") == "NEUTRAL"
        and to_float(r.get("est_sps")) > 0.0
        and to_float(r.get("v_fil")) > 0.0
    ]


def buffer_position_sigma_mm(rows):
    bp = [to_float(r.get("bp_mm"), None) for r in rows if r.get("bp_mm") not in ("", None)]
    bp = [v for v in bp if v is not None]
    if len(bp) < 50:
        return None
    return stats.stdev(bp)


def run_duration_s(run):
    ts = [to_float(r.get("ts_ms"), -1.0) for r in run["rows"]]
    ts = [v for v in ts if v >= 0.0]
    if len(ts) < 2:
        return 0.0
    return max(ts) / 1000.0 - min(ts) / 1000.0


def confidence(n, high, medium=None):
    if medium is None:
        medium = max(1, high // 2)
    if n >= high:
        return "HIGH"
    if n >= medium:
        return "MEDIUM"
    if n > 0:
        return "LOW"
    return "DEFAULT"


def confidence_from_buckets(qualifying, locked_only):
    n = len(qualifying)
    if locked_only and n >= 5:
        return "HIGH"
    if locked_only and n >= 2:
        return "MEDIUM"
    if not locked_only and n >= 5:
        return "MEDIUM"
    if n > 0:
        return "LOW"
    return "DEFAULT"


def current_recommendations(current, detail="current"):
    return {
        key: (current[key], "DEFAULT", detail)
        for key in DEFAULTS
    }


def force_low_confidence(recommendations):
    lowered = {}
    for key, (value, conf, detail) in recommendations.items():
        lowered[key] = (value, "LOW" if conf != "DEFAULT" else conf, detail)
    return lowered


def trimmed_labels_by_x(labels, state_buckets):
    labels = [label for label in labels if label in state_buckets]
    if len(labels) < 3:
        return set(labels)
    xs = [to_float(state_buckets[label].get("x")) for label in labels]
    lo = percentile(xs, 5)
    hi = percentile(xs, 95)
    return {
        label for label in labels
        if lo <= to_float(state_buckets[label].get("x")) <= hi
    }


def bucket_weight(raw):
    return max(0.0, to_float(raw.get("n"))) / max(to_float(raw.get("resid_var_ewma"), V_NOISE_FLOOR), V_NOISE_FLOOR)


def weighted_mean(values):
    total_w = sum(w for _value, w in values)
    if total_w <= 0.0:
        return 0.0
    return sum(value * w for value, w in values) / total_w


def read_flow_schedule_cap(path):
    if not path or not os.path.exists(path):
        return DEFAULT_FLOW_SCHEDULE_CAP
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line or ":" not in line:
                continue
            key, raw = line.split(":", 1)
            if key.strip() != "flow_schedule_cap":
                continue
            try:
                return int(float(raw.strip().split()[0]))
            except (ValueError, IndexError):
                return DEFAULT_FLOW_SCHEDULE_CAP
    return DEFAULT_FLOW_SCHEDULE_CAP


def clamp_flow_schedule_cap(cap):
    cap = int(cap)
    if cap < 1:
        raise ValueError("flow_schedule_cap must be >= 1")
    if cap > 16:
        raise ValueError("flow_schedule_cap must be <= 16")
    return cap


def deterministic_profile_params(rows):
    mids = neutral_rows(rows)
    grouped = defaultdict(list)
    for r in mids:
        grouped[bucket_label(r.get("feature", ""), to_float(r.get("v_fil")))].append(r)

    if not grouped:
        return None

    weighted_x = []
    weighted_bias = []
    for label in sorted(grouped.keys()):
        bucket_rows = grouped[label]
        n = len(bucket_rows)
        if n == 0:
            continue
        ests = [to_float(r.get("est_sps")) for r in bucket_rows]
        bp_delta = [to_float(r.get("bp_mm")) - to_float(r.get("rt_mm")) for r in bucket_rows]
        if ests:
            weighted_x.append((median(ests), float(n)))
        if bp_delta:
            weighted_bias.append((
                clamp(0.4 + median(bp_delta) / 7.8, BIAS_SAFE_MIN, BIAS_SAFE_MAX),
                float(n),
            ))

    # Intentional: Python 3 round() uses banker's rounding. Keep that rule for
    # deterministic reducers so repeated analysis stays byte-identical.
    baseline = round(weighted_mean(weighted_x)) if weighted_x else 1600.0
    bias = clamp(weighted_mean(weighted_bias), BIAS_SAFE_MIN, BIAS_SAFE_MAX) if weighted_bias else 0.4
    return int(baseline), bias


def interpolate_float(a, b, x, x0, x1):
    span = x1 - x0
    if span <= 0:
        return float(a)
    return float(a) + (float(b - a) * float(x - x0) / float(span))


def reduce_flow_schedule_points(points, cap):
    if cap <= 1:
        return points[:1]
    reduced = list(points)
    while len(reduced) > cap:
        ranked = []
        for idx in range(1, len(reduced) - 1):
            prev_flow, prev_base, prev_bias = reduced[idx - 1]
            flow, baseline, bias = reduced[idx]
            next_flow, next_base, next_bias = reduced[idx + 1]
            pred_base = interpolate_float(prev_base, next_base, flow, prev_flow, next_flow)
            pred_bias = interpolate_float(prev_bias, next_bias, flow, prev_flow, next_flow)
            curvature = abs(float(baseline) - pred_base) + abs(float(bias) - pred_bias)
            ranked.append((curvature, flow, idx))
        if not ranked:
            break
        _curvature, _flow, drop_idx = min(ranked)
        del reduced[drop_idx]
    return reduced


def deterministic_flow_schedule(rows, cap):
    scalar = deterministic_profile_params(rows)
    if scalar is None:
        return None

    scalar_baseline, scalar_bias = scalar
    scalar_point = (
        int(scalar_baseline),
        int(scalar_baseline),
        int(round(clamp(scalar_bias, BIAS_SAFE_MIN, BIAS_SAFE_MAX) * 1000.0)),
    )
    if cap <= 1:
        return [scalar_point]

    by_flow = defaultdict(list)
    for r in neutral_rows(rows):
        by_flow[bin_v_fil(to_float(r.get("v_fil")))].append(r)

    points = []
    for flow in sorted(by_flow.keys()):
        flow_rows = by_flow[flow]
        if len(flow_rows) < MIN_RUN_BUCKET_ROWS:
            continue

        by_bucket = defaultdict(list)
        for r in flow_rows:
            by_bucket[bucket_label(r.get("feature", ""), to_float(r.get("v_fil")))].append(r)

        weighted_x = []
        weighted_bias = []
        for label in sorted(by_bucket.keys()):
            bucket_rows = by_bucket[label]
            n = len(bucket_rows)
            if n == 0:
                continue
            ests = [to_float(r.get("est_sps")) for r in bucket_rows]
            bp_delta = [to_float(r.get("bp_mm")) - to_float(r.get("rt_mm")) for r in bucket_rows]
            if ests:
                weighted_x.append((median(ests), float(n)))
            if bp_delta:
                weighted_bias.append((
                    clamp(0.4 + median(bp_delta) / 7.8, BIAS_SAFE_MIN, BIAS_SAFE_MAX),
                    float(n),
                ))
        if not weighted_x:
            continue
        baseline = int(round(weighted_mean(weighted_x)))
        if weighted_bias:
            bias = clamp(weighted_mean(weighted_bias), BIAS_SAFE_MIN, BIAS_SAFE_MAX)
            bias_milli = int(round(bias * 1000.0))
        else:
            bias_milli = scalar_point[2]
        points.append((int(flow), baseline, bias_milli))

    if len(points) < 2:
        return [scalar_point]
    return reduce_flow_schedule_points(points, cap)


def write_flow_schedule(path, cap, points):
    with open(path, "w") as fh:
        fh.write("# flare_analyze.py emitted flow schedule\n")
        fh.write("# Each point: flow_sps, baseline_sps, compression_bias_frac\n")
        fh.write(f"flow_schedule_cap: {cap}\n\n")
        fh.write("[flow_schedule.v1]\n")
        for idx, (flow_sps, baseline_sps, bias_milli) in enumerate(points):
            fh.write(f"point{idx}: {flow_sps}, {baseline_sps}, {bias_milli / 1000.0:.3f}\n")


def contributor_entries(state_buckets, force=False, include_stale=False):
    locked = locked_bucket_labels(state_buckets)
    labels = locked if locked else (force_qualifying_labels(state_buckets, include_stale=include_stale) if force else set())
    labels = trimmed_labels_by_x(labels, state_buckets)
    raw_entries = []
    for label in labels:
        raw = state_buckets.get(label, {})
        w = bucket_weight(raw)
        if w <= 0.0:
            continue
        raw_entries.append({
            "label": label,
            "n": int(raw.get("n", 0)),
            "x": to_float(raw.get("x")),
            "ratio": bucket_noise_ratio(raw),
            "weight": w,
        })
    total_w = sum(entry["weight"] for entry in raw_entries)
    if total_w > 0.0:
        for entry in raw_entries:
            entry["norm_weight"] = entry["weight"] / total_w
    return sorted(raw_entries, key=lambda e: e["weight"], reverse=True)


def qualifying_labels(state_buckets, force=False, include_stale=False):
    locked = locked_bucket_labels(state_buckets)
    if locked:
        return locked
    if force:
        return force_qualifying_labels(state_buckets, include_stale=include_stale)
    return set()


def contributor_mass(state_buckets, labels):
    total_n = 0
    contributor_n = 0
    for label, raw in state_buckets.items():
        if label.startswith("_") or not isinstance(raw, dict):
            continue
        n = int(raw.get("n", 0))
        if n < DENOMINATOR_MIN_BUCKET_N:
            continue
        total_n += n
        if label in labels:
            contributor_n += n
    return (contributor_n / total_n) if total_n > 0 else 0.0


def recommend_for_subset(runs_subset, rows_subset, state_buckets, current, mode, force, include_stale):
    runs = runs_subset
    rows = rows_subset
    mids = neutral_rows(rows)

    import time
    now_ts = time.time()
    stale_cutoff = now_ts - 60 * 86400
    locked = locked_bucket_labels(state_buckets)
    if locked:
        qualifying = locked
        locked_only = True
    elif force:
        qualifying = force_qualifying_labels(state_buckets, include_stale=include_stale)
        locked_only = False
    else:
        qualifying = set()
        locked_only = False

    by_bucket = defaultdict(list)
    for r in mids:
        label = bucket_label(r.get("feature", ""), to_float(r.get("v_fil")))
        if state_buckets and label in state_buckets:
            last_seen = state_buckets[label].get("last_seen", now_ts)
            if last_seen < stale_cutoff and not include_stale:
                continue
        if qualifying and label not in qualifying:
            continue
        by_bucket[label].append(r)

    trimmed_qualifying = trimmed_labels_by_x(qualifying, state_buckets) if qualifying else set()
    weighted_x = [
        (to_float(state_buckets[label].get("x")), bucket_weight(state_buckets[label]))
        for label in trimmed_qualifying
        if bucket_weight(state_buckets[label]) > 0.0
    ]
    if weighted_x:
        baseline = weighted_mean(weighted_x)
        est_sigma = math.sqrt(1.0 / max(sum(w for _value, w in weighted_x), 1e-9))
        baseline_conf = confidence_from_buckets(trimmed_qualifying, locked_only)
        baseline_detail = f"{len(trimmed_qualifying)} buckets"
    else:
        dominant = max(by_bucket.values(), key=len) if by_bucket else []
        est_vals = [to_float(r.get("est_sps")) for r in dominant]
        est_p50 = median(est_vals)
        est_sigma = stdev(est_vals)
        baseline = est_p50 if est_vals else current["baseline_rate"]
        baseline_conf = confidence_from_buckets(set(by_bucket), locked_only) if est_vals else "DEFAULT"
        baseline_detail = f"n={len(est_vals)}, sigma={est_sigma:.1f}"

    qualifying_rows = [r for rows_for_bucket in by_bucket.values() for r in rows_for_bucket]
    weighted_bias = []
    if trimmed_qualifying:
        for label in trimmed_qualifying:
            deltas = [
                to_float(r.get("bp_mm")) - to_float(r.get("rt_mm"))
                for r in by_bucket.get(label, [])
            ]
            if not deltas:
                continue
            weighted_bias.append((
                clamp(0.4 + median(deltas) / 7.8, BIAS_SAFE_MIN, BIAS_SAFE_MAX),
                bucket_weight(state_buckets[label]),
            ))
    if weighted_bias:
        bias = clamp(weighted_mean(weighted_bias), BIAS_SAFE_MIN, BIAS_SAFE_MAX)
        bias_conf = confidence_from_buckets(trimmed_qualifying, locked_only)
        bias_detail = f"{len(weighted_bias)} buckets"
    else:
        bp_delta = [to_float(r.get("bp_mm")) - to_float(r.get("rt_mm")) for r in qualifying_rows]
        current_bias = current["sync_compression_bias_frac"]
        bias = clamp(current_bias + (stats.mean(bp_delta) / 7.8 if bp_delta else 0.0), BIAS_SAFE_MIN, BIAS_SAFE_MAX)
        bias_conf = confidence_from_buckets(set(by_bucket), locked_only) if bp_delta else "DEFAULT"
        bias_detail = f"n={len(bp_delta)}"

    dwell_ms = []
    for run in runs:
        segment_start = None
        last_ts = None
        for r in run["rows"]:
            ts = to_float(r.get("ts_ms"), -1.0)
            active = r.get("zone") == "NEUTRAL" and to_float(r.get("est_sps")) > 0.0 and ts >= 0.0
            if active and segment_start is None:
                segment_start = ts
            if not active and segment_start is not None and last_ts is not None:
                dwell_ms.append(max(0.0, last_ts - segment_start))
                segment_start = None
            if ts >= 0.0:
                last_ts = ts
        if segment_start is not None and last_ts is not None:
            dwell_ms.append(max(0.0, last_ts - segment_start))
    neutral_timeout = current["neutral_creep_timeout_ms"]
    neutral_timeout_conf = "DEFAULT"

    creep_slopes = []
    creep_caps = []
    for run in runs:
        prev = None
        segment_start_est = None
        for r in run["rows"]:
            ts = to_float(r.get("ts_ms"), -1.0) / 1000.0
            est = to_float(r.get("est_sps"))
            creeping = r.get("zone") == "NEUTRAL" and to_float(r.get("nc")) > 0.0 and ts >= 0.0 and est > 0.0
            if not creeping:
                prev = None
                segment_start_est = None
                continue
            if segment_start_est is None:
                segment_start_est = est
            else:
                creep_caps.append((est / max(segment_start_est, 1.0)) * 100.0)
            if prev:
                dt = ts - prev[0]
                if dt > 0.0:
                    creep_slopes.append((est - prev[1]) / dt)
            prev = (ts, est)
    creep_rate = median(creep_slopes) if len(creep_slopes) >= 20 else current["neutral_creep_rate_sps_per_s"]
    creep_rate_conf = confidence(len(creep_slopes), 20, 10) if creep_slopes else "DEFAULT"
    creep_cap = percentile(creep_caps, 90) if len(creep_caps) >= 20 else current["neutral_creep_cap_frac"]
    creep_cap_conf = confidence(len(creep_caps), 20, 10) if creep_caps else "DEFAULT"

    sigma_vals = []
    for bucket_rows in by_bucket.values():
        sigma = buffer_position_sigma_mm(bucket_rows)
        if sigma is not None:
            sigma_vals.append(sigma)
    sigma_p95 = percentile(sigma_vals, 95)
    if len(sigma_vals) >= 5:
        clamped_sigma = clamp(sigma_p95, 0.1, 5.0)
        var_blend = 0.5 if clamped_sigma <= current["buf_variance_blend_ref_mm"] else 0.3
        var_conf = confidence_from_buckets(trimmed_qualifying or set(by_bucket), locked_only)
        var_ref = clamp(round(clamped_sigma / 0.5) * 0.5, 0.1, 5.0)
        ref_conf = var_conf
        sigma_detail = f"bp sigma p95 {sigma_p95:.2f}"
    else:
        var_blend = current["buf_variance_blend_frac"]
        var_conf = "DEFAULT"
        var_ref = current["buf_variance_blend_ref_mm"]
        ref_conf = "DEFAULT"
        sigma_detail = f"insufficient bp sigma buckets {len(sigma_vals)}/5"

    return {
        "baseline_rate": (baseline, baseline_conf, baseline_detail),
        "sync_compression_bias_frac": (bias, bias_conf, bias_detail),
        "neutral_creep_timeout_ms": (neutral_timeout, neutral_timeout_conf, f"deferred; {len(dwell_ms)} dwells"),
        "neutral_creep_rate_sps_per_s": (creep_rate, creep_rate_conf, f"{len(creep_slopes)} creep slopes"),
        "neutral_creep_cap_frac": (creep_cap, creep_cap_conf, f"{len(creep_caps)} creep ratios"),
        "buf_variance_blend_frac": (var_blend, var_conf, sigma_detail),
        "buf_variance_blend_ref_mm": (var_ref, ref_conf, sigma_detail),
    }


def relay_duty_recommendations(runs, current):
    """Compute type-D relay duty recommendations from status CSV rows."""
    estimates = []
    fill_rates = []

    for run in runs:
        phase = None
        travel = 0.0
        weighted_rate = 0.0
        weight_ms = 0.0
        last_fill = None
        last_relieve = None
        prev_ts = None
        prev_rate = 0.0
        prev_zone = None

        def finish_phase():
            nonlocal phase, travel, weighted_rate, weight_ms, last_fill, last_relieve
            if phase is None or weight_ms <= 0.0:
                return
            rec = {
                "travel": travel,
                "rate": weighted_rate / weight_ms,
            }
            if phase == "fill":
                last_fill = rec
                fill_rates.append(rec["rate"])
            elif phase == "relieve":
                last_relieve = rec
            if last_fill and last_relieve:
                dl = last_fill["travel"]
                dh = last_relieve["travel"]
                total = dl + dh
                if total > 0.001:
                    fh = clamp(dh / total, 0.0, 1.0)
                    estimates.append((1.0 - fh) * last_relieve["rate"] + fh * last_fill["rate"])
            travel = 0.0
            weighted_rate = 0.0
            weight_ms = 0.0

        for row in run["rows"]:
            ts = to_float(row.get("ts_ms"), -1.0)
            zone = row.get("zone")
            rate = to_float(row.get("est_mm_min"), math.nan)
            if ts < 0.0 or math.isnan(rate):
                if ts >= 0.0:
                    prev_ts = ts
                prev_zone = zone
                continue
            if prev_ts is not None and prev_ts >= 0.0 and phase is not None:
                dt = max(0.0, ts - prev_ts)
                travel += abs(prev_rate) * dt / 60000.0
                weighted_rate += abs(prev_rate) * dt
                weight_ms += dt
            if zone != prev_zone:
                if zone == "COMPRESSION":
                    finish_phase()
                    phase = "relieve"
                elif zone == "TENSION":
                    finish_phase()
                    phase = "fill"
            prev_ts = ts
            prev_rate = rate
            prev_zone = zone

    if not estimates:
        return {}

    conf = confidence(len(estimates), 8, 3)
    detail = f"{len(estimates)} paired relay cycles"
    lo = max(1.0, percentile(estimates, 10) * 0.90)
    hi = max(lo, percentile(estimates, 90) * 1.10)
    seed = median(estimates)
    relay_base = max(current["baseline_rate"], percentile(fill_rates, 90) if fill_rates else seed)
    return {
        "baseline_rate": (relay_base, conf, detail),
        "relay_estimate_lo": (lo, conf, detail),
        "relay_estimate_hi": (hi, conf, detail),
        "relay_seed_rate": (seed, conf, detail),
    }


# Minimum paired duty cycles per demand bucket for a PASS verdict.
RELAY_COVERAGE_MIN_BUCKET_CYCLES = 4


def relay_duty_coverage(runs, current):
    """Compute per-bucket relay duty coverage and return a PASS/WARN verdict.

    Splits paired duty-cycle estimates into a low-demand and a high-demand
    bucket at the median.  Reports per-bucket sample counts and names each
    deficiency so the user knows exactly what to capture next.

    Returns a dict:
      {
        "verdict": "PASS" | "WARN",
        "reasons": [str, ...],          # empty on PASS
        "low_bucket_n": int,
        "high_bucket_n": int,
        "total_cycles": int,
        "detail": str,
      }
    This is a pure function of the inputs; same CSVs → same dict.
    """
    estimates = []
    fill_rates = []

    for run in runs:
        phase = None
        travel = 0.0
        weighted_rate = 0.0
        weight_ms = 0.0
        last_fill = None
        last_relieve = None
        prev_ts = None
        prev_rate = 0.0
        prev_zone = None

        def _finish(phase=None, travel=0.0, weighted_rate=0.0, weight_ms=0.0,
                    last_fill=None, last_relieve=None):
            return phase, travel, weighted_rate, weight_ms, last_fill, last_relieve

        for row in run["rows"]:
            ts = to_float(row.get("ts_ms"), -1.0)
            zone = row.get("zone")
            rate = to_float(row.get("est_mm_min"), math.nan)
            if ts < 0.0 or math.isnan(rate):
                if ts >= 0.0:
                    prev_ts = ts
                prev_zone = zone
                continue
            if prev_ts is not None and prev_ts >= 0.0 and phase is not None:
                dt = max(0.0, ts - prev_ts)
                travel += abs(prev_rate) * dt / 60000.0
                weighted_rate += abs(prev_rate) * dt
                weight_ms += dt
            if zone != prev_zone:
                if zone in ("COMPRESSION", "TENSION"):
                    # finish old phase
                    if phase is not None and weight_ms > 0.0:
                        rec = {"travel": travel, "rate": weighted_rate / weight_ms}
                        if phase == "fill":
                            last_fill = rec
                            fill_rates.append(rec["rate"])
                        elif phase == "relieve":
                            last_relieve = rec
                        if last_fill and last_relieve:
                            dl = last_fill["travel"]
                            dh = last_relieve["travel"]
                            total = dl + dh
                            if total > 0.001:
                                fh = clamp(dh / total, 0.0, 1.0)
                                estimates.append(
                                    (1.0 - fh) * last_relieve["rate"] + fh * last_fill["rate"]
                                )
                        travel = 0.0
                        weighted_rate = 0.0
                        weight_ms = 0.0
                    phase = "relieve" if zone == "COMPRESSION" else "fill"
            prev_ts = ts
            prev_rate = rate
            prev_zone = zone

    total = len(estimates)
    if total == 0:
        return {
            "verdict": "WARN",
            "reasons": ["no paired relay cycles found; print with type-D relay active"],
            "low_bucket_n": 0,
            "high_bucket_n": 0,
            "total_cycles": 0,
            "detail": "0 paired relay cycles",
        }

    mid = median(estimates)
    low_bucket = [e for e in estimates if e <= mid]
    high_bucket = [e for e in estimates if e > mid]
    low_n = len(low_bucket)
    high_n = len(high_bucket)

    reasons = []
    min_n = RELAY_COVERAGE_MIN_BUCKET_CYCLES
    if low_n < min_n:
        reasons.append(
            f"low-demand bucket under-sampled ({low_n}/{min_n} cycles) "
            "→ print slower or with more low-speed features"
        )
    if high_n < min_n:
        reasons.append(
            f"high-demand bucket under-sampled ({high_n}/{min_n} cycles) "
            "→ print faster or with more high-speed features (taller object preferred)"
        )

    # Detect no-spread: all estimates within 5 % of the median (no constant-baseline segments).
    spread = (max(estimates) - min(estimates)) / max(mid, 1.0) if total >= 2 else 0.0
    if spread < 0.05 and total >= 2:
        reasons.append(
            "no constant-baseline segment detected "
            "(all estimates within 5 % of median) "
            "→ use a single purpose-built model with sustained slow and fast sections"
        )

    verdict = "PASS" if not reasons else "WARN"
    detail = (
        f"{total} paired relay cycles, "
        f"low_bucket={low_n}, high_bucket={high_n}, "
        f"spread={spread * 100:.1f}%"
    )
    return {
        "verdict": verdict,
        "reasons": reasons,
        "low_bucket_n": low_n,
        "high_bucket_n": high_n,
        "total_cycles": total,
        "detail": detail,
    }


def compute_recommendations(rows, runs, state_buckets, current, mode, include_stale=False, force=False):
    recommendations = recommend_for_subset(runs, rows, state_buckets, current, mode, force, include_stale)
    relay = relay_duty_recommendations(runs, current)
    if relay:
        recommendations.update(relay)
    return recommendations


def raw_consistency_by_run(runs):
    baseline_vals = defaultdict(list)
    bias_vals = defaultdict(list)
    for run in runs:
        grouped = defaultdict(list)
        for r in neutral_rows(run["rows"]):
            grouped[bucket_label(r.get("feature", ""), to_float(r.get("v_fil")))].append(r)
        for label, bucket_rows in grouped.items():
            ests = [to_float(r.get("est_sps")) for r in bucket_rows]
            bp_delta = [to_float(r.get("bp_mm")) - to_float(r.get("rt_mm")) for r in bucket_rows]
            if ests:
                baseline_vals[label].append(median(ests))
            if bp_delta:
                bias_vals[label].append(clamp(0.4 + stats.mean(bp_delta) / 7.8, BIAS_SAFE_MIN, BIAS_SAFE_MAX))
    max_baseline_delta = max((max(vs) - min(vs) for vs in baseline_vals.values() if len(vs) >= 2), default=0.0)
    max_bias_delta = max((max(vs) - min(vs) for vs in bias_vals.values() if len(vs) >= 2), default=0.0)
    return max_baseline_delta, max_bias_delta


def consistency_by_run(runs, state_buckets, current, mode="safe", force=False, include_stale=False):
    report = consistency_report_by_run(
        runs,
        state_buckets,
        current,
        mode=mode,
        force=force,
        include_stale=include_stale,
    )
    return report["max_baseline_delta"], report["max_bias_delta"]


def classify_run(run, state_buckets, current, mode, force, include_stale):
    recs = recommend_for_subset(
        [run],
        run["rows"],
        state_buckets,
        current,
        mode,
        force,
        include_stale,
    )
    baseline, baseline_conf, _baseline_detail = recs["baseline_rate"]
    bias, bias_conf, _bias_detail = recs["sync_compression_bias_frac"]
    entries = contributor_entries(state_buckets, force=force, include_stale=include_stale)
    contributor_labels = {entry["label"] for entry in entries}
    row_counts = defaultdict(int)
    for row in neutral_rows(run["rows"]):
        label = bucket_label(row.get("feature", ""), to_float(row.get("v_fil")))
        if label in contributor_labels:
            row_counts[label] += 1
    max_bucket_rows = max(row_counts.values(), default=0)

    confidence_rank = {"DEFAULT": 0, "LOW": 1, "MEDIUM": 2, "HIGH": 3}
    confidence = baseline_conf
    if confidence_rank.get(bias_conf, 0) < confidence_rank.get(confidence, 0):
        confidence = bias_conf

    comparable = True
    reason = "ok"
    if confidence not in ("HIGH", "MEDIUM"):
        comparable = False
        reason = f"{confidence} confidence ({len(entries)} contributing buckets)"
    elif len(entries) < MIN_COMPARABLE_BUCKETS:
        comparable = False
        reason = f"contributors {len(entries)} < {MIN_COMPARABLE_BUCKETS}"
    elif max_bucket_rows < MIN_RUN_BUCKET_ROWS:
        comparable = False
        reason = f"<{MIN_RUN_BUCKET_ROWS} rows per contributing bucket"

    return {
        "path": run.get("path", ""),
        "comparable": comparable,
        "reason": reason,
        "baseline": baseline,
        "bias": bias,
        "contributors": len(entries),
        "confidence": confidence,
        "max_bucket_rows": max_bucket_rows,
    }


def consistency_report_by_run(runs, state_buckets, current, mode="safe", force=False, include_stale=False):
    per_run = [
        classify_run(run, state_buckets, current, mode, force, include_stale)
        for run in runs
    ]
    comparable = [entry for entry in per_run if entry["comparable"]]
    baseline_vals = [entry["baseline"] for entry in comparable]
    bias_vals = [entry["bias"] for entry in comparable]
    skipped = [entry for entry in per_run if not entry["comparable"]]
    consistency_skipped = len(comparable) < 2
    if consistency_skipped:
        max_baseline_delta = 0.0
        max_bias_delta = 0.0
    else:
        max_baseline_delta = max(baseline_vals) - min(baseline_vals)
        max_bias_delta = max(bias_vals) - min(bias_vals)
    return {
        "per_run": per_run,
        "comparable_runs": len(comparable),
        "skipped_runs": skipped,
        "consistency_skipped": consistency_skipped,
        "max_baseline_delta": max_baseline_delta,
        "max_bias_delta": max_bias_delta,
    }


def legacy_consistency_by_run(runs, state_buckets, current, mode="safe", force=False, include_stale=False):
    baseline_vals = []
    bias_vals = []
    for run in runs:
        recs = recommend_for_subset(
            [run],
            run["rows"],
            state_buckets,
            current,
            mode,
            force,
            include_stale,
        )
        baseline, baseline_conf, _baseline_detail = recs["baseline_rate"]
        bias, bias_conf, _bias_detail = recs["sync_compression_bias_frac"]
        if baseline_conf != "DEFAULT":
            baseline_vals.append(baseline)
        if bias_conf != "DEFAULT":
            bias_vals.append(bias)
    max_baseline_delta = max(baseline_vals) - min(baseline_vals) if len(baseline_vals) >= 2 else 0.0
    max_bias_delta = max(bias_vals) - min(bias_vals) if len(bias_vals) >= 2 else 0.0
    return max_baseline_delta, max_bias_delta


def acceptance_gate(rows, runs, state_buckets, current, mode="safe", force=False, include_stale=False):
    reasons = []
    warnings = []
    mids = neutral_rows(rows)
    locked = locked_bucket_labels(state_buckets)
    qualifying = qualifying_labels(state_buckets, force=force, include_stale=include_stale)
    locked_neutral = [
        r for r in mids
        if bucket_label(r.get("feature", ""), to_float(r.get("v_fil"))) in locked
    ]
    raw_coverage = (len(locked_neutral) / len(mids)) if mids else 0.0
    mass = contributor_mass(state_buckets, qualifying)
    if mass < CONTRIBUTOR_MASS_PASS:
        reasons.append(f"contributor mass {mass * 100:.1f}% < {CONTRIBUTOR_MASS_PASS * 100:.1f}%")
    elif mass < CONTRIBUTOR_MASS_WARN:
        warnings.append(f"contributor mass {mass * 100:.1f}% < {CONTRIBUTOR_MASS_WARN * 100:.1f}%")
    if raw_coverage < RAW_COVERAGE_WARN:
        warnings.append(f"raw NEUTRAL coverage {raw_coverage * 100:.1f}% < {RAW_COVERAGE_WARN * 100:.1f}%")

    consistency = consistency_report_by_run(
        runs,
        state_buckets,
        current,
        mode=mode,
        force=force,
        include_stale=include_stale,
    )
    base_delta = consistency["max_baseline_delta"]
    bias_delta = consistency["max_bias_delta"]
    if base_delta > 50.0:
        reasons.append(f"baseline consistency delta {base_delta:.0f} sps > 50")
    if bias_delta > 0.05:
        reasons.append(f"bias consistency delta {bias_delta:.3f} > 0.050")

    by_qualifying_bucket = defaultdict(list)
    for row in mids:
        label = bucket_label(row.get("feature", ""), to_float(row.get("v_fil")))
        if label in qualifying:
            by_qualifying_bucket[label].append(row)
    sigma_vals = [
        sigma for bucket_rows in by_qualifying_bucket.values()
        for sigma in [buffer_position_sigma_mm(bucket_rows)]
        if sigma is not None
    ]
    sigma_p95 = percentile(sigma_vals, 95)
    # FAIL: hardware-level scatter suggesting mechanical failure.
    if sigma_p95 >= SIGMA_HARDWARE_CEILING_MM:
        reasons.append(f"sigma p95 hardware failure {sigma_p95:.2f} >= {SIGMA_HARDWARE_CEILING_MM:.2f} mm")
    # WARN: config is stale relative to current actual scatter (fixable by patch).
    elif sigma_p95 >= current["buf_variance_blend_ref_mm"]:
        warnings.append(f"config stale: actual sigma p95 {sigma_p95:.2f} >= current ref {current['buf_variance_blend_ref_mm']:.2f}")

    # FAIL (a): Recommendation Unreliable if we don't have enough comparable data to reduce.
    if consistency["comparable_runs"] < 2:
        reasons.append(f"comparable run count {consistency['comparable_runs']} < 2 (insufficient consistent data)")

    # WARN (b): Config Stale / Process Immature if we haven't reached the "golden" soak targets.
    if len(runs) < 3:
        warnings.append(f"soak immature: run count {len(runs)} < 3")
    durations = [run_duration_s(run) for run in runs]
    if not (durations and all(d >= 600.0 for d in durations) or sum(durations) >= 1800.0):
        warnings.append(f"soak immature: duration total {sum(durations) / 60.0:.1f} min < 30 min and at least one run < 10 min")
    if len(locked) < 3:
        warnings.append(f"soak immature: locked bucket count {len(locked)} < 3")

    telemetry = {
        "FAULT_HOLD": 0,
        "TENSION_RISK_HIGH": 0,
        "EST_FALLBACK": 0,
    }
    if telemetry["FAULT_HOLD"] != 0:
        reasons.append("FAULT_HOLD count > 0")
    if telemetry["TENSION_RISK_HIGH"] > 5 * max(1, len(runs)):
        reasons.append("TENSION_RISK_HIGH count > 5 per run")
    if telemetry["EST_FALLBACK"] != 0:
        reasons.append("EST_FALLBACK count > 0")

    return {
        "pass": not reasons,
        "reasons": reasons,
        "warnings": warnings,
        "coverage": raw_coverage,
        "raw_coverage": raw_coverage,
        "contributor_mass": mass,
        "max_baseline_delta": base_delta,
        "max_bias_delta": bias_delta,
        "sigma_p95": sigma_p95,
        "telemetry": telemetry,
        "per_run_estimates": consistency["per_run"],
        "comparable_runs": consistency["comparable_runs"],
        "skipped_runs": consistency["skipped_runs"],
        "consistency_skipped": consistency["consistency_skipped"],
    }


def format_value(key, value):
    if key in (
        "sync_compression_bias_frac",
        "buf_variance_blend_frac",
        "buf_variance_blend_ref_mm",
        "relay_catchup_frac",
        "relay_neutral_frac",
    ):
        return f"{value:.3f}"
    return f"{int(round(value))}"


def write_patch(path, runs, rows, state_buckets, current, recommendations, gate, banner="", contributors=None, relay_coverage=None):
    locked = locked_bucket_labels(state_buckets)
    rejected = gate and not gate["pass"]
    with open(path, "w") as fh:
        if banner:
            fh.write(f"{banner}\n")
        fh.write("# flare_analyze.py emitted patch\n")
        fh.write(f"# Source: {len(runs)} runs, {len(rows)} samples, {len(locked)} LOCKED buckets\n")
        if gate:
            fh.write(f"# Acceptance gate: {'PASS' if gate['pass'] else 'FAIL'}\n")
            fh.write(
                f"# Coverage: contributor mass {gate.get('contributor_mass', 0.0) * 100:.1f} %, "
                f"raw NEUTRAL coverage {gate.get('raw_coverage', gate['coverage']) * 100:.1f} %\n"
            )
            fh.write(
                f"# Consistency: max baseline delta {gate['max_baseline_delta']:.0f} sps, "
                f"max bias delta {gate['max_bias_delta']:.3f}\n"
            )
            fh.write("# Per-run estimates used in consistency check:\n")
            for idx, entry in enumerate(gate.get("per_run_estimates", []), 1):
                status = "comparable" if entry.get("comparable") else f"skipped: {entry.get('reason', '')}"
                fh.write(
                    f"#   run {idx} ({entry.get('path', '')}): "
                    f"baseline={entry.get('baseline', 0.0):.0f}, "
                    f"bias={entry.get('bias', 0.0):.3f}, "
                    f"contributors={entry.get('contributors', 0)}, "
                    f"conf={entry.get('confidence', 'DEFAULT')}, {status}\n"
                )
            fh.write(
                f"# Comparable runs: {gate.get('comparable_runs', 0)} "
                f"of {len(gate.get('per_run_estimates', []))}\n"
            )
            skipped = gate.get("skipped_runs", [])
            fh.write("# Skipped runs:\n")
            if skipped:
                for entry in skipped:
                    fh.write(f"#   {entry.get('path', '')}: {entry.get('reason', '')}\n")
            else:
                fh.write("#   (none)\n")
            if gate.get("consistency_skipped"):
                fh.write("# Consistency: skipped (need >= 2 comparable runs)\n")
            tel = gate["telemetry"]
            fh.write("# Telemetry: not currently parsed from logs;\n")
            fh.write("# counters reflect pending feature, not real events\n")
            fh.write(
                "# Telemetry: "
                f"TENSION_RISK_HIGH={tel['TENSION_RISK_HIGH']}, "
                f"EST_FALLBACK={tel['EST_FALLBACK']}, "
                f"FAULT_HOLD={tel['FAULT_HOLD']}\n"
            )
            if gate.get("warnings"):
                fh.write("# Warnings:\n")
                for warning in gate["warnings"]:
                    fh.write(f"# - {warning}\n")
            if gate["reasons"]:
                fh.write("# Failure reasons:\n")
                for reason in gate["reasons"]:
                    fh.write(f"# - {reason}\n")
        else:
            fh.write("# Acceptance gate: NOT RUN\n")
        if relay_coverage is not None:
            fh.write(
                f"# Relay coverage: {relay_coverage['verdict']} "
                f"({relay_coverage['detail']})\n"
            )
            for reason in relay_coverage.get("reasons", []):
                fh.write(f"# - {reason}\n")
        fh.write("# WARNING: do not blindly apply; review against config.ini first.\n\n")
        fh.write("[flare_review]\n")
        fh.write("# Each line: current_value -> suggested_value (confidence)\n")
        for key in recommendations:
            suggested, conf, detail = recommendations[key]
            line_conf = "REJECTED" if rejected else conf
            fh.write(
                f"# {key:<28} {format_value(key, current[key]):>7} -> "
                f"{format_value(key, suggested):<7} ({line_conf}, {detail})\n"
            )
        if contributors:
            fh.write("\n[flare_contributors]\n")
            for key in recommendations:
                _suggested, conf, _detail = recommendations[key]
                if conf == "DEFAULT":
                    continue
                entries = contributors.get(key, [])
                if not entries:
                    continue
                total_n = sum(entry["n"] for entry in entries)
                fh.write(f"# {key:<28} {len(entries)} buckets, total n={total_n}\n")
                for entry in entries[:5]:
                    suffix = " [marginal]" if entry["ratio"] > NOISE_RATIO_THR else ""
                    fh.write(
                        f"#   {entry['label']:<24} n={entry['n']} "
                        f"x={entry['x']:.0f} sigma/x={entry['ratio']:.2f} "
                        f"w={entry.get('norm_weight', 0.0):.2f}{suffix}\n"
                    )
        fh.write("\n")
        fh.write("# To apply, copy reviewed values into config.ini, then run:\n")
        fh.write("#   python3 scripts/gen_config.py\n")
        fh.write("#   ninja -C build_local\n")
        fh.write("#   bash scripts/flash_flare.sh\n")



def run(args):
    # Normalize outputs
    args.out = normalize_output(getattr(args, "out", None))
    args.state = normalize_output(getattr(args, "state", None))
    args.config = normalize_output(getattr(args, "config", None))

    if getattr(args, "emit_baseline", False) or getattr(args, "emit_flow_schedule", False):
        if getattr(args, "emit_baseline", False) and getattr(args, "emit_flow_schedule", False):
            print("Error: choose only one emit mode", file=sys.stderr)
            return 1

        # Resolve inputs
        args.profile_fast = resolve_input(getattr(args, "profile_fast", None))
        args.profile_slow = resolve_input(getattr(args, "profile_slow", None))

        if not getattr(args, "out", None) and getattr(args, "emit_flow_schedule", False):
            print("Error: --emit-flow-schedule requires --out", file=sys.stderr)
            return 1

        fast_runs, fast_rows = read_csv_runs([args.profile_fast])
        slow_runs, slow_rows = read_csv_runs([args.profile_slow])

        all_rows = fast_rows + slow_rows
        scalar = deterministic_profile_params(all_rows)
        if scalar is None:
            print("Error: no NEUTRAL rows found in profiles", file=sys.stderr)
            return 1
        baseline, bias = scalar

        if getattr(args, "emit_flow_schedule", False):
            raw_cap = getattr(args, "flow_schedule_cap", None)
            try:
                cap = clamp_flow_schedule_cap(
                    raw_cap if raw_cap is not None else read_flow_schedule_cap(getattr(args, "config", None))
                )
            except ValueError as exc:
                print(f"Error: {exc}", file=sys.stderr)
                return 1
            points = deterministic_flow_schedule(all_rows, cap)
            if points is None:
                print("Error: no NEUTRAL rows found in profiles", file=sys.stderr)
                return 1
            write_flow_schedule(args.out, cap, points)
            print(f"[*] Wrote deterministic flow schedule to {args.out}")
            return 0

        if args.out:
            with open(args.out, "w") as fh:
                fh.write(f"baseline_rate: {int(baseline)}\n")
                fh.write(f"sync_compression_bias_frac: {bias:.3f}\n")
            print(f"[*] Wrote deterministic baseline to {args.out}")
        else:
            print(f"baseline_rate: {int(baseline)}")
            print(f"sync_compression_bias_frac: {bias:.3f}")
        return 0

    if not getattr(args, "inputs", None):
        print("Error: --in required unless using --emit-baseline or --emit-flow-schedule", file=sys.stderr)
        return 1
    if not getattr(args, "out", None):
        print("Error: --out required unless using --emit-baseline or --emit-flow-schedule", file=sys.stderr)
        return 1

    runs, rows = read_csv_runs(args.inputs)
    if not rows:
        print("Error: no data rows found", file=sys.stderr)
        return 1
    current = read_config(args.config)
    try:
        state_buckets = load_state(args.state) if args.state else {}
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Error: could not read state file: {exc}", file=sys.stderr)
        return 1
    force = bool(getattr(args, "force", False))
    zero_locked_state = bool(args.state) and not locked_bucket_labels(state_buckets)
    banner = ""
    if zero_locked_state and args.mode == "safe" and not force:
        banner = "# REFUSED: no LOCKED buckets in state file"
        recommendations = current_recommendations(current, "current; no LOCKED buckets")
    else:
        recommendations = compute_recommendations(
            rows,
            runs,
            state_buckets,
            current,
            args.mode,
            include_stale=getattr(args, "include_stale", False),
            force=force,
        )
        if zero_locked_state:
            if force:
                banner = "# WARNING: zero LOCKED buckets; --force bypassed locked-bucket floor"
            else:
                banner = "# WARNING: zero LOCKED buckets; suggestions are pre-lock estimates"
            recommendations = force_low_confidence(recommendations)
    gate = acceptance_gate(
        rows,
        runs,
        state_buckets,
        current,
        mode=args.mode,
        force=force,
        include_stale=getattr(args, "include_stale", False),
    ) if args.acceptance_gate else None
    entries = contributor_entries(state_buckets, force=force, include_stale=getattr(args, "include_stale", False))
    contributors = {
        key: entries
        for key, (_value, conf, _detail) in recommendations.items()
        if key in DEFAULTS and conf != "DEFAULT" and entries
    }
    # Only emit relay coverage when relay recommendations were computed.
    relay_has_recs = "relay_estimate_lo" in recommendations or "relay_seed_rate" in recommendations
    relay_cov = relay_duty_coverage(runs, current) if relay_has_recs else None
    write_patch(args.out, runs, rows, state_buckets, current, recommendations, gate, banner=banner, contributors=contributors, relay_coverage=relay_cov)
    if zero_locked_state and args.mode == "safe" and not force:
        print("refused: no LOCKED buckets in state file", file=sys.stderr)
        print(f"[*] Wrote refused review patch to {args.out}", file=sys.stderr)
        return 2
    if zero_locked_state:
        print("warning: zero LOCKED buckets; suggestions are pre-lock estimates", file=sys.stderr)
    if gate and not gate["pass"]:
        for reason in gate["reasons"]:
            print(f"acceptance gate failed: {reason}", file=sys.stderr)
        print(f"[*] Wrote rejected review patch to {args.out}", file=sys.stderr)
        return 1
    print(f"[*] Wrote review patch to {args.out}")

    if args.commit_watermark:
        if not args.state:
            print("Error: --commit-watermark requires --state", file=sys.stderr)
            return 1
        import time
        with open(args.state, "r") as fh:
            data = json.load(fh)
        machine_meta = data.setdefault(args.machine_id, {}).setdefault("_meta", {})
        last_commit = machine_meta.setdefault("last_commit_values", {})
        now_ts = int(time.time())
        keys_to_update = [k.strip() for k in args.keys.split(",")] if args.keys else list(recommendations.keys())
        for key in keys_to_update:
            if key in recommendations:
                rec = recommendations[key][0]
                last_commit[key] = {
                    "value": rec,
                    "applied_at": now_ts,
                    "source": "config.ini"
                }
        tmp = args.state + ".tmp"
        machine_meta["last_commit_sample_total"] = sum(
            b.get("n", 0) for k, b in data.get(args.machine_id, {}).items() if not k.startswith("_")
        )
        with open(tmp, "w") as fh:
            json.dump(data, fh, indent=2, sort_keys=True)
            fh.write("\n")
        os.replace(tmp, args.state)
        print(f"[*] Updated watermark in {args.state} for keys: {', '.join(keys_to_update)}", file=sys.stderr)

    return 0


def main():
    ap = argparse.ArgumentParser(description="Analyze FLARE calibration CSVs")
    ap.add_argument("--in", dest="inputs", nargs="+")
    ap.add_argument("--out")
    ap.add_argument("--mode", choices=["safe", "aggressive"], default="safe")
    ap.add_argument("--state", help="Optional flare_live_tuner.py bucket state JSON")
    ap.add_argument("--machine-id", default="default", help="Machine ID for state file (default: default)")
    ap.add_argument("--config", default="config.ini", help="Current config.ini for current-value display")
    ap.add_argument("--profile-fast", help="Fast-profile CSV for deterministic emit modes")
    ap.add_argument("--profile-slow", help="Slow-profile CSV for deterministic emit modes")
    ap.add_argument("--emit-baseline", action="store_true", help="Emit deterministic scalar baseline patch from two profiles")
    ap.add_argument("--emit-flow-schedule", action="store_true", help="Emit deterministic flow-keyed schedule from two profiles")
    ap.add_argument("--flow-schedule-cap", type=int, help="Maximum emitted schedule points (1..16)")
    ap.add_argument("--acceptance-gate", action="store_true", help="Exit non-zero unless acceptance checks pass")
    ap.add_argument("--commit-watermark", action="store_true", help="Write analysis values to _meta watermark in state JSON")
    ap.add_argument("--keys", help="Comma-separated list of keys to update in watermark (defaults to all)")
    ap.add_argument("--include-stale", action="store_true", help="Include buckets not seen in >60 days")
    ap.add_argument("--force", action="store_true", help="Bypass the locked-bucket floor and emit low-confidence estimates")
    ap.add_argument("--feedforward", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()
    try:
        return run(args)
    except PathError as exc:
        print(f"Error: {exc.path}: {exc.reason}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
