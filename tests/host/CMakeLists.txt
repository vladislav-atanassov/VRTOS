# Host-side unit tests for VRTOS.
#
# Standalone CMake project (not glued into the root kartos build, which uses
# the arm-none-eabi cross toolchain). Configure with the host compiler:
#
#   cmake --preset host
#   cmake --build --preset host
#   ctest --preset host
#
# Each test executable links Unity + the framework fakes + ONE production .c
# file under test, compiled with `-Dstatic=` so file-scope statics are
# visible to the test. Production .c files are not modified.

cmake_minimum_required(VERSION 3.21)
project(kartos_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

set(KARTOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# ── 1. Unity static library ──────────────────────────────────────────────
add_library(unity STATIC vendor/unity.c)
target_include_directories(unity PUBLIC vendor)
target_compile_options(unity PRIVATE -Wno-everything) # Unity ships with -Wall noise we don't own

# ── 2. Fakes static library ─────────────────────────────────────────────
add_library(host_fakes STATIC fakes/fakes.c)
target_include_directories(host_fakes PUBLIC
    fakes
    vendor                            # fff.h
    config                            # rtos_config.h override
    ${KARTOS_ROOT}/include
    ${KARTOS_ROOT}/src/core           # kernel_priv.h
    ${KARTOS_ROOT}/src/task           # task_priv.h
    ${KARTOS_ROOT}/src/logging        # klog.h
    ${KARTOS_ROOT}/src/utils          # rtos_assert.h
    ${KARTOS_ROOT}/src/sync/queue     # queue_priv.h
    ${KARTOS_ROOT}/src/profiling)
target_compile_definitions(host_fakes PUBLIC
    RTOS_PROFILING_SYSTEM_ENABLED=0)
target_compile_options(host_fakes PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ── 3. Helper: register a test that links one production .c file ────────
#
# add_kartos_host_test(NAME <test_name>
#                     SOURCES <test.c> [<more.c>...]
#                     PRODUCTION <path-to-src.c>
#                     [EXPOSE_STATICS])
#
# EXPOSE_STATICS adds `-Dstatic=` to the production file's compile flags so
# that file-scope static helpers become extern. The production .c is added
# as a per-target source rather than linked from a library so the override
# applies only to its TU.
function(add_kartos_host_test)
    cmake_parse_arguments(_T "EXPOSE_STATICS" "NAME;PRODUCTION" "SOURCES" ${ARGN})

    add_executable(${_T_NAME} ${_T_SOURCES} ${_T_PRODUCTION})

    target_link_libraries(${_T_NAME} PRIVATE unity host_fakes)

    if(_T_EXPOSE_STATICS)
        set_source_files_properties(${_T_PRODUCTION}
            TARGET_DIRECTORY ${_T_NAME}
            PROPERTIES COMPILE_FLAGS "-Dstatic=")
    endif()

    target_compile_options(${_T_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -g)

    add_test(NAME ${_T_NAME} COMMAND ${_T_NAME})
endfunction()

# ── 4. Register the Phase 1 tests ───────────────────────────────────────
add_kartos_host_test(
    NAME       test_mutex_list
    SOURCES    tests/test_mutex_list.c
    PRODUCTION ${KARTOS_ROOT}/src/sync/mutex/mutex.c
    EXPOSE_STATICS)

add_kartos_host_test(
    NAME       test_mutex_pip_walker
    SOURCES    tests/test_mutex_pip_walker.c
    PRODUCTION ${KARTOS_ROOT}/src/sync/mutex/mutex.c
    EXPOSE_STATICS)

add_kartos_host_test(
    NAME       test_queue_ring
    SOURCES    tests/test_queue_ring.c
    PRODUCTION ${KARTOS_ROOT}/src/sync/queue/queue.c)

add_kartos_host_test(
    NAME       test_event_group_match
    SOURCES    tests/test_event_group_match.c
    PRODUCTION ${KARTOS_ROOT}/src/sync/event_group/event_group.c)
