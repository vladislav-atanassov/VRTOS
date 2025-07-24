# /*******************************************************************************
#  * File: tools/scripts/pre_build.py
#  * Description: Pre-build Script for RTOS Project
#  * Author: Student
#  * Date: 2025
#  ******************************************************************************/

#!/usr/bin/env python3
"""
Pre-build script for RTOS project.

This script performs pre-build tasks such as:
- Validating configuration
- Generating build information
- Checking dependencies
"""

import os
import sys
import datetime
import subprocess

def validate_config():
    """Validate RTOS configuration parameters."""
    print("Validating RTOS configuration...")
    
    # Check if required files exist
    required_files = [
        "include/rtos/config.h",
        "config/stm32f446re/rtos_config.h",
        "src/core/kernel.c",
        "port/cortex_m4/port.c"
    ]
    
    for file_path in required_files:
        if not os.path.exists(file_path):
            print(f"ERROR: Required file missing: {file_path}")
            return False
    
    print("Configuration validation passed.")
    return True

def generate_build_info():
    """Generate build information header."""
    print("Generating build information...")
    
    try:
        # Get git commit hash if available
        git_hash = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except:
        git_hash = "unknown"
    
    # Generate build info header
    build_info_header = f"""/*
 * Auto-generated build information
 * Generated on: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
 */

#ifndef BUILD_INFO_H
#define BUILD_INFO_H

#define BUILD_DATE_TIME     "{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
#define BUILD_GIT_HASH      "{git_hash}"
#define BUILD_VERSION       "1.0.0-MVP"

#endif /* BUILD_INFO_H */
"""
    
    # Write to include directory
    os.makedirs("include/rtos", exist_ok=True)
    with open("include/rtos/build_info.h", "w") as f:
        f.write(build_info_header)
    
    print(f"Build info generated (Git: {git_hash})")

def main():
    """Main pre-build function."""
    print("=== RTOS Pre-build Script ===")
    
    if not validate_config():
        sys.exit(1)
    
    generate_build_info()
    
    print("Pre-build tasks completed successfully.")

if __name__ == "__main__":
    main()