#!/usr/bin/env python3
"""
KARTOS Test Runner

Single command to run a complete test: flash, capture serial, parse, and analyze.

Usage:
    python test_runner.py test_scheduler_rr --port COM3
    python test_runner.py test_scheduler_preemptive --port COM3 --duration 15
    python test_runner.py test_scheduler_cooperative --port COM3 --skip-analysis
    python test_runner.py test_mutex_state --port COM3 --skip-upload
"""

import argparse
import csv
import os
import re
import subprocess
import sys
import time

import serial
import serial.tools.list_ports

from datetime import datetime
from pathlib import Path

# Default OpenOCD search paths
_OPENOCD_HINTS = [
    os.path.expanduser(r"~\.platformio\packages\tool-openocd\bin\openocd.exe"),
    os.path.expanduser("~/.platformio/packages/tool-openocd/bin/openocd"),
]

# Test configuration
DEFAULT_BAUD             = 921600
DEFAULT_TEST_DURATION_SEC = 20
DEFAULT_OUTPUT_DIR       = os.path.join("tests", "artifacts")


# =================== Log Parsing ===================

LOG_PATTERN = re.compile(r'^(\d+)\t(\w+)\t([^\t]+)\t(\d+)\t([^\t]+)\t([^\t]+)\t(.+)$')


def parse_log_line(line: str) -> dict | None:
    """Parse a single log line into structured data."""
    line = line.strip()
    if not line:
        return None

    match = LOG_PATTERN.match(line)
    if not match:
        return None

    return {
        'timestamp_ms': int(match.group(1)),
        'level':        match.group(2),
        'file':         os.path.basename(match.group(3)),
        'line':         int(match.group(4)),
        'function':     match.group(5),
        'event':        match.group(6),
        'context':      match.group(7),
    }


