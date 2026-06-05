#!/usr/bin/env python3
"""
FLARE — G-code Metadata Marker (Lean)
Injects markers for feature/speed/geometry and a final FINISH marker.
Optimized to remove IDLE markers for cleaner G-code.
"""

import argparse
import hashlib
import json
import math
import os
import re
import sys
import tempfile

from path_utils import PathError, normalize_output, resolve_input

# Regular expressions
MOVE_RE = re.compile(r"([Gg][0123])\s*(.*)")
PARAM_RE = re.compile(r"([XYZEFIJ])([-+]?\d*\.?\d*)")
FEATURE_RES = [
    re.compile(r"^; TYPE:(.*)"),
    re.compile(r"^; FEATURE:(.*)"),
    re.compile(r"^;TYPE:(.*)"),
]
WIDTH_RE = re.compile(r"^;WIDTH:([-+]?\d*\.?\d*)")
HEIGHT_RE = re.compile(r"^;HEIGHT:([-+]?\d*\.?\d*)")
LHEIGHT_RE = re.compile(r"^;layer_height=([-+]?\d*\.?\d*)")
LAYER_RE = re.compile(r"^;LAYER:(\d+)")
LAYER_CHANGE_RE = re.compile(r"^;LAYER_CHANGE\b")
EXCLUDE_START_RE = re.compile(r"^EXCLUDE_OBJECT_START\b(?:.*\bNAME=([^\s]+))?")
EXCLUDE_END_RE = re.compile(r"^EXCLUDE_OBJECT_END\b")


def _source_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _parse_params(raw_line):
    code = raw_line.split(";", 1)[0].upper()
    return {k: float(v) for k, v in PARAM_RE.findall(code) if v not in ("", "+", "-")}


def _v_fil(width, height, feedrate, fil_area):
    # Preserve the existing marker math so sidecar events bucket exactly like
    # legacy M118/RUN_SHELL_COMMAND markers.
    return (width * height * feedrate) / fil_area


def _v_bin(v_fil):
    return int(round(v_fil / 25.0)) * 25


def _empty_segment(
    line_start,
    line_end,
    layer,
    feature,
    z_mm,
    width,
    height,
    feedrate,
    v_fil,
    e_start,
    e_end,
    x_start,
    y_start,
    x_end,
    y_end,
    skip,
    object_name="",
):
    return {
        "byte_start": line_start,
        "byte_end": line_end,
        "layer": int(layer),
        "feature": feature or "Unknown",
        "z_mm": float(z_mm),
        "width_mm": float(width),
        "height_mm": float(height),
        "feedrate_mm_per_min": float(feedrate),
        "v_fil_mm3_per_s": float(v_fil),
        "v_fil_bin": _v_bin(v_fil),
        "e_start": float(e_start),
        "e_end": float(e_end),
        "x_start": float(x_start),
        "y_start": float(y_start),
        "x_end": float(x_end),
        "y_end": float(y_end),
        "skip": bool(skip),
        "object": object_name or "",
    }


