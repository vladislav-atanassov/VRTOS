#!/usr/bin/env python3
"""KARTOS CLI — build, upload, monitor, and test."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports

# Import log_parser from tools/test/ (stays in place per project convention)
_HERE = Path(__file__).parent
sys.path.insert(0, str(_HERE.parent / "test"))
from log_parser import parse_log_file, write_csv  # noqa: E402

DEFAULT_BOARD = "stm32f446re_nucleo"
DEFAULT_BAUD = 921600
DEFAULT_TEST_DURATION_SEC = 20
DEFAULT_OUTPUT_DIR = "tests/artifacts"


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

            if "\tRESULT\t" in line or ",RESULT," in line:
                print("\n[*] RESULT detected — stopping capture.")
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
                        if "\tRESULT\t" in extra_line or ",RESULT," in extra_line:
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


def analyze_verdict(entries: list[dict]) -> tuple[bool, str]:
    lines = ["=" * 50, "VERDICT RESULTS", "=" * 50]

    pass_count = sum(1 for e in entries if e["event"] == "ASSERT_PASS")
    fail_count = sum(1 for e in entries if e["event"] == "ASSERT_FAIL")
    lines.append(f"\nAssertions: {pass_count} passed, {fail_count} failed")

    result_entry = next((e for e in entries if e["event"] == "RESULT"), None)
    if result_entry is None:
        lines += [
            "\n[!] No RESULT line found — test may have timed out without verdict",
            "Result: FAIL (no verdict)",
            "=" * 50,
        ]
        return False, "\n".join(lines)

    verdict = result_entry["context"].strip()
    passed = verdict == "PASS"
    lines.append(f"\nVerdict: {verdict}")
    lines.append(f'Result: {"PASS" if passed else "FAIL"}')
    lines.append("=" * 50)
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

    if not args.skip_upload:
        _ensure_configured(board)
        result = subprocess.run(
            ["cmake", "--build", "--preset", f"flash-{args.environment}"],
            cwd=str(project_dir),
        )
        if result.returncode != 0:
            sys.exit(result.returncode)
        time.sleep(1)

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

    if not capture_serial(port, args.baud_rate, args.duration, log_file):
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

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
