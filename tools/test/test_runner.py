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
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

# Default PlatformIO path (Windows)
DEFAULT_PIO_PATH = os.path.expanduser(r"~\.platformio\penv\Scripts\platformio.exe")

# Test configuration
DEFAULT_TEST_DURATION_SEC = 10
DEFAULT_OUTPUT_DIR = "logs"
DEFAULT_TOLERANCE_MS = 50


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


# =================== Timeline Analysis ===================

@dataclass
class TimelineEvent:
    timestamp_ms: int
    task_name: str
    event: str


def load_expected_timeline(csv_path: str) -> list[TimelineEvent]:
    """Load expected timeline from CSV."""
    if not os.path.exists(csv_path):
        return []
    events = []
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            events.append(TimelineEvent(
                timestamp_ms=int(row['timestamp_ms']),
                task_name=row['task_name'],
                event=row['event']
            ))
    return sorted(events, key=lambda e: e.timestamp_ms)


def analyze_timeline(entries: list[dict], expected_path: str, tolerance_ms: int) -> tuple[bool, str]:
    """Analyze parsed log entries against expected timeline."""
    # Extract task events
    task_events = [e for e in entries if e['level'] == 'TASK']
    
    if not task_events:
        return False, "No task events found in log"
    
    # Load expected timeline if exists
    expected = load_expected_timeline(expected_path)
    
    # Generate summary
    lines = []
    lines.append("=" * 50)
    lines.append("TEST RESULTS")
    lines.append("=" * 50)
    
    # Count events by task
    task_counts = {}
    for e in task_events:
        name = e['context']
        task_counts[name] = task_counts.get(name, 0) + 1
    
    lines.append(f"\nTask Events Summary:")
    for name, count in sorted(task_counts.items()):
        lines.append(f"  {name}: {count} events")
    
    # Time range
    if task_events:
        first_ts = task_events[0]['timestamp_ms']
        last_ts = task_events[-1]['timestamp_ms']
        lines.append(f"\nTime Range: {first_ts}ms - {last_ts}ms (duration: {last_ts - first_ts}ms)")
    
    # Compare with expected if available
    if expected:
        matched = 0
        for exp in expected:
            for act in task_events:
                if (act['context'] == exp.task_name and 
                    act['event'] == exp.event and
                    abs(act['timestamp_ms'] - exp.timestamp_ms) <= tolerance_ms):
                    matched += 1
                    break
        
        lines.append(f"\nExpected Events: {len(expected)}")
        lines.append(f"Matched (±{tolerance_ms}ms): {matched}")
        lines.append(f"Result: {'PASS' if matched == len(expected) else 'FAIL'}")
        passed = matched == len(expected)
    else:
        lines.append(f"\nNo expected timeline found - showing captured events only")
        passed = True  # No comparison = pass
    
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
                    
                    # Check if test completed (TIMEOUT event detected)
                    if '\tTIMEOUT\t' in line or ',TIMEOUT,' in line:
                        print("\n[*] Test TIMEOUT detected, stopping capture...")
                        test_complete = True
                        # Give a moment for any final output
                        time.sleep(0.5)
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
    parser.add_argument("--tolerance", type=int, default=DEFAULT_TOLERANCE_MS,
                        help=f"Timeline tolerance in ms (default: {DEFAULT_TOLERANCE_MS})")
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
    
    # Expected timeline path
    expected_file = os.path.join(
        project_dir, "tools", "test", 
        f"expected_timeline_{args.test_name.replace('test_scheduler_', '')}.csv"
    )
    
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
        print(f"\n[*] Analyzing timeline...")
        passed, report = analyze_timeline(entries, expected_file, args.tolerance)
        print(report)
        
        if not passed:
            sys.exit(1)
    
    print("\n[+] Test complete!")


if __name__ == "__main__":
    main()
