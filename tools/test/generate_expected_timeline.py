#!/usr/bin/env python3
"""
VRTOS Expected Timeline Generator

Auto-extracts task parameters from test source files and simulates
scheduling to produce expected timeline CSV files.

Usage:
    python generate_expected_timeline.py rr
    python generate_expected_timeline.py preemptive
    python generate_expected_timeline.py cooperative

    # Manual override (fallback):
    python generate_expected_timeline.py rr --delays 200 300 400 --iterations 15 10 8 --priorities 2 2 2
"""

import argparse
import csv
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# Project layout relative to this script (tools/test/)
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
TESTS_DIR = PROJECT_ROOT / "tests" / "scheduler"

# Scheduler type to directory mapping
SCHEDULER_DIRS = {
    "rr": "round_robin",
    "preemptive": "preemptive",
    "cooperative": "cooperative",
}


# =================== Parameter Extraction ===================


def extract_defines(file_path: Path, pattern: str) -> dict[str, int]:
    """Extract #define values matching a pattern from a C header/source file."""
    defines = {}
    regex = re.compile(rf"#define\s+({pattern})\s+\((\d+)U?\)")

    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            match = regex.search(line)
            if match:
                defines[match.group(1)] = int(match.group(2))

    return defines


def extract_task_params(test_dir: Path) -> tuple[list[int], list[int], list[int]]:
    """
    Extract task delays, iterations, and priorities from test source files.

    Reads test_config.h for delays/iterations and the .c file for priorities.
    Returns (delays, iterations, priorities) as lists ordered by task number.
    """
    config_path = test_dir / "test_config.h"
    if not config_path.exists():
        raise FileNotFoundError(f"test_config.h not found in {test_dir}")

    # Extract delays and iterations from test_config.h
    config_defines = extract_defines(config_path, r"TEST_TASK\d+_(?:DELAY_MS|ITERATIONS)")

    delays = []
    iterations = []
    for i in range(1, 10):  # Support up to 9 tasks
        delay_key = f"TEST_TASK{i}_DELAY_MS"
        iter_key = f"TEST_TASK{i}_ITERATIONS"
        if delay_key in config_defines and iter_key in config_defines:
            delays.append(config_defines[delay_key])
            iterations.append(config_defines[iter_key])
        else:
            break

    if not delays:
        raise ValueError(f"No task parameters found in {config_path}")

    # Extract priorities from .c test file
    c_files = list(test_dir.glob("test_scheduler_*.c"))
    if not c_files:
        raise FileNotFoundError(f"No test_scheduler_*.c file found in {test_dir}")

    priority_defines = extract_defines(c_files[0], r"TASK\d+_PRIORITY")

    priorities = []
    for i in range(1, len(delays) + 1):
        key = f"TASK{i}_PRIORITY"
        if key in priority_defines:
            priorities.append(priority_defines[key])
        else:
            raise ValueError(f"{key} not found in {c_files[0]}")

    return delays, iterations, priorities


def extract_startup_hold(test_dir: Path) -> int:
    """
    Extract TEST_STARTUP_HOLD_MS from test_common.h.

    This is the offset applied to all timestamps since the RTOS tick counter
    starts at scheduler launch, but tasks only begin after the hold.
    """
    common_path = test_dir.parent / "test_common.h"
    if not common_path.exists():
        raise FileNotFoundError(f"test_common.h not found at {common_path}")

    defines = extract_defines(common_path, r"TEST_STARTUP_HOLD_MS")
    if "TEST_STARTUP_HOLD_MS" not in defines:
        raise ValueError(f"TEST_STARTUP_HOLD_MS not found in {common_path}")

    return defines["TEST_STARTUP_HOLD_MS"]


# =================== Scheduler Simulation ===================


@dataclass
class TaskState:
    """Runtime state of a simulated task."""
    name: str
    priority: int
    delay_ms: int
    max_iterations: int
    iteration: int = 0
    wake_time: int = 0
    started: bool = False
    finished: bool = False
    events: list = field(default_factory=list)


def simulate_round_robin(delays: list[int], iterations: list[int],
                         priorities: list[int]) -> list[dict]:
    """
    Simulate round-robin scheduling.

    All tasks have equal priority and cycle in creation order.
    At each decision point, all ready tasks execute RUN+DELAY in order.
    """
    num_tasks = len(delays)
    tasks = [
        TaskState(
            name=f"Task{i+1}",
            priority=priorities[i],
            delay_ms=delays[i],
            max_iterations=iterations[i],
        )
        for i in range(num_tasks)
    ]

    timeline = []

    # t=0: All tasks start (START + first RUN + DELAY)
    t = 0
    for task in tasks:
        task.started = True
        timeline.append({"timestamp_ms": t, "task_name": task.name, "event": "START"})
        timeline.append({"timestamp_ms": t, "task_name": task.name, "event": "RUN"})
        timeline.append({"timestamp_ms": t, "task_name": task.name, "event": "DELAY"})
        task.iteration = 1
        task.wake_time = t + task.delay_ms

    # Simulation loop: advance to next wakeup time
    while True:
        # Find next wake time among non-finished tasks
        active = [t for t in tasks if not t.finished]
        if not active:
            break

        next_time = min(t.wake_time for t in active)

        # Collect all tasks ready at next_time (round-robin: creation order)
        ready = [t for t in active if t.wake_time <= next_time]

        for task in ready:
            if task.iteration >= task.max_iterations:
                task.finished = True
                continue

            timeline.append({"timestamp_ms": next_time, "task_name": task.name, "event": "RUN"})
            timeline.append({"timestamp_ms": next_time, "task_name": task.name, "event": "DELAY"})
            task.iteration += 1
            task.wake_time = task.wake_time + task.delay_ms

    return timeline


