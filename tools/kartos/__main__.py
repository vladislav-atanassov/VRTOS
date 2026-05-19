#!/usr/bin/env python3
"""KARTOS CLI — build, upload, monitor, and test."""

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports

_HERE = Path(__file__).parent

# Tab-delimited log lines emitted by the test framework:
#   <tick>\t<level>\t<file>\t<line>\t<func>\t<event>\t<context>
_LOG_PATTERN = re.compile(r"^(\d+)\t(\w+)\t([^\t]+)\t(\d+)\t([^\t]+)\t([^\t]+)\t(.+)$")
_CSV_FIELDS = ["timestamp_ms", "level", "file", "line", "function", "event", "context"]


def _parse_log_line(line: str) -> dict | None:
    match = _LOG_PATTERN.match(line.strip())
    if not match:
        return None
    return {
        "timestamp_ms": int(match.group(1)),
        "level": match.group(2),
        "file": os.path.basename(match.group(3)),
        "line": int(match.group(4)),
        "function": match.group(5),
        "event": match.group(6),
        "context": match.group(7),
    }


def parse_log_file(input_path: str) -> list[dict]:
    entries: list[dict] = []
    with open(input_path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            entry = _parse_log_line(raw)
            if entry:
                entries.append(entry)
    return entries


def write_csv(entries: list[dict], output_path: str) -> None:
    if not entries:
        return
    with open(output_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=_CSV_FIELDS)
        writer.writeheader()
        writer.writerows(entries)


DEFAULT_BOARD = "stm32f446re_nucleo"
DEFAULT_BAUD = 921600
DEFAULT_TEST_DURATION_SEC = 20
DEFAULT_OUTPUT_DIR = "tests/artifacts"

# Mirrors klog_level_t in src/logging/klog.h and KLOG_NOINIT_MAGIC in klog.c.
# Writing the magic alongside the new value keeps klog_init() from rejecting it
# on the next boot (it would only reset if magic is wrong or level > TRACE).
KLOG_LEVELS = {
    "FAULT": 0, "ERROR": 1, "WARN": 2,
    "INFO": 3, "DEBUG": 4, "TRACE": 5,
}
KLOG_NOINIT_MAGIC = 0xB007CA11


def _project_root() -> Path:
    return _HERE.parent.parent


def _resolve_board(args_board: str | None) -> str:
    if args_board:
        return args_board
    if env := os.environ.get("KARTOS_BOARD"):
        return env
    rc_path = _project_root() / ".kartosrc"
    if rc_path.exists():
        for line in rc_path.read_text().splitlines():
            line = line.strip()
            if line.startswith("board="):
                return line.split("=", 1)[1].strip()
    return DEFAULT_BOARD


def _build_dir(board: str) -> Path:
    return _project_root() / "build" / board


def _find_openocd() -> str:
    if path := shutil.which("openocd"):
        return path
    for env_var in ("USERPROFILE", "HOME"):
        if base := os.environ.get(env_var):
            for exe in ("openocd.exe", "openocd"):
                candidate = Path(base) / ".platformio/packages/tool-openocd/bin" / exe
                if candidate.exists():
                    return str(candidate)
    return "openocd"


def _resolve_elf(board: str, environment: str | None) -> Path:
    build_dir = _build_dir(board)
    if environment:
        elf = build_dir / f"{environment}.elf"
        if not elf.exists():
            print(f"[!] ELF not found: {elf}")
            sys.exit(1)
        return elf
    if not build_dir.exists():
        print(f"[!] Build dir not found: {build_dir}. Build a variant first.")
        sys.exit(1)
    elfs = sorted(build_dir.glob("*.elf"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not elfs:
        print(f"[!] No .elf files under {build_dir}. Build a variant first.")
        sys.exit(1)
    return elfs[0]


def _resolve_symbol(elf: Path, name: str) -> int:
    try:
        result = subprocess.run(
            ["arm-none-eabi-nm", str(elf)],
            capture_output=True, text=True, check=False,
        )
    except FileNotFoundError:
        print("[!] arm-none-eabi-nm not found on PATH. Install the ARM toolchain.")
        sys.exit(1)
    if result.returncode != 0:
        print(f"[!] arm-none-eabi-nm failed: {result.stderr.strip()}")
        sys.exit(1)
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    print(f"[!] Symbol '{name}' not found in {elf.name}")
    sys.exit(1)


def _parse_level(s: str) -> int:
    s = s.strip().upper()
    if s in KLOG_LEVELS:
        return KLOG_LEVELS[s]
    try:
        n = int(s)
    except ValueError:
        print(f"[!] Unknown level '{s}'. Use {'|'.join(KLOG_LEVELS)} or 0-5.")
        sys.exit(1)
    if not 0 <= n <= 5:
        print(f"[!] Level out of range: {n}. Must be 0-5.")
        sys.exit(1)
    return n


def _ensure_configured(board: str):
    if not _build_dir(board).exists():
        print(f"[*] Build dir not found — configuring for board '{board}'...")
        result = subprocess.run(["cmake", "--preset", board], cwd=str(_project_root()))
        if result.returncode != 0:
            sys.exit(result.returncode)


def find_serial_port() -> str | None:
    for port in serial.tools.list_ports.comports():
        desc = (port.description or "").lower()
        mfr = (port.manufacturer or "").lower()
        if "stlink" in desc or "stm32" in desc or "stlink" in mfr or "stm" in mfr:
            return port.device
    ports = serial.tools.list_ports.comports()
    return ports[0].device if len(ports) == 1 else None


def capture_serial(port: str, baud: int, duration_sec: int, output_file: str) -> bool:
    print(f"[*] Capturing serial on {port} at {baud} baud (max {duration_sec}s)...")
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
            line = raw.decode("utf-8", errors="replace")
            output_lines.append(line)
            print(line, end="", flush=True)

            if "\tSUITE_RESULT\t" in line or ",SUITE_RESULT," in line:
                print("\n[*] SUITE_RESULT detected — stopping capture.")
                test_complete = True
                break

            if "\tTIMEOUT\t" in line or ",TIMEOUT," in line:
                print("\n[*] TIMEOUT detected — waiting for verdict (5s)...")
                verdict_deadline = time.monotonic() + 5.0
                while time.monotonic() < verdict_deadline:
                    try:
                        extra = ser.readline()
                    except serial.SerialException:
                        break
                    if extra:
                        extra_line = extra.decode("utf-8", errors="replace")
                        output_lines.append(extra_line)
                        print(extra_line, end="", flush=True)
                        if "\tSUITE_RESULT\t" in extra_line or ",SUITE_RESULT," in extra_line:
                            break
                test_complete = True
                break
    finally:
        ser.close()

    with open(output_file, "w", encoding="utf-8") as f:
        f.writelines(output_lines)

    status = "complete" if test_complete else f"captured {duration_sec}s"
    print(f"\n[+] Captured {len(output_lines)} lines ({status})")
    return True


def _parse_case_result(ctx: str) -> dict:
    """Decode the tab-delimited body of a CASE_RESULT log entry.

    The body produced by tests/framework/test_invariants.c looks like:
        <suite>:<case>:VERDICT \t INV-A:p/c;INV-B:p/c \t failed=<csv>
    where the trailing 'failed=' may be SKIP:<reason>, HARD_FAIL[,IDs], or empty.
    """
    parts = ctx.split("\t")
    head = parts[0] if parts else ""
    suite, _, rest = head.partition(":")
    case, _, verdict = rest.partition(":")

    inv_field = parts[1] if len(parts) > 1 else "-"
    invariants: list[tuple[str, int, int]] = []
    if inv_field and inv_field != "-":
        for tok in inv_field.split(";"):
            tok = tok.strip()
            if not tok:
                continue
            inv_id, _, counts = tok.rpartition(":")
            passed_s, _, checked_s = counts.partition("/")
            try:
                invariants.append((inv_id, int(passed_s), int(checked_s)))
            except ValueError:
                continue

    failed_field = parts[2] if len(parts) > 2 else ""
    if failed_field.startswith("failed="):
        failed_field = failed_field[len("failed="):]
    failed_field = failed_field.strip()

    return {
        "suite": suite, "case": case, "verdict": verdict,
        "invariants": invariants, "failed": failed_field,
    }


def _parse_suite_result(ctx: str) -> dict:
    """Decode the tab-delimited body of a SUITE_RESULT log entry.

    Body shape: <suite>:VERDICT \t cases=N;pass=P;fail=F;skip=S
    """
    parts = ctx.split("\t")
    head = parts[0] if parts else ""
    suite, _, verdict = head.partition(":")

    stats = {"cases": 0, "pass": 0, "fail": 0, "skip": 0}
    if len(parts) > 1:
        for tok in parts[1].split(";"):
            key, _, val = tok.partition("=")
            try:
                stats[key.strip()] = int(val)
            except ValueError:
                continue

    return {"suite": suite, "verdict": verdict, **stats}


def analyze_verdict(entries: list[dict]) -> tuple[bool, str]:
    lines = ["=" * 60, "VERDICT RESULTS", "=" * 60]

    cases = [_parse_case_result(e["context"]) for e in entries if e["event"] == "CASE_RESULT"]
    suite = next((_parse_suite_result(e["context"]) for e in entries if e["event"] == "SUITE_RESULT"), None)

    if cases:
        labels = [f'{c["suite"]}:{c["case"]}' for c in cases]
        width = max((len(lbl) for lbl in labels), default=20)
        lines.append("")
        lines.append(f'{"CASE".ljust(width)}  VERDICT  INVARIANTS')
        lines.append("-" * 60)
        for c, label in zip(cases, labels):
            inv_summary = ";".join(f"{i}:{p}/{ch}" for (i, p, ch) in c["invariants"]) or "-"
            lines.append(f'{label.ljust(width)}  {c["verdict"]:<7}  {inv_summary}')
            if c["failed"]:
                lines.append(f'{" " * width}           failed={c["failed"]}')

    if suite is None:
        lines += [
            "",
            "[!] No SUITE_RESULT line — capture likely truncated mid-suite",
            "Result: FAIL (no suite verdict)",
            "=" * 60,
        ]
        return False, "\n".join(lines)

    passed = suite["verdict"] == "PASS" and suite["fail"] == 0
    lines.append("")
    lines.append(
        f'Suite {suite["suite"]}: {suite["verdict"]}  '
        f'(cases={suite["cases"]} pass={suite["pass"]} fail={suite["fail"]} skip={suite["skip"]})'
    )
    lines.append(f'Result: {"PASS" if passed else "FAIL"}')
    lines.append("=" * 60)
    return passed, "\n".join(lines)


# ---- Subcommand handlers ----

def cmd_build(args):
    board = _resolve_board(args.board)
    _ensure_configured(board)
    result = subprocess.run(
        ["cmake", "--build", "--preset", args.environment],
        cwd=str(_project_root()),
    )
    sys.exit(result.returncode)


def cmd_upload(args):
    board = _resolve_board(args.board)
    _ensure_configured(board)
    result = subprocess.run(
        ["cmake", "--build", "--preset", f"flash-{args.environment}"],
        cwd=str(_project_root()),
    )
    sys.exit(result.returncode)


def cmd_monitor(args):
    port = args.port or find_serial_port()
    if not port:
        print("[!] No serial port found. Specify -p/--port (e.g. -p COM3).")
        sys.exit(1)
    baud = args.baud_rate
    print(f"[*] Opening serial monitor on {port} at {baud} baud. Press Ctrl+C to exit.")
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"[!] Cannot open {port}: {e}")
        sys.exit(1)
    try:
        while True:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"[!] Serial read error: {e}")
                break
            if raw:
                print(raw.decode("utf-8", errors="replace"), end="", flush=True)
    except KeyboardInterrupt:
        print("\n[*] Monitor closed.")
    finally:
        ser.close()


def cmd_test(args):
    board = _resolve_board(args.board)
    project_dir = _project_root()

    port = args.port or find_serial_port()
    if not port:
        print("[!] No serial port found. Specify -p/--port.")
        sys.exit(1)

    output_dir = project_dir / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = str(output_dir / f"log_{args.environment}_{timestamp}.txt")
    csv_file = str(output_dir / f"log_{args.environment}_{timestamp}.csv")

    print("=" * 50)
    print(f"KARTOS TEST: {args.environment}")
    print("=" * 50)

    # Start capturing BEFORE flashing so we don't miss output from fast-completing
    # test suites (semaphore suite finishes in ~30 ms; OpenOCD takes several seconds).
    capture_ok = [False]
    capture_done = threading.Event()

    def _capture():
        capture_ok[0] = capture_serial(port, args.baud_rate, args.duration, log_file)
        capture_done.set()

    capture_thread = threading.Thread(target=_capture, daemon=True)
    capture_thread.start()

    if not args.skip_upload:
        _ensure_configured(board)
        result = subprocess.run(
            ["cmake", "--build", "--preset", f"flash-{args.environment}"],
            cwd=str(project_dir),
        )
        if result.returncode != 0:
            capture_thread.join(timeout=2)
            sys.exit(result.returncode)

    capture_done.wait()

    if not capture_ok[0]:
        sys.exit(1)

    print("\n[*] Parsing log file...")
    entries = parse_log_file(log_file)
    print(f"[+] Parsed {len(entries)} entries")
    if entries:
        write_csv(entries, csv_file)
        print(f"[+] Saved CSV: {csv_file}")

    if not args.skip_analysis:
        print("\n[*] Analyzing verdict...")
        passed, report = analyze_verdict(entries)
        print(report)
        if not passed:
            sys.exit(1)

    print("\n[+] Test complete!")


def cmd_configure(args):
    board = _resolve_board(args.board)
    result = subprocess.run(["cmake", "--preset", board], cwd=str(_project_root()))
    sys.exit(result.returncode)


def cmd_list(args):
    presets_path = _project_root() / "CMakePresets.json"
    if not presets_path.exists():
        print("[!] CMakePresets.json not found.")
        sys.exit(1)
    data = json.loads(presets_path.read_text())

    boards = [p["name"] for p in data.get("configurePresets", []) if not p.get("hidden")]
    build_presets = [p["name"] for p in data.get("buildPresets", [])]
    variants = [n for n in build_presets if not n.startswith("flash-")]

    print("Boards:")
    for b in boards:
        print(f"  {b}")
    print("\nVariants:")
    for v in variants:
        print(f"  {v}")


def cmd_verbosity(args):
    board = _resolve_board(args.board)
    level = _parse_level(args.level)
    elf = _resolve_elf(board, args.environment)

    verbosity_addr = _resolve_symbol(elf, "klog_verbosity")
    magic_addr = _resolve_symbol(elf, "klog_noinit_magic")

    openocd_cfg = _project_root() / "boards" / board / "openocd.cfg"
    if not openocd_cfg.exists():
        print(f"[!] OpenOCD config not found: {openocd_cfg}")
        sys.exit(1)

    level_name = next(k for k, v in KLOG_LEVELS.items() if v == level)
    rel_elf = elf.relative_to(_project_root())
    print(f"[*] ELF: {rel_elf}")
    print(f"[*] klog_verbosity     @ 0x{verbosity_addr:08x} -> {level} ({level_name})")
    print(f"[*] klog_noinit_magic  @ 0x{magic_addr:08x} -> 0x{KLOG_NOINIT_MAGIC:08x}")
    print(f"[*] {'Soft-reset after write' if not args.no_reset else 'Resume without reset (live change)'}")

    ocd_cmds = [
        "init",
        "halt",
        f"mwb 0x{verbosity_addr:08x} {level}",
        f"mww 0x{magic_addr:08x} 0x{KLOG_NOINIT_MAGIC:08x}",
        "reset run" if not args.no_reset else "resume",
        "exit",
    ]
    cmd = [_find_openocd(), "-f", str(openocd_cfg)]
    for c in ocd_cmds:
        cmd += ["-c", c]
    result = subprocess.run(cmd, cwd=str(_project_root()))
    sys.exit(result.returncode)


def cmd_clean(args):
    board = _resolve_board(args.board)
    build_dir = _build_dir(board)
    if not build_dir.exists():
        print(f"[*] Nothing to clean: {build_dir} does not exist.")
        return
    print(f"[*] Removing {build_dir}...")
    shutil.rmtree(build_dir)
    print("[+] Done.")


# ---- Entry point ----

def main():
    parser = argparse.ArgumentParser(
        prog="kartos",
        description="KARTOS build, upload, monitor, and test CLI",
    )
    parser.add_argument(
        "--board",
        default=None,
        help="Board name (default: stm32f446re_nucleo, or KARTOS_BOARD env, or .kartosrc)",
    )

    sub = parser.add_subparsers(dest="command", required=True)

    p_build = sub.add_parser("build", help="Build a variant")
    p_build.add_argument("-e", "--environment", required=True, metavar="VARIANT")
    p_build.set_defaults(func=cmd_build)

    p_upload = sub.add_parser("upload", help="Build and flash a variant")
    p_upload.add_argument("-e", "--environment", required=True, metavar="VARIANT")
    p_upload.set_defaults(func=cmd_upload)

    p_monitor = sub.add_parser("monitor", help="Open serial monitor")
    p_monitor.add_argument("-p", "--port", default=None, help="Serial port (auto-detected if omitted)")
    p_monitor.add_argument("-b", "--baud-rate", type=int, default=DEFAULT_BAUD,
                           help=f"Baud rate (default: {DEFAULT_BAUD})")
    p_monitor.set_defaults(func=cmd_monitor)

    p_test = sub.add_parser("test", help="Flash, capture, parse, and analyze a test variant")
    p_test.add_argument("-e", "--environment", required=True, metavar="VARIANT")
    p_test.add_argument("-p", "--port", default=None, help="Serial port (auto-detected if omitted)")
    p_test.add_argument("-b", "--baud-rate", type=int, default=DEFAULT_BAUD,
                        help=f"Baud rate (default: {DEFAULT_BAUD})")
    p_test.add_argument("--duration", type=int, default=DEFAULT_TEST_DURATION_SEC,
                        help=f"Max capture duration in seconds (default: {DEFAULT_TEST_DURATION_SEC})")
    p_test.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR,
                        help="Output directory for logs and CSV")
    p_test.add_argument("--skip-upload", action="store_true",
                        help="Skip flashing — capture from already-running firmware")
    p_test.add_argument("--skip-analysis", action="store_true", help="Skip verdict analysis")
    p_test.set_defaults(func=cmd_test)

    p_configure = sub.add_parser("configure", help="Run cmake --preset for a board")
    p_configure.set_defaults(func=cmd_configure)

    p_list = sub.add_parser("list", help="List available boards and variants")
    p_list.set_defaults(func=cmd_list)

    p_clean = sub.add_parser("clean", help="Remove a board's build directory")
    p_clean.set_defaults(func=cmd_clean)

    p_verbosity = sub.add_parser(
        "verbosity",
        help="Set klog runtime verbosity in .noinit RAM (survives soft reset)",
    )
    p_verbosity.add_argument(
        "level",
        help=f"Level: {'|'.join(KLOG_LEVELS)} or 0-5",
    )
    p_verbosity.add_argument(
        "-e", "--environment", default=None, metavar="VARIANT",
        help="Variant ELF for symbol resolution (default: most-recently-built .elf in build dir)",
    )
    p_verbosity.add_argument(
        "--no-reset", action="store_true",
        help="Write the value and resume; skip the soft reset (takes effect live since klog_verbosity is volatile)",
    )
    p_verbosity.set_defaults(func=cmd_verbosity)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
