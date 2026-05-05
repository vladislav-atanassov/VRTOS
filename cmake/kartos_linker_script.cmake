# Preprocesses the board's linker script template into a concrete .ld file.
# Direct CMake port of tools/scripts/pre_build.py action 1.
#
# Inputs (set by board.cmake):
#   KARTOS_LINKER_SCRIPT_TEMPLATE  — path to <board>/linker.ld.in
#   KARTOS_BOARD_INCLUDE_DIR       — directory that contains memory_map.h
#
# Output: ${CMAKE_BINARY_DIR}/linker.ld

set(KARTOS_GENERATED_LINKER_SCRIPT "${CMAKE_BINARY_DIR}/linker.ld")

add_custom_command(
    OUTPUT  "${KARTOS_GENERATED_LINKER_SCRIPT}"
    DEPENDS "${KARTOS_LINKER_SCRIPT_TEMPLATE}"
            "${KARTOS_BOARD_INCLUDE_DIR}/memory_map.h"
    COMMAND "${CMAKE_C_COMPILER}"
            -E -P -x c-header
            -DLINKER_SCRIPT
            "-I${KARTOS_BOARD_INCLUDE_DIR}"
            "${KARTOS_LINKER_SCRIPT_TEMPLATE}"
            -o "${KARTOS_GENERATED_LINKER_SCRIPT}"
    COMMENT "Generating linker script from ${KARTOS_LINKER_SCRIPT_TEMPLATE}"
    VERBATIM)

add_custom_target(kartos_linker_script
    DEPENDS "${KARTOS_GENERATED_LINKER_SCRIPT}")
