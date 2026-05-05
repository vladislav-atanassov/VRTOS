# Generates ${CMAKE_BINARY_DIR}/generated/rtos/build_info.h before every build.
# Port of tools/scripts/pre_build.py action 2.
#
# The custom target always re-runs the writer script, which in turn only
# rewrites the file when the git hash changes (avoids spurious recompiles).

set(KARTOS_BUILD_INFO_HEADER "${CMAKE_BINARY_DIR}/generated/rtos/build_info.h")

add_custom_target(kartos_build_info
    BYPRODUCTS "${KARTOS_BUILD_INFO_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            "-DOUT_FILE=${KARTOS_BUILD_INFO_HEADER}"
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/kartos_build_info_writer.cmake"
    COMMENT "Generating build_info.h"
    VERBATIM)