def parse_log_file(input_path: str) -> list[dict]:
    """Parse entire log file and return list of parsed entries."""
    entries = []
    with open(input_path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            entry = parse_log_line(line)
            if entry:
                entries.append(entry)
    return entries


def write_csv(entries: list[dict], output_path: str):
    """Write parsed entries to CSV file."""
    if not entries:
        return
    fieldnames = ['timestamp_ms', 'level', 'file', 'line', 'function', 'event', 'context']
    with open(output_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(entries)


# =================== Verdict Analysis (invariant-based tests) ===================

VERDICT_TESTS = {
    'test_scheduler_preemptive_state',
    'test_scheduler_rr_state',
    'test_scheduler_cooperative_state',
    'test_mutex_state',
    'test_semaphore_state',
    'test_queue_state',
    'test_notification_state',
    'test_event_group_state',
    'test_task_state_transitions',
}


def analyze_verdict(entries: list[dict]) -> tuple[bool, str]:
    """Analyze parsed log entries for RESULT verdict (PASS / FAIL:N)."""
    lines = []
    lines.append("=" * 50)
    lines.append("VERDICT RESULTS")
    lines.append("=" * 50)

    pass_count = 0
    fail_count = 0
    for e in entries:
        if e['event'] == 'ASSERT_PASS':
            pass_count += 1
        elif e['event'] == 'ASSERT_FAIL':
            fail_count += 1

    lines.append(f"\nAssertions: {pass_count} passed, {fail_count} failed")

    result_entry = None
    for e in entries:
        if e['event'] == 'RESULT':
            result_entry = e
            break

    if result_entry is None:
        lines.append("\n[!] No RESULT line found — test may have timed out without verdict")
        lines.append("Result: FAIL (no verdict)")
        lines.append("=" * 50)
        return False, "\n".join(lines)

    verdict = result_entry['context'].strip()
    if verdict == 'PASS':
        lines.append(f"\nVerdict: PASS")
        passed = True
    elif verdict.startswith('FAIL'):
        lines.append(f"\nVerdict: {verdict}")
        passed = False
    else:
        lines.append(f"\nUnexpected verdict value: {verdict}")
        passed = False

    lines.append(f"Result: {'PASS' if passed else 'FAIL'}")
    lines.append("=" * 50)
    return passed, "\n".join(lines)


# =================== OpenOCD / Serial Interface ===================

def find_openocd() -> str | None:
    """Return path to openocd executable, or None if not found."""
    for hint in _OPENOCD_HINTS:
        if os.path.isfile(hint):
            return hint
    # Fall back to system PATH
    try:
        which = "where" if sys.platform == "win32" else "which"
        result = subprocess.run([which, "openocd"], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip().split('\n')[0]
    except FileNotFoundError:
        pass
    return None


def find_serial_port() -> str | None:
    """Return the first available ST-Link VCP port, or None."""
    for port in serial.tools.list_ports.comports():
        desc = (port.description or "").lower()
        mfr  = (port.manufacturer or "").lower()
        if "stlink" in desc or "stm32" in desc or "stlink" in mfr or "stm" in mfr:
            return port.device
    ports = serial.tools.list_ports.comports()
    return ports[0].device if len(ports) == 1 else None


def flash_firmware(openocd_path: str, project_dir: str, test_name: str, board: str) -> bool:
    """Flash the pre-built ELF for test_name via OpenOCD."""
    build_dir = os.path.join(project_dir, "build", board)
    elf_path  = os.path.join(build_dir, f"{test_name}.elf").replace("\\", "/")
    cfg_path  = os.path.join(project_dir, "boards", board, "openocd.cfg")

    if not os.path.isfile(elf_path.replace("/", os.sep)):
        print(f"[!] ELF not found: {elf_path}")
        print(f"    Run: cmake --build --preset {test_name}")
        return False

    print(f"[*] Flashing {test_name}.elf via OpenOCD...")
    cmd = [openocd_path, "-f", cfg_path, "-c", f"program {{{elf_path}}} verify reset exit"]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        if "Verified OK" in output:
            print("[+] Flash successful")
            return True
        print(f"[!] Flash failed:\n{output}")
        return False
    except subprocess.TimeoutExpired:
        print("[!] Flash timed out")
        return False
    except Exception as e:
        print(f"[!] Flash error: {e}")
        return False


def capture_serial(port: str, baud: int, duration_sec: int, output_file: str) -> bool:
    """Capture serial output for duration_sec seconds (stops early on RESULT/TIMEOUT)."""
    print(f"[*] Capturing serial output on {port} at {baud} baud (max {duration_sec}s)...")

    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"[!] Cannot open {port}: {e}")
        return False

    output_lines = []
    test_complete = False
    deadline = time.monotonic() + duration_sec

    try:
        while time.monotonic() < deadline:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"[!] Serial read error: {e}")
                break
            if not raw:
                continue
            line = raw.decode('utf-8', errors='replace')
            output_lines.append(line)
            print(line, end='', flush=True)

            if '\tRESULT\t' in line or ',RESULT,' in line:
                print("\n[*] RESULT detected — stopping capture.")
                test_complete = True
                break

            if '\tTIMEOUT\t' in line or ',TIMEOUT,' in line:
                print("\n[*] Test TIMEOUT detected — waiting for verdict (5s)...")
                verdict_deadline = time.monotonic() + 5.0
                while time.monotonic() < verdict_deadline:
                    try:
                        extra = ser.readline()
                    except serial.SerialException:
                        break
                    if extra:
                        extra_line = extra.decode('utf-8', errors='replace')
                        output_lines.append(extra_line)
                        print(extra_line, end='', flush=True)
                        if '\tRESULT\t' in extra_line or ',RESULT,' in extra_line:
                            break
                test_complete = True
                break
    finally:
        ser.close()

    with open(output_file, 'w', encoding='utf-8') as f:
        f.writelines(output_lines)

    status = "complete" if test_complete else f"captured {duration_sec}s"
    print(f"\n[+] Captured {len(output_lines)} lines ({status})")
    return True


# =================== Main ===================

def main():
    parser = argparse.ArgumentParser(
        description="KARTOS Test Runner — flash, capture, parse, and analyze",
        epilog="Example: python test_runner.py test_scheduler_rr --port COM3"
    )
    parser.add_argument("test_name",
        help="Variant name matching cmake/variants.cmake (e.g. test_scheduler_rr_state)")
    parser.add_argument("--port", default=None,
        help="Serial port (e.g. COM3, /dev/ttyACM0). Auto-detected if omitted.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
        help=f"Baud rate (default: {DEFAULT_BAUD})")
    parser.add_argument("--board", default="stm32f446re_nucleo",
        help="Board name used for build path and OpenOCD config (default: stm32f446re_nucleo)")
    parser.add_argument("--duration", type=int, default=DEFAULT_TEST_DURATION_SEC,
        help=f"Max capture duration in seconds (default: {DEFAULT_TEST_DURATION_SEC})")
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR,
        help="Output directory for logs and CSV")
    parser.add_argument("--skip-upload", action="store_true",
        help="Skip flashing — capture from already-running firmware")
    parser.add_argument("--skip-analysis", action="store_true",
        help="Skip verdict analysis")
    parser.add_argument("--openocd-path", default=None,
        help="Path to openocd executable (auto-detected if omitted)")

    args = parser.parse_args()

    # Resolve paths
    project_dir = str(Path(__file__).resolve().parents[2])
    output_dir  = os.path.join(project_dir, args.output_dir)
    os.makedirs(output_dir, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file  = os.path.join(output_dir, f"log_{args.test_name}_{timestamp}.txt")
    csv_file  = os.path.join(output_dir, f"log_{args.test_name}_{timestamp}.csv")

    print("=" * 50)
    print(f"KARTOS TEST: {args.test_name}")
    print("=" * 50)

    # Step 1: Flash
    if not args.skip_upload:
        openocd = args.openocd_path or find_openocd()
        if not openocd:
            print("[!] openocd not found. Specify --openocd-path or install it.")
            sys.exit(1)
        if not flash_firmware(openocd, project_dir, args.test_name, args.board):
            sys.exit(1)
        time.sleep(1)

    # Step 2: Resolve serial port
    port = args.port or find_serial_port()
    if not port:
        print("[!] No serial port specified and auto-detection found none.")
        print("    Specify --port (e.g. --port COM3 or --port /dev/ttyACM0).")
        sys.exit(1)

    # Step 3: Capture
    if not capture_serial(port, args.baud, args.duration, log_file):
        sys.exit(1)

    # Step 4: Parse
    print(f"\n[*] Parsing log file...")
    entries = parse_log_file(log_file)
    print(f"[+] Parsed {len(entries)} entries")

    if entries:
        write_csv(entries, csv_file)
        print(f"[+] Saved CSV: {csv_file}")

    # Step 5: Analyze
    if not args.skip_analysis:
        print(f"\n[*] Analyzing verdict...")
        passed, report = analyze_verdict(entries)
        print(report)

        if not passed:
            sys.exit(1)

    print("\n[+] Test complete!")


if __name__ == "__main__":
    main()
