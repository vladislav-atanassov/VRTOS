#!/usr/bin/env python3
"""
VRTOS Test Runner

Single command to run a complete test: upload, capture, parse, and analyze.

Usage:
    python test_runner.py test_scheduler_rr
    python test_runner.py test_scheduler_preemptive --duration 15
    python test_runner.py test_scheduler_cooperative --skip-analysis
"""

import argparse
import csv

import os
import re
import subprocess
import sys
import time

from datetime import datetime
from pathlib import Path

# Default PlatformIO path (Windows)
DEFAULT_PIO_PATH = os.path.expanduser(r"~\.platformio\penv\Scripts\platformio.exe")

# Test configuration
DEFAULT_TEST_DURATION_SEC = 20
DEFAULT_OUTPUT_DIR = os.path.join("logs", "tests")


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
        'level': match.group(2),
        'file': os.path.basename(match.group(3)),
        'line': int(match.group(4)),
        'function': match.group(5),
        'event': match.group(6),
        'context': match.group(7)
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

    # Count assertion results
    pass_count = 0
    fail_count = 0
    for e in entries:
        if e['event'] == 'ASSERT_PASS':
            pass_count += 1
        elif e['event'] == 'ASSERT_FAIL':
            fail_count += 1

    lines.append(f"\nAssertions: {pass_count} passed, {fail_count} failed")

    # Find the RESULT line
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


# =================== PlatformIO Interface ===================

def find_platformio():
    """Find PlatformIO executable."""
    if os.path.exists(DEFAULT_PIO_PATH):
        return DEFAULT_PIO_PATH
    try:
        result = subprocess.run(["where", "platformio"], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip().split('\n')[0]
    except FileNotFoundError:
        pass
    return None


def upload_firmware(pio_path: str, project_dir: str, environment: str) -> bool:
    """Upload firmware to device."""
    print(f"[*] Building and uploading: {environment}")
    cmd = [pio_path, "run", "--target", "upload", "--environment", environment]
    
    try:
        result = subprocess.run(cmd, cwd=project_dir, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            print(f"[!] Upload failed:\n{result.stderr}")
            return False
        print("[+] Upload successful")
        return True
    except subprocess.TimeoutExpired:
        print("[!] Upload timed out")
        return False
    except Exception as e:
        print(f"[!] Upload error: {e}")
        return False


def capture_serial(pio_path: str, project_dir: str, environment: str, 
                   duration_sec: int, output_file: str) -> bool:
    """Capture serial output for specified duration or until TIMEOUT event."""
    print(f"[*] Capturing serial output (max {duration_sec} seconds)...")
    cmd = [pio_path, "device", "monitor", "--environment", environment]
    
    try:
        process = subprocess.Popen(
            cmd, cwd=project_dir,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        
        start_time = time.time()
        output_lines = []
        test_complete = False
        
        while time.time() - start_time < duration_sec:
            if process.poll() is not None:
                break
            try:
                line = process.stdout.readline()
                if line:
                    output_lines.append(line)
                    print(line, end='')
                    
                    # Stop on RESULT (verdict tests emit this before TIMEOUT)
                    if '\tRESULT\t' in line or ',RESULT,' in line:
                        print("\n[*] RESULT detected, stopping capture...")
                        test_complete = True
                        break
                    
                    # Stop on TIMEOUT, then wait for RESULT
                    if '\tTIMEOUT\t' in line or ',TIMEOUT,' in line:
                        print("\n[*] Test TIMEOUT detected, waiting for verdict...")
                        test_complete = True
                        deadline = time.time() + 5.0
                        while time.time() < deadline:
                            if process.poll() is not None:
                                break
                            try:
                                extra = process.stdout.readline()
                                if extra:
                                    output_lines.append(extra)
                                    print(extra, end='')
                                    if '\tRESULT\t' in extra or ',RESULT,' in extra:
                                        break
                            except Exception:
                                pass
                        break
            except Exception:
                pass
        
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
        
        with open(output_file, 'w', encoding='utf-8') as f:
            f.writelines(output_lines)
        
        status = "complete" if test_complete else f"captured {duration_sec}s"
        print(f"\n[+] Captured {len(output_lines)} lines ({status})")
        return True
        
    except Exception as e:
        print(f"[!] Capture error: {e}")
        return False


# =================== Main ===================

def main():
    parser = argparse.ArgumentParser(
        description="VRTOS Test Runner - Single command to run complete test",
        epilog="Example: python test_runner.py test_scheduler_rr"
    )
    parser.add_argument("test_name", help="Test environment name (e.g., test_scheduler_rr)")
    parser.add_argument("--duration", type=int, default=DEFAULT_TEST_DURATION_SEC,
                        help=f"Capture duration in seconds (default: {DEFAULT_TEST_DURATION_SEC})")

    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR, help="Output directory")
    parser.add_argument("--skip-upload", action="store_true", help="Skip firmware upload")
    parser.add_argument("--skip-analysis", action="store_true", help="Skip timeline analysis")
    parser.add_argument("--pio-path", default=None, help="Path to PlatformIO executable")
    
    args = parser.parse_args()
    
    # Find PlatformIO
    pio_path = args.pio_path or find_platformio()
    if not pio_path:
        print("[!] Could not find PlatformIO. Please specify --pio-path")
        sys.exit(1)
    
    # Resolve paths
    project_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    output_dir = os.path.join(project_dir, args.output_dir)
    os.makedirs(output_dir, exist_ok=True)
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(output_dir, f"log_{args.test_name}_{timestamp}.txt")
    csv_file = os.path.join(output_dir, f"log_{args.test_name}_{timestamp}.csv")
    

    
    print("=" * 50)
    print(f"VRTOS TEST: {args.test_name}")
    print("=" * 50)
    
    # Step 1: Upload
    if not args.skip_upload:
        if not upload_firmware(pio_path, project_dir, args.test_name):
            sys.exit(1)
    
    time.sleep(1)
    
    # Step 2: Capture
    if not capture_serial(pio_path, project_dir, args.test_name, args.duration, log_file):
        sys.exit(1)
    
    # Step 3: Parse
    print(f"\n[*] Parsing log file...")
    entries = parse_log_file(log_file)
    print(f"[+] Parsed {len(entries)} entries")
    
    if entries:
        write_csv(entries, csv_file)
        print(f"[+] Saved CSV: {csv_file}")
    
    # Step 4: Analyze
    if not args.skip_analysis:
        print(f"\n[*] Analyzing verdict...")
        passed, report = analyze_verdict(entries)
        print(report)
        
        if not passed:
            sys.exit(1)
    
    print("\n[+] Test complete!")


if __name__ == "__main__":
    main()