def build_sidecar(input_path, sidecar_path, dia):
    """Build `<basename>.flare.json` metadata for Klipper motion tracking."""
    if not os.path.exists(input_path):
        raise FileNotFoundError(input_path)

    fil_area = math.pi * (dia / 2.0) ** 2
    current_f = 0.0
    current_w = None
    current_h = None
    current_feature = "Unknown"
    current_layer = 0
    current_z = 0.0
    current_x = 0.0
    current_y = 0.0
    current_e = 0.0
    e_mode = "absolute"
    layer_change_n = -1
    current_object = ""
    layers = []
    segments = []
    current_segment = None

    def close_segment():
        nonlocal current_segment
        if current_segment is not None:
            segments.append(current_segment)
            current_segment = None

    def note_layer(index, z_mm, byte_start):
        nonlocal current_layer
        if layers:
            layers[-1]["byte_end"] = byte_start
        current_layer = int(index)
        layers.append(
            {
                "index": int(index),
                "z_mm": float(z_mm),
                "byte_start": int(byte_start),
                "byte_end": int(byte_start),
            }
        )

    with open(input_path, "rb") as fh:
        while True:
            line_start = fh.tell()
            raw = fh.readline()
            if not raw:
                break
            line_end = fh.tell()
            line = raw.decode("utf-8", errors="replace")
            raw_line = line.strip()
            upper_line = raw_line.upper()

            if upper_line.startswith("M82"):
                e_mode = "absolute"
            elif upper_line.startswith("M83"):
                e_mode = "relative"
            elif upper_line.startswith("G92"):
                params = _parse_params(raw_line)
                if "E" in params:
                    current_e = params["E"]

            exclude_start = EXCLUDE_START_RE.match(raw_line)
            if exclude_start:
                close_segment()
                current_object = exclude_start.group(1) or current_object
            elif EXCLUDE_END_RE.match(raw_line):
                close_segment()
                current_object = ""

            layer_match = LAYER_RE.match(raw_line)
            if layer_match:
                close_segment()
                note_layer(int(layer_match.group(1)), current_z, line_start)
            elif LAYER_CHANGE_RE.match(raw_line):
                close_segment()
                layer_change_n += 1
                note_layer(layer_change_n, current_z, line_start)

            for regex in FEATURE_RES:
                match = regex.match(raw_line)
                if match:
                    feature = match.group(1).strip() or "Unknown"
                    if feature != current_feature:
                        close_segment()
                        current_feature = feature
                    break

            w_match = WIDTH_RE.match(raw_line)
            if w_match:
                new_w = float(w_match.group(1))
                if current_w != new_w:
                    close_segment()
                    current_w = new_w

            h_match = HEIGHT_RE.match(raw_line) or LHEIGHT_RE.match(raw_line)
            if h_match:
                new_h = float(h_match.group(1))
                if current_h != new_h:
                    close_segment()
                    current_h = new_h

            move_match = MOVE_RE.match(raw_line)
            if not move_match:
                continue

            move = move_match.group(1).upper()
            params = _parse_params(raw_line)
            prev_x, prev_y, _prev_z, prev_e = current_x, current_y, current_z, current_e
            if "F" in params:
                current_f = params["F"]
            end_x = params.get("X", current_x)
            end_y = params.get("Y", current_y)
            end_z = params.get("Z", current_z)

            e_delta = 0.0
            end_e = current_e
            if "E" in params:
                if e_mode == "relative":
                    e_delta = params["E"]
                    end_e = current_e + params["E"]
                else:
                    end_e = params["E"]
                    e_delta = end_e - current_e

            current_x, current_y, current_z, current_e = end_x, end_y, end_z, end_e

            if "Z" in params and layers:
                layers[-1]["z_mm"] = float(end_z)

            if e_delta <= 0.0 or not (current_f > 0 and current_w and current_h):
                continue

            v_fil = _v_fil(current_w, current_h, current_f, fil_area)
            key = (
                current_layer,
                current_feature,
                current_w,
                current_h,
                _v_bin(v_fil),
                current_object,
            )
            active_key = None
            if current_segment is not None:
                active_key = (
                    current_segment["layer"],
                    current_segment["feature"],
                    current_segment["width_mm"],
                    current_segment["height_mm"],
                    current_segment["v_fil_bin"],
                    current_segment.get("object", ""),
                )

            if move in ("G2", "G3"):
                close_segment()
                segments.append(
                    _empty_segment(
                        line_start, line_end, current_layer, current_feature,
                        end_z, current_w, current_h, current_f, v_fil,
                        prev_e, end_e, prev_x, prev_y, end_x, end_y, False, current_object,
                    )
                )
                continue

            if current_segment is None or key != active_key:
                close_segment()
                current_segment = _empty_segment(
                    line_start, line_end, current_layer, current_feature,
                    end_z, current_w, current_h, current_f, v_fil,
                    prev_e, end_e, prev_x, prev_y, end_x, end_y, False, current_object,
                )
            else:
                current_segment["byte_end"] = line_end
                current_segment["e_end"] = float(end_e)
                current_segment["x_end"] = float(end_x)
                current_segment["y_end"] = float(end_y)
                current_segment["z_mm"] = float(end_z)

        file_size = fh.tell()

    close_segment()
    if not layers:
        layers.append({"index": 0, "z_mm": current_z, "byte_start": 0, "byte_end": file_size})
    else:
        layers[-1]["byte_end"] = file_size

    sidecar = {
        "_schema": 1,
        "generator": "gcode_marker.py 2.10.1",
        "source_gcode": os.path.basename(input_path),
        "source_sha256": _source_sha256(input_path),
        "filament_dia_mm": float(dia),
        "fil_area_mm2": fil_area,
        "e_mode": e_mode,
        "layers": layers,
        "segments": segments,
    }
    parent = os.path.dirname(os.path.abspath(sidecar_path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(sidecar_path, "w") as fh:
        json.dump(sidecar, fh, indent=2, sort_keys=True)
        fh.write("\n")
    return sidecar


def _write_sidecar_gcode(input_path, output_path):
    with open(input_path) as fin, open(output_path, "w") as fout:
        fout.write("M118 NT:START\n")
        for line in fin:
            fout.write(line)
        fout.write("\n; --- FLARE TUNING FINISH ---\n")
        fout.write("M118 FLARE_TUNE:FINISH:0:0:0\n")


def process_gcode(input_path, output_path, filament_dia=1.75, sidecar_path=None):
    if not os.path.exists(input_path):
        print(f"Error: Input file {input_path} not found.")
        return False

    print("[*] Processing file with FLARE sidecar metadata ...")
    _write_sidecar_gcode(input_path, output_path)
    if sidecar_path is None:
        base, _ext = os.path.splitext(output_path)
        sidecar_path = base + ".flare.json"
    sidecar = build_sidecar(output_path, sidecar_path, filament_dia)
    print(
        f"[*] Done. Wrote {len(sidecar['segments'])} sidecar segments to {sidecar_path}.",
        file=sys.stderr,
    )
    return True


def main():
    parser = argparse.ArgumentParser(description="Inject lean sync markers")
    parser.add_argument("input", help="Input G-code")
    parser.add_argument("--output", help="Output path")
    parser.add_argument("--sidecar", help="Sidecar JSON path")
    parser.add_argument("--dia", type=float, default=1.75, help="Filament diameter")
    parser.add_argument("--emit", choices=["sidecar"], default="sidecar",
                        help="Marker output: sidecar JSON")

    args = parser.parse_args()

    try:
        args.input = resolve_input(args.input)
        args.output = normalize_output(args.output)
        args.sidecar = normalize_output(args.sidecar)
    except PathError as exc:
        print(f"Error: {exc.path}: {exc.reason}", file=sys.stderr)
        sys.exit(2)

    in_place = args.output is None
    if in_place:
        tmp_fd, tmp_path = tempfile.mkstemp(suffix=".gcode", dir=os.path.dirname(os.path.abspath(args.input)))
        os.close(tmp_fd)
        output = tmp_path
    else:
        output = args.output

    sidecar_arg = args.sidecar
    if in_place and sidecar_arg is None:
        base, _ext = os.path.splitext(args.input)
        sidecar_arg = base + ".flare.json"

    ok = process_gcode(
        args.input,
        output,
        args.dia,
        sidecar_arg,
    )
    if ok and in_place:
        os.replace(tmp_path, args.input)
        sidecar_path = sidecar_arg
        if sidecar_path is None:
            base, _ext = os.path.splitext(args.input)
            sidecar_path = base + ".flare.json"
        build_sidecar(args.input, sidecar_path, args.dia)
    elif not ok and in_place and os.path.exists(tmp_path):
        os.unlink(tmp_path)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
