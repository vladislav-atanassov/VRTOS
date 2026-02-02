#!/usr/bin/env python3
"""
VRTOS Log Parser

Parses tab-delimited test logs and converts to structured CSV format.

Input format (tab-delimited):
    timestamp_ms\tlevel\tfile\tline\tfunc\tevent\tcontext

Output CSV columns:
    timestamp_ms,level,file,line,function,event,context

Usage:
    python log_parser.py <input_log> [-o output.csv]
"""

import argparse
import csv
import os
import re
import sys
from pathlib import Path


# Pattern for test log lines (tab-delimited)
# Format: 00001234\tTASK\tfile.c\t45\tfunc_name\tSTART\tTask1
LOG_PATTERN = re.compile(
    r'^(\d+)\t(\w+)\t([^\t]+)\t(\d+)\t([^\t]+)\t([^\t]+)\t(.+)$'
)


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
        'file': os.path.basename(match.group(3)),  # Extract filename only
        'line': int(match.group(4)),
        'function': match.group(5),
        'event': match.group(6),
        'context': match.group(7)
    }


def parse_log_file(input_path: str) -> list[dict]:
    """Parse entire log file and return list of parsed entries."""
    entries = []
    
    with open(input_path, 'r', encoding='utf-8', errors='replace') as f:
        for line_num, line in enumerate(f, 1):
            entry = parse_log_line(line)
            if entry:
                entries.append(entry)
    
    return entries


def write_csv(entries: list[dict], output_path: str):
    """Write parsed entries to CSV file."""
    if not entries:
        print("[!] No entries to write")
        return
    
    fieldnames = ['timestamp_ms', 'level', 'file', 'line', 'function', 'event', 'context']
    
    with open(output_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(entries)


def filter_task_events(entries: list[dict]) -> list[dict]:
    """Filter to only TASK level entries."""
    return [e for e in entries if e['level'] == 'TASK']


def print_summary(entries: list[dict]):
    """Print parsing summary."""
    task_events = filter_task_events(entries)
    
    # Count events by task
    task_counts = {}
    for entry in task_events:
        task_name = entry['context']
        task_counts[task_name] = task_counts.get(task_name, 0) + 1
    
    print(f"\n[*] Parsing Summary:")
    print(f"    Total entries: {len(entries)}")
    print(f"    Task events: {len(task_events)}")
    print(f"    Tasks found: {list(task_counts.keys())}")
    
    if task_events:
        first_ts = task_events[0]['timestamp_ms']
        last_ts = task_events[-1]['timestamp_ms']
        print(f"    Time range: {first_ts}ms - {last_ts}ms ({last_ts - first_ts}ms)")


def main():
    parser = argparse.ArgumentParser(description="VRTOS Log Parser")
    parser.add_argument("input", help="Input log file")
    parser.add_argument("-o", "--output", help="Output CSV file (default: <input>_parsed.csv)")
    parser.add_argument("--tasks-only", action="store_true", help="Output only TASK events")
    
    args = parser.parse_args()
    
    # Validate input
    if not os.path.exists(args.input):
        print(f"[!] Input file not found: {args.input}")
        sys.exit(1)
    
    # Generate output path
    if args.output:
        output_path = args.output
    else:
        input_base = os.path.splitext(args.input)[0]
        output_path = f"{input_base}_parsed.csv"
    
    print(f"[*] Parsing: {args.input}")
    
    # Parse log
    entries = parse_log_file(args.input)
    
    if not entries:
        print("[!] No valid log entries found")
        sys.exit(1)
    
    # Filter if requested
    if args.tasks_only:
        entries = filter_task_events(entries)
        print(f"[*] Filtered to task events only")
    
    # Write output
    write_csv(entries, output_path)
    print(f"[+] Wrote {len(entries)} entries to: {output_path}")
    
    # Print summary
    print_summary(parse_log_file(args.input))
    
    print(f"\n[*] Next step: python tools/test/timeline_analyzer.py {output_path} expected_timeline.csv")


if __name__ == "__main__":
    main()
