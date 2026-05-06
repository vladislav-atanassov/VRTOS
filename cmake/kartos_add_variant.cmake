# add_kartos_variant(name SOURCE <file> [EXTRA_INCLUDES <dir>...]
#                                        [EXTRA_DEFINES <def>...])
#
# Creates an ELF executable target <name> that links the full KARTOS stack
# (kernel + arch + mcu + board), applies the generated linker script, and
# registers a flash-<name> convenience target for OpenOCD upload.
#
# Inputs from board.cmake / CMakeLists.txt:
#   KARTOS_ARCH, KARTOS_MCU_FAMILY, KARTOS_STARTUP_FILE, KARTOS_OPENOCD_CFG

# Resolve OpenOCD: system PATH first, then PlatformIO's bundled copy.
find_program(OPENOCD_EXECUTABLE openocd
    HINTS "$ENV{USERPROFILE}/.platformio/packages/tool-openocd/bin"
          "$ENV{HOME}/.platformio/packages/tool-openocd/bin")
if(NOT OPENOCD_EXECUTABLE)
    message(WARNING "openocd not found — flash-* targets will not work")
    set(OPENOCD_EXECUTABLE openocd)
endif()

function(add_kartos_variant name)
    cmake_parse_arguments(_V "" "SOURCE" "EXTRA_INCLUDES;EXTRA_DEFINES" ${ARGN})

    add_executable(${name}
        "${_V_SOURCE}"
        "${KARTOS_STARTUP_FILE}")

    target_link_libraries(${name} PRIVATE
        kartos::kernel
        kartos::board_support
        kartos::arch_${KARTOS_ARCH}
        kartos::mcu_${KARTOS_MCU_FAMILY}
        kartos::board)

    target_include_directories(${name} PRIVATE
        ${_V_EXTRA_INCLUDES}
        "${CMAKE_BINARY_DIR}/generated")

    if(_V_EXTRA_DEFINES)
        target_compile_definitions(${name} PRIVATE ${_V_EXTRA_DEFINES})
    endif()

    target_compile_options(${name} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -O2 -g3)

    target_link_options(${name} PRIVATE
        "-T${CMAKE_BINARY_DIR}/linker.ld"
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,--no-warn-rwx-segments
        -specs=nano.specs
        -specs=nosys.specs
        "-Wl,-Map=${CMAKE_BINARY_DIR}/${name}.map")

    set_target_properties(${name} PROPERTIES
        SUFFIX ".elf"
        LINK_DEPENDS "${CMAKE_BINARY_DIR}/linker.ld")

    add_dependencies(${name} kartos_linker_script kartos_build_info)

    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:${name}>"
            "$<TARGET_FILE_DIR:${name}>/${name}.bin"
        COMMENT "Generating ${name}.bin"
        VERBATIM)

    add_custom_target(flash-${name}
        COMMAND "${OPENOCD_EXECUTABLE}"
            -f "${KARTOS_OPENOCD_CFG}"
            -c "program $<TARGET_FILE:${name}> verify reset exit"
        DEPENDS ${name}
        COMMENT "Flashing ${name} via OpenOCD"
        VERBATIM)
endfunction()
