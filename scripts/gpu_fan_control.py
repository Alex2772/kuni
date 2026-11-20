#!/usr/bin/env python3
"""
gpu_fan_control.py — controls fan speed (via hwmon pwm) based on GPU temperature.

Reads GPU temperature via `nvidia-smi` and writes a PWM value (0-255) to a
hwmon pwm file (e.g. /sys/class/hwmon/hwmon1/pwm7) based on a temperature ->
speed curve.

Usage:
    sudo python3 gpu_fan_control.py
    sudo python3 gpu_fan_control.py --pwm /sys/class/hwmon/hwmon1/pwm7 --interval 2

Notes:
    - Writing to /sys/class/hwmon/*/pwmN usually requires root.
    - Most hwmon pwm devices also expose a pwmN_enable file that must be set
      to 1 (manual control) before pwmN can be written directly. This script
      sets it automatically unless --no-enable-manual is passed, and restores
      the original mode on exit (Ctrl+C / SIGTERM).
"""

from __future__ import annotations

import argparse
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

# (temperature_celsius, pwm_value) control points, sorted by temperature.
# Linear interpolation is used between points.
DEFAULT_CURVE = [
    (0, 40),    # idle / min speed
    (50, 40),
    (60, 80),
    (70, 140),
    (80, 200),
    (90, 255),
]

PWM_MIN = 0
PWM_MAX = 255


def get_gpu_temp(gpu_index: int = 0) -> int:
    """Returns current GPU temperature in Celsius via nvidia-smi."""
    result = subprocess.run(
        [
            "nvidia-smi",
            f"--id={gpu_index}",
            "--query-gpu=temperature.gpu",
            "--format=csv,noheader,nounits",
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    return int(result.stdout.strip())


def parse_curve(raw: str):
    """Parses a curve string like "40:40,60:80,70:140,80:200,90:255" into a
    sorted list of (temp, pwm) tuples."""
    points = []
    for chunk in raw.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        temp_s, pwm_s = chunk.split(":")
        points.append((int(temp_s), int(pwm_s)))
    points.sort(key=lambda p: p[0])
    if not points:
        raise ValueError("empty curve")
    return points


def pwm_for_temp(curve, temp: int) -> int:
    """Linearly interpolates the pwm value for `temp` using `curve`."""
    if temp <= curve[0][0]:
        return curve[0][1]
    if temp >= curve[-1][0]:
        return curve[-1][1]

    for (t0, p0), (t1, p1) in zip(curve, curve[1:]):
        if t0 <= temp <= t1:
            if t1 == t0:
                return p1
            ratio = (temp - t0) / (t1 - t0)
            return round(p0 + ratio * (p1 - p0))

    return curve[-1][1]


def clamp(value: int, lo: int = PWM_MIN, hi: int = PWM_MAX) -> int:
    return max(lo, min(hi, value))


def write_pwm(pwm_path: Path, value: int) -> None:
    pwm_path.write_text(str(clamp(value)))


def read_pwm_enable(pwm_path: Path) -> Optional[str]:
    enable_path = Path(str(pwm_path) + "_enable")
    if enable_path.exists():
        try:
            return enable_path.read_text().strip()
        except OSError:
            return None
    return None


def write_pwm_enable(pwm_path: Path, value: str) -> None:
    enable_path = Path(str(pwm_path) + "_enable")
    if enable_path.exists():
        enable_path.write_text(value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pwm",
        default="/sys/class/hwmon/hwmon1/pwm7",
        help="Path to the hwmon pwm control file (default: %(default)s)",
    )
    parser.add_argument(
        "--gpu-index",
        type=int,
        default=0,
        help="nvidia-smi GPU index to query (default: %(default)s)",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=2.0,
        help="Polling interval in seconds (default: %(default)s)",
    )
    parser.add_argument(
        "--curve",
        default=",".join(f"{t}:{p}" for t, p in DEFAULT_CURVE),
        help="Temp:pwm control points, comma separated, e.g. '50:40,70:140,90:255'",
    )
    parser.add_argument(
        "--min-pwm",
        type=int,
        default=None,
        help="Hard floor applied to computed pwm value",
    )
    parser.add_argument(
        "--max-pwm",
        type=int,
        default=None,
        help="Hard ceiling applied to computed pwm value",
    )
    parser.add_argument(
        "--no-enable-manual",
        action="store_true",
        help="Don't touch pwmN_enable (assume manual mode is already set)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would happen without writing to the pwm file",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print temp/pwm on every poll",
    )
    args = parser.parse_args()

    curve = parse_curve(args.curve)
    pwm_path = Path(args.pwm)

    if not args.dry_run and not pwm_path.exists():
        print(f"error: pwm path does not exist: {pwm_path}", file=sys.stderr)
        return 1

    original_enable = None
    if not args.no_enable_manual and not args.dry_run:
        original_enable = read_pwm_enable(pwm_path)
        if original_enable is not None and original_enable != "1":
            print(f"setting {pwm_path}_enable to manual mode (1)")
            try:
                write_pwm_enable(pwm_path, "1")
            except PermissionError:
                print(
                    f"error: permission denied writing to {pwm_path}_enable "
                    "(run with sudo)",
                    file=sys.stderr,
                )
                return 1

    stop = False

    def _handle_signal(signum, frame):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    print(f"gpu_fan_control: pwm={pwm_path} curve={curve} interval={args.interval}s")

    last_pwm = None
    try:
        while not stop:
            try:
                temp = get_gpu_temp(args.gpu_index)
            except (subprocess.CalledProcessError, FileNotFoundError, ValueError) as e:
                print(f"warning: failed to read GPU temperature: {e}", file=sys.stderr)
                time.sleep(args.interval)
                continue

            pwm_value = pwm_for_temp(curve, temp)
            if args.min_pwm is not None:
                pwm_value = max(pwm_value, args.min_pwm)
            if args.max_pwm is not None:
                pwm_value = min(pwm_value, args.max_pwm)
            pwm_value = clamp(pwm_value)

            if args.verbose or pwm_value != last_pwm:
                print(f"GPU temp: {temp}°C -> pwm {pwm_value}")

            if not args.dry_run:
                try:
                    write_pwm(pwm_path, pwm_value)
                except PermissionError:
                    print(
                        f"error: permission denied writing to {pwm_path} "
                        "(run with sudo)",
                        file=sys.stderr,
                    )
                    return 1
                except OSError as e:
                    print(f"warning: failed to write pwm value: {e}", file=sys.stderr)

            last_pwm = pwm_value
            time.sleep(args.interval)
    finally:
        if original_enable is not None and original_enable != "1" and not args.dry_run:
            print(f"restoring {pwm_path}_enable to {original_enable}")
            try:
                write_pwm_enable(pwm_path, original_enable)
            except OSError:
                pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
