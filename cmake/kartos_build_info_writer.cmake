# Invoked with: cmake -DOUT_FILE=<path> -DSOURCE_DIR=<path> -P <this file>
# Only writes the output when the git hash has changed, preventing spurious
# downstream recompilation.

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _git_result)

if(NOT _git_result EQUAL 0)
    set(GIT_HASH "unknown")
endif()

string(TIMESTAMP BUILD_DATETIME "%Y-%m-%d %H:%M:%S")

set(_new_content
"/*
 * Auto-generated -- do not edit.
 * Generated: ${BUILD_DATETIME}
 */

#ifndef BUILD_INFO_H
#define BUILD_INFO_H

#define BUILD_DATE_TIME \"${BUILD_DATETIME}\"
#define BUILD_GIT_HASH  \"${GIT_HASH}\"
#define BUILD_VERSION   \"1.0.0\"

#endif /* BUILD_INFO_H */
")

# Skip rewrite if only the timestamp changed (git hash is same).
if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" _existing)
    string(REGEX MATCH "BUILD_GIT_HASH  \"([^\"]+)\"" _ "${_existing}")
    set(_old_hash "${CMAKE_MATCH_1}")
    if(_old_hash STREQUAL GIT_HASH)
        return()
    endif()
endif()

get_filename_component(_out_dir "${OUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")
file(WRITE "${OUT_FILE}" "${_new_content}")
message(STATUS "build_info.h updated (git: ${GIT_HASH})")
