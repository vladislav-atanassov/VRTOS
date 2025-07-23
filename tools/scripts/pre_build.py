# tools/scripts/pre_build.py

import os
import shutil

# Choose the example to build
EXAMPLE_MAIN = "examples/basic_blinky/main.c"
DEST_MAIN = "src/main.c"

def before_build():
    print(">>> [pre_build.py] Running pre-build script...")
    print(f"Copying '{EXAMPLE_MAIN}' to '{DEST_MAIN}'...")
    os.makedirs(os.path.dirname(DEST_MAIN), exist_ok=True)
    shutil.copy(EXAMPLE_MAIN, DEST_MAIN)


before_build()
