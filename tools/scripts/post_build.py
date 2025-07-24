# /*******************************************************************************
#  * File: tools/scripts/post_build.py
#  * Description: Post-build Script for RTOS Project
#  * Author: Student
#  * Date: 2025
#  ******************************************************************************/

#!/usr/bin/env python3
"""
Post-build script for RTOS project.

This script performs post-build tasks such as:
- Analyzing binary size
- Generating memory usage report
- Creating documentation
"""

import os
import sys
import subprocess
import re

def analyze_binary_size():
    """Analyze the built binary size."""
    print("Analyzing binary size...")
    
    # Find the ELF file
    elf_file = None
    for root, dirs, files in os.walk(".pio/build"):
        for file in files:
            if file.endswith(".elf"):
                elf_file = os.path.join(root, file)
                break
        if elf_file:
            break
    
    if not elf_file:
        print("Warning: ELF file not found for size analysis")
        return
    
    try:
        # Run size command
        size_output = subprocess.check_output(
            ["arm-none-eabi-size", "-A", elf_file],
            stderr=subprocess.DEVNULL
        ).decode()
        
        print("Binary size analysis:")
        print(size_output)
        
        # Extract key metrics
        text_match = re.search(r'\.text\s+(\d+)', size_output)
        data_match = re.search(r'\.data\s+(\d+)', size_output)
        bss_match = re.search(r'\.bss\s+(\d+)', size_output)
        
        if text_match and data_match and bss_match:
            text_size = int(text_match.group(1))
            data_size = int(data_match.group(1))
            bss_size = int(bss_match.group(1))
            
            flash_usage = text_size + data_size
            ram_usage = data_size + bss_size
            
            print(f"\nMemory Usage Summary:")
            print(f"Flash: {flash_usage} bytes ({flash_usage/1024:.1f} KB)")
            print(f"RAM:   {ram_usage} bytes ({ram_usage/1024:.1f} KB)")
            
            # Check against STM32F446RE limits
            flash_limit = 512 * 1024  # 512KB
            ram_limit = 128 * 1024    # 128KB
            
            flash_percent = (flash_usage / flash_limit) * 100
            ram_percent = (ram_usage / ram_limit) * 100
            
            print(f"Flash usage: {flash_percent:.1f}% of available")
            print(f"RAM usage:   {ram_percent:.1f}% of available")
            
            if flash_percent > 90:
                print("WARNING: Flash usage is very high!")
            if ram_percent > 90:
                print("WARNING: RAM usage is very high!")
                
    except Exception as e:
        print(f"Size analysis failed: {e}")

def generate_memory_map():
    """Generate memory map report."""
    print("Generating memory map...")
    
    # Find the map file
    map_file = None
    for root, dirs, files in os.walk(".pio/build"):
        for file in files:
            if file.endswith(".map"):
                map_file = os.path.join(root, file)
                break
        if map_file:
            break
    
    if map_file and os.path.exists(map_file):
        print(f"Memory map available at: {map_file}")
        # Could parse and summarize the map file here
    else:
        print("Memory map file not found")

def main():
    """Main post-build function."""
    print("=== RTOS Post-build Script ===")
    
    analyze_binary_size()
    generate_memory_map()
    
    print("Post-build analysis completed.")

if __name__ == "__main__":
    main()