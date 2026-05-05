set(KARTOS_ARCH        cortex_m4)
set(KARTOS_MCU_FAMILY  stm32f4)
set(KARTOS_MCU_PART    STM32F401xE)
set(KARTOS_BOARD_NAME  stm32f401re_nucleo)

string(TOUPPER "${KARTOS_BOARD_NAME}" KARTOS_BOARD_NAME_UPPER)

set(KARTOS_STARTUP_FILE
    "${CMAKE_SOURCE_DIR}/vendor/stm32cubef4/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f401xe.s")

set(KARTOS_LINKER_SCRIPT_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/linker.ld.in")
set(KARTOS_BOARD_INCLUDE_DIR      "${CMAKE_CURRENT_LIST_DIR}")
set(KARTOS_OPENOCD_CFG            "${CMAKE_CURRENT_LIST_DIR}/openocd.cfg")

add_library(kartos_board INTERFACE)
add_library(kartos::board ALIAS kartos_board)

target_include_directories(kartos_board INTERFACE "${KARTOS_BOARD_INCLUDE_DIR}")
target_compile_definitions(kartos_board INTERFACE
    "${KARTOS_MCU_PART}"
    "RTOS_TARGET_${KARTOS_BOARD_NAME_UPPER}")
