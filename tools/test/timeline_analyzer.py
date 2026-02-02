#!/usr/bin/env python3
"""
VRTOS Timeline Analyzer

Compares actual task execution timeline against expected timeline.
Both timelines are in CSV format for easy matching.

Expected timeline CSV format:
    timestamp_ms,task_name,event
    0,Task1,START
    0,Task2,START
    200,Task1,RUN

Actual timeline CSV format (from log_parser.py):
    timestamp_ms,level,file,line,function,event,context

Usage:
    python timeline_analyzer.py <actual.csv> <expected.csv> [--tolerance 50]
"""

import argparse
import csv
import os
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class TimelineEvent:
    """Single event in timeline."""
    timestamp_ms: int
    task_name: str
    event: str
    
    def matches(self, other: 'TimelineEvent', tolerance_ms: int) -> bool:
        """Check if this event matches another within tolerance."""
        if self.task_name != other.task_name:
            return False
        if self.event != other.event:
            return False
        return abs(self.timestamp_ms - other.timestamp_ms) <= tolerance_ms


def load_expected_timeline(csv_path: str) -> list[TimelineEvent]:
    """Load expected timeline from CSV."""
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


def load_actual_timeline(csv_path: str) -> list[TimelineEvent]:
    """Load actual timeline from parsed log CSV."""
    events = []
    
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Only include TASK events
            if row.get('level') != 'TASK':
                continue
            
            events.append(TimelineEvent(
                timestamp_ms=int(row['timestamp_ms']),
                task_name=row['context'],  # Task name is in context column
                event=row['event']
            ))
    
    return sorted(events, key=lambda e: e.timestamp_ms)


def find_matching_events(actual: list[TimelineEvent], 
                         expected: list[TimelineEvent],
                         tolerance_ms: int) -> tuple[list, list, list]:
    """
    Find matches between actual and expected timelines.
    
    Returns:
        matched: List of (actual, expected, offset_ms) tuples
        missing: Expected events not found in actual
        extra: Actual events not matched to expected
    """
    matched = []
    missing = []
    used_actual = set()
    
    for exp in expected:
        found = False
        for i, act in enumerate(actual):
            if i in used_actual:
                continue
            if act.matches(exp, tolerance_ms):
                offset = act.timestamp_ms - exp.timestamp_ms
                matched.append((act, exp, offset))
                used_actual.add(i)
                found = True
                break
        
        if not found:
            missing.append(exp)
    
    # Find extra actual events
    extra = [act for i, act in enumerate(actual) if i not in used_actual]
    
    return matched, missing, extra


def generate_report(matched: list, missing: list, extra: list,
                    tolerance_ms: int, test_name: str) -> str:
    """Generate test report."""
    lines = []
    lines.append(f"SCHEDULER TEST: {test_name}")
    lines.append("=" * 50)
    lines.append("")
    
    total_expected = len(matched) + len(missing)
    match_count = len(matched)
    
    # Summary
    lines.append(f"RESULT: {'PASS' if len(missing) == 0 else 'FAIL'}")
    lines.append(f"Matched: {match_count}/{total_expected} expected events")
    lines.append(f"Missing: {len(missing)} events")
    lines.append(f"Extra: {len(extra)} events")
    lines.append(f"Tolerance: ±{tolerance_ms}ms")
    lines.append("")
    
    # Matched events
    if matched:
        lines.append("Matched Events:")
        lines.append("-" * 40)
        for act, exp, offset in matched:
            status = "✓" if abs(offset) <= tolerance_ms else "~"
            lines.append(f"  {act.timestamp_ms:8}ms {act.task_name:8} {act.event:8} "
                        f"[expected: {exp.timestamp_ms}ms, offset: {offset:+}ms] {status}")
    
    # Missing events
    if missing:
        lines.append("")
        lines.append("Missing Events (EXPECTED but not found):")
        lines.append("-" * 40)
        for exp in missing:
            lines.append(f"  {exp.timestamp_ms:8}ms {exp.task_name:8} {exp.event:8} ✗")
    
    # Extra events
    if extra:
        lines.append("")
        lines.append("Extra Events (ACTUAL but not expected):")
        lines.append("-" * 40)
        for act in extra:
            lines.append(f"  {act.timestamp_ms:8}ms {act.task_name:8} {act.event:8} ?")
    
    lines.append("")
    lines.append("=" * 50)
    
    return "\n".join(lines)


def save_results_csv(matched: list, missing: list, extra: list, output_path: str):
    """Save detailed results to CSV."""
    with open(output_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['status', 'actual_ts', 'expected_ts', 'offset_ms', 
                        'task_name', 'event'])
        
        for act, exp, offset in matched:
            writer.writerow(['MATCH', act.timestamp_ms, exp.timestamp_ms, 
                           offset, act.task_name, act.event])
        
        for exp in missing:
            writer.writerow(['MISSING', '', exp.timestamp_ms, '', 
                           exp.task_name, exp.event])
        
        for act in extra:
            writer.writerow(['EXTRA', act.timestamp_ms, '', '', 
                           act.task_name, act.event])


def main():
    parser = argparse.ArgumentParser(description="VRTOS Timeline Analyzer")
    parser.add_argument("actual", help="Actual timeline CSV (from log_parser.py)")
    parser.add_argument("expected", help="Expected timeline CSV")
    parser.add_argument("--tolerance", type=int, default=50,
                        help="Timing tolerance in ms (default: 50)")
    parser.add_argument("--output", help="Output results CSV path")
    parser.add_argument("--test-name", default="Scheduler", help="Test name for report")
    
    args = parser.parse_args()
    
    # Validate inputs
    for path in [args.actual, args.expected]:
        if not os.path.exists(path):
            print(f"[!] File not found: {path}")
            sys.exit(1)
    
    print(f"[*] Loading actual timeline: {args.actual}")
    actual = load_actual_timeline(args.actual)
    print(f"    Found {len(actual)} task events")
    
    print(f"[*] Loading expected timeline: {args.expected}")
    expected = load_expected_timeline(args.expected)
    print(f"    Found {len(expected)} expected events")
    
    print(f"[*] Analyzing with tolerance: ±{args.tolerance}ms")
    matched, missing, extra = find_matching_events(actual, expected, args.tolerance)
    
    # Generate and print report
    report = generate_report(matched, missing, extra, args.tolerance, args.test_name)
    print("\n" + report)
    
    # Save results CSV if requested
    if args.output:
        save_results_csv(matched, missing, extra, args.output)
        print(f"[+] Saved results to: {args.output}")
    
    # Exit code: 0 = pass, 1 = fail
    if missing:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
