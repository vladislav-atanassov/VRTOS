# kartos::mcu_stm32f4 — STM32F4 HAL + CMSIS Device static library.
#
# Sources are enumerated explicitly (only the modules KARTOS actually uses)
# so unused drivers don't bloat the binary.
#
# Requires kartos::board to be defined first (provides STM32F4xxxx define and
# the board include dir where stm32f4xx_hal_conf.h lives).

set(_F4_ROOT  "${CMAKE_SOURCE_DIR}/vendor/stm32cubef4")
set(_HAL_SRC  "${_F4_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src")
set(_CMSIS_DEV_SRC
    "${_F4_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates")

add_library(kartos_mcu_stm32f4 STATIC
    # HAL base (required by all other HAL modules)
    "${_HAL_SRC}/stm32f4xx_hal.c"
    # Peripherals used by KARTOS
    "${_HAL_SRC}/stm32f4xx_hal_cortex.c"
    "${_HAL_SRC}/stm32f4xx_hal_dma.c"
    "${_HAL_SRC}/stm32f4xx_hal_gpio.c"
    "${_HAL_SRC}/stm32f4xx_hal_pwr.c"
    "${_HAL_SRC}/stm32f4xx_hal_pwr_ex.c"
    "${_HAL_SRC}/stm32f4xx_hal_rcc.c"
    "${_HAL_SRC}/stm32f4xx_hal_rcc_ex.c"
    "${_HAL_SRC}/stm32f4xx_hal_uart.c"
    # CMSIS Device system file
    "${_CMSIS_DEV_SRC}/system_stm32f4xx.c")
add_library(kartos::mcu_stm32f4 ALIAS kartos_mcu_stm32f4)

target_include_directories(kartos_mcu_stm32f4
    PUBLIC
        "${_F4_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc"
        "${_F4_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy"
        "${_F4_ROOT}/Drivers/CMSIS/Include"
        "${_F4_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Include")

target_compile_definitions(kartos_mcu_stm32f4
    PUBLIC
        USE_HAL_DRIVER)

target_compile_options(kartos_mcu_stm32f4
    PRIVATE
        -w)  # suppress warnings from vendor HAL sources

# board provides: the MCU part define (STM32F446xx) and the include dir for
# stm32f4xx_hal_conf.h.  Link PRIVATE — these are compilation-only details.
target_link_libraries(kartos_mcu_stm32f4
    PRIVATE
        kartos::board)
