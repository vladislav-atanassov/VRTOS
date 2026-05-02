#!/usr/bin/env python3
"""
Pre-build script for KARTOS.

Runs as a PlatformIO extra_script (pre: phase). Two actions:
  1. Preprocess ldscripts/stm32f446re.ld.in → ldscripts/stm32f446re.ld
     so the linker sees memory layout sourced from config/stm32f446re/memory_map.h.
  2. Generate include/rtos/build_info.h with timestamp and git hash.
"""

import os
import subprocess
import datetime

Import("env")  # PlatformIO SCons environment


def generate_linker_script(source, target, env):
    cc = env.get("CC", "arm-none-eabi-gcc")
    project_dir = env.subst("$PROJECT_DIR")

    template = os.path.join(project_dir, "ldscripts", "stm32f446re.ld.in")
    output   = os.path.join(project_dir, "ldscripts", "stm32f446re.ld")
    inc_dir  = os.path.join(project_dir, "config", "stm32f446re")

    cmd = [cc, "-E", "-P", "-x", "c-header",
           "-D", "LINKER_SCRIPT",
           "-I", inc_dir,
           template, "-o", output]

    print(f"Generating linker script: {os.path.relpath(output, project_dir)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ERROR: Linker script generation failed:\n{result.stderr}")
        env.Exit(1)
    print("Linker script generated.")


def generate_build_info(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")

    try:
        git_hash = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL,
            cwd=project_dir
        ).decode().strip()
    except Exception:
        git_hash = "unknown"

    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    content = f"""/*
 * Auto-generated — do not edit.
 * Generated: {now}
 */

#ifndef BUILD_INFO_H
#define BUILD_INFO_H

#define BUILD_DATE_TIME "{now}"
#define BUILD_GIT_HASH  "{git_hash}"
#define BUILD_VERSION   "1.0.0"

#endif /* BUILD_INFO_H */
"""
    out_dir  = os.path.join(project_dir, "include", "rtos")
    out_file = os.path.join(out_dir, "build_info.h")
    os.makedirs(out_dir, exist_ok=True)
    with open(out_file, "w") as f:
        f.write(content)
    print(f"Build info generated (git: {git_hash})")


env.AddPreAction("$BUILD_DIR/firmware.elf", generate_linker_script)
env.AddPreAction("$BUILD_DIR/firmware.elf", generate_build_info)
