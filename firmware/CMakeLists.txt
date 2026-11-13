# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.13)

# The board must be set before the SDK is imported. The board header selects
# PICO_PLATFORM=rp2350 (Arm cores); pass -DPICO_PLATFORM=rp2350-riscv for Hazard3.
set(PICO_BOARD hyperion_v1_2 CACHE STRING "Board type")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.1.0")
    message(FATAL_ERROR "Pico SDK 2.1.0 or later is required (RP2350 support)")
endif ()

project(hyperion_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# --- Build options -----------------------------------------------------------------
set(HYPERION_MODES "drive;blink;adc;rc;imu;motor;failsafe;thermal" CACHE STRING
        "Firmware modes to build, one hyperion_<mode>.uf2 each")
set(HYPERION_RC_PROTOCOL "SBUS" CACHE STRING "Receiver protocol: SBUS, CRSF or PWM")
set_property(CACHE HYPERION_RC_PROTOCOL PROPERTY STRINGS SBUS CRSF PWM)
set(HYPERION_EXTRA_DEFINES "" CACHE STRING
        "Extra config.h overrides, e.g. RC_CH_ARM=4;MOTOR_TEST_MAX_PERMILLE=300")

set(HYPERION_SOURCES
        src/main.c
        src/bringup.c
        src/drive.c
        src/motor.c
        src/servo_out.c
        src/sense.c
        src/imu.c
        src/rc_input.c
        src/sbus_pio.c
        src/crsf_uart.c
        src/rc_pwm.c
        src/core/rc.c
        src/core/sbus.c
        src/core/crsf.c
        src/core/arming.c
        src/core/mixer.c
)

# Warnings for our sources only, not the SDK sources compiled into each target.
set_source_files_properties(${HYPERION_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")

foreach (mode IN LISTS HYPERION_MODES)
    string(TOUPPER ${mode} MODE_UC)
    set(target hyperion_${mode})

    add_executable(${target} ${HYPERION_SOURCES})
    pico_generate_pio_header(${target} ${CMAKE_CURRENT_LIST_DIR}/src/sbus_rx.pio)
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE
            HYPERION_MODE=HYPERION_MODE_${MODE_UC}
            RC_PROTOCOL=RC_PROTOCOL_${HYPERION_RC_PROTOCOL}
            # Never let a stalled USB terminal block the control loop for long.
            PICO_STDIO_USB_STDOUT_TIMEOUT_US=5000
            ${HYPERION_EXTRA_DEFINES}
    )
    target_link_libraries(${target}
            pico_stdlib
            hardware_adc
            hardware_irq
            hardware_pio
            hardware_pwm
            hardware_spi
            hardware_uart
            hardware_watchdog
    )
    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    pico_set_program_name(${target} "hyperion-${mode}")
    pico_add_extra_outputs(${target})
endforeach ()