def simulate_preemptive(delays: list[int], iterations: list[int],
                        priorities: list[int]) -> list[dict]:
    """
    Simulate preemptive static-priority scheduling.

    Higher priority tasks run first when multiple are ready.
    Tasks are sorted by priority (descending) at each decision point.
    """
    num_tasks = len(delays)
    tasks = [
        TaskState(
            name=f"Task{i+1}",
            priority=priorities[i],
            delay_ms=delays[i],
            max_iterations=iterations[i],
        )
        for i in range(num_tasks)
    ]

    timeline = []

    # t=0: Tasks start in priority order (highest first)
    t = 0
    priority_order = sorted(tasks, key=lambda t: t.priority, reverse=True)
    for task in priority_order:
        task.started = True
        timeline.append({"timestamp_ms": t, "task_name": task.name, "event": "START"})
        timeline.append({"timestamp_ms": t, "task_name": task.name, "event": "RUN"})
        timeline.append({"timestamp_ms": t, "task_name": task.name, "event": "DELAY"})
        task.iteration = 1
        task.wake_time = t + task.delay_ms

    # Simulation loop
    while True:
        active = [t for t in tasks if not t.finished]
        if not active:
            break

        next_time = min(t.wake_time for t in active)
        ready = [t for t in active if t.wake_time <= next_time]

        # Sort by priority (highest first)
        ready.sort(key=lambda t: t.priority, reverse=True)

        for task in ready:
            if task.iteration >= task.max_iterations:
                task.finished = True
                continue

            timeline.append({"timestamp_ms": next_time, "task_name": task.name, "event": "RUN"})
            timeline.append({"timestamp_ms": next_time, "task_name": task.name, "event": "DELAY"})
            task.iteration += 1
            task.wake_time = task.wake_time + task.delay_ms

    return timeline


def simulate_cooperative(delays: list[int], iterations: list[int],
                         priorities: list[int]) -> list[dict]:
    """
    Simulate cooperative scheduling.

    Same ordering as preemptive (priority-based) since all tasks
    yield via delay — no mid-execution preemption difference.
    """
    return simulate_preemptive(delays, iterations, priorities)


SIMULATORS = {
    "rr": simulate_round_robin,
    "preemptive": simulate_preemptive,
    "cooperative": simulate_cooperative,
}


# =================== Output ===================


def write_timeline_csv(timeline: list[dict], output_path: Path):
    """Write timeline events to CSV."""
    with open(output_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["timestamp_ms", "task_name", "event"])
        writer.writeheader()
        writer.writerows(timeline)


# =================== Main ===================


def main():
    parser = argparse.ArgumentParser(
        description="Generate expected timeline for VRTOS scheduler tests",
        epilog="Example: python generate_expected_timeline.py rr",
    )
    parser.add_argument(
        "scheduler",
        choices=list(SCHEDULER_DIRS.keys()),
        help="Scheduler type (rr, preemptive, cooperative)",
    )
    parser.add_argument(
        "--delays",
        type=int,
        nargs="+",
        help="Manual override: task delay values in ms",
    )
    parser.add_argument(
        "--iterations",
        type=int,
        nargs="+",
        help="Manual override: task iteration counts",
    )
    parser.add_argument(
        "--priorities",
        type=int,
        nargs="+",
        help="Manual override: task priorities",
    )
    parser.add_argument(
        "--output",
        type=str,
        help="Output CSV path (default: test directory/expected_timeline.csv)",
    )

    args = parser.parse_args()

    # Resolve test directory
    test_dir = TESTS_DIR / SCHEDULER_DIRS[args.scheduler]
    if not test_dir.exists():
        print(f"[!] Test directory not found: {test_dir}")
        sys.exit(1)

    # Extract or use manual parameters
    if args.delays and args.iterations and args.priorities:
        print("[*] Using manual parameters")
        delays = args.delays
        iterations = args.iterations
        priorities = args.priorities
    else:
        print(f"[*] Auto-extracting parameters from {test_dir}")
        try:
            delays, iterations, priorities = extract_task_params(test_dir)
        except (FileNotFoundError, ValueError) as e:
            print(f"[!] Extraction failed: {e}")
            print("[!] Use --delays, --iterations, --priorities for manual override")
            sys.exit(1)

    # Validate
    if not (len(delays) == len(iterations) == len(priorities)):
        print("[!] Mismatched parameter counts")
        sys.exit(1)

    print(f"[*] Tasks: {len(delays)}")
    for i in range(len(delays)):
        print(f"    Task{i+1}: delay={delays[i]}ms, iterations={iterations[i]}, priority={priorities[i]}")

    # Extract startup hold offset
    try:
        startup_hold_ms = extract_startup_hold(test_dir)
    except (FileNotFoundError, ValueError) as e:
        print(f"[!] Could not extract startup hold: {e}")
        print("[!] Defaulting to 0ms offset")
        startup_hold_ms = 0

    print(f"[*] Startup hold offset: {startup_hold_ms}ms")

    # Simulate
    print(f"[*] Simulating {args.scheduler} scheduler...")
    simulator = SIMULATORS[args.scheduler]
    timeline = simulator(delays, iterations, priorities)

    # Apply startup hold offset to all timestamps
    for event in timeline:
        event["timestamp_ms"] += startup_hold_ms

    print(f"[+] Generated {len(timeline)} events")

    # Output
    if args.output:
        output_path = Path(args.output)
    else:
        output_path = test_dir / "expected_timeline.csv"

    write_timeline_csv(timeline, output_path)
    print(f"[+] Wrote: {output_path}")


if __name__ == "__main__":
    main()
