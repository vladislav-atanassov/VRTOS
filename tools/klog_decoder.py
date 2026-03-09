#!/usr/bin/env python3
"""
klog_decoder.py — Host-side KLog serial capture and decoder.

Connects to a serial port, captures lines from the KLog flush task,
and writes decoded output to a timestamped log file in logs/klogs/.

Usage:
    python klog_decoder.py [--port COM3] [--baud 115200] [--output logs/klogs/]

The flush task on-target already decodes KLog binary records into
human-readable lines like:

    [K/I] 00345678 T02 TaskCreate       0x00000001 0x00000003

This script captures those lines, optionally filters them, adds host
timestamps, and saves to a log file. It can also be extended to decode
raw binary KLog records for SWO/RTT capture.
"""

import argparse
import datetime
import os
import re
import sys

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


# Regex to match KLog flush task output lines
KLOG_LINE_RE = re.compile(
    r"\[K/([FEWIDT])\]\s+([0-9A-Fa-f]+)\s+(T\d+|ISR)\s+(\S+)\s+(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)"
)


def parse_args():
    parser = argparse.ArgumentParser(description="KLog serial decoder")
    parser.add_argument("--port", default="COM3", help="Serial port (default: COM3)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    default_out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "logs", "klogs"))
    parser.add_argument("--output", default=default_out, help="Output directory (default: logs/klogs/)")
    parser.add_argument("--filter-level", default=None, choices=["F", "E", "W", "I", "D", "T"],
                        help="Minimum level to display (default: show all)")
    return parser.parse_args()


LEVEL_ORDER = {"F": 0, "E": 1, "W": 2, "I": 3, "D": 4, "T": 5}


def should_display(level_char, min_level):
    if min_level is None:
        return True
    return LEVEL_ORDER.get(level_char, 99) <= LEVEL_ORDER.get(min_level, 99)


def main():
    args = parse_args()

    # Create output directory
    os.makedirs(args.output, exist_ok=True)

    # Generate log filename with timestamp
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = os.path.join(args.output, f"klog_{ts}.log")

    print(f"KLog Decoder — connecting to {args.port} at {args.baud} baud")
    print(f"Logging to: {log_path}")
    print("Press Ctrl+C to stop\n")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {args.port}: {e}")
        sys.exit(1)

    klog_count = 0
    other_count = 0

    try:
        with open(log_path, "w") as logfile:
            logfile.write(f"# KLog capture started at {ts}\n")
            logfile.write(f"# Port: {args.port} Baud: {args.baud}\n")
            logfile.write("#\n")

            while True:
                try:
                    raw = ser.readline()
                    if not raw:
                        continue

                    line = raw.decode("ascii", errors="replace").rstrip()
                    if not line:
                        continue

                    # Check if this is a KLog line
                    m = KLOG_LINE_RE.match(line)
                    if m:
                        level, cycles, ctx, event, arg0, arg1 = m.groups()
                        klog_count += 1

                        if should_display(level, args.filter_level):
                            host_ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                            display = f"[{host_ts}] {line}"
                            print(display)
                            logfile.write(display + "\n")
                    else:
                        # Non-KLog line (user prints, profiling, etc.)
                        other_count += 1
                        host_ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        display = f"[{host_ts}] {line}"
                        print(display)
                        logfile.write(display + "\n")

                    logfile.flush()

                except UnicodeDecodeError:
                    continue

    except KeyboardInterrupt:
        print(f"\n\nCapture stopped. KLog records: {klog_count}, Other lines: {other_count}")
        print(f"Log saved to: {log_path}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
