import os
import shutil

def after_build(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    compdb_src = os.path.join(build_dir, "compile_commands.json")
    compdb_dst = os.path.join(env.subst("$PROJECT_DIR"), "compile_commands.json")

    if os.path.isfile(compdb_src):
        shutil.copyfile(compdb_src, compdb_dst)
        print("✔ compile_commands.json copied to project root")
    else:
        print("⚠ No compile_commands.json found.")
