cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()

# Domyślnie odbiornik
set(MODE "RECEIVER" CACHE STRING "Build mode: SENDER or RECEIVER")

# Pico 2 W is RP2350. Pico W is RP2040. One build directory cannot switch.
if(MODE STREQUAL "SENDER")

    set(PICO_BOARD pico2_w)
    set(PICO_PLATFORM rp2350)

elseif(MODE STREQUAL "RECEIVER")

    set(PICO_BOARD pico_w)
    set(PICO_PLATFORM rp2040)

else()

    message(FATAL_ERROR "MODE must be SENDER or RECEIVER")

endif()

set(PICO_BOARD "${PICO_BOARD}" CACHE STRING "PICO target board" FORCE)
set(PICO_PLATFORM "${PICO_PLATFORM}" CACHE STRING "PICO platform" FORCE)
# Normal variables override pico-vscode.cmake and a stale -D from the other board.
set(PICO_BOARD "${PICO_BOARD}")
set(PICO_PLATFORM "${PICO_PLATFORM}")

if(DEFINED PICO_PREVIOUS_PLATFORM)
    string(REGEX REPLACE "-.*" "" _previous_platform "${PICO_PREVIOUS_PLATFORM}")
    string(REGEX REPLACE "-.*" "" _wanted_platform "${PICO_PLATFORM}")
    if(NOT _previous_platform STREQUAL _wanted_platform)
        message(FATAL_ERROR
            "This build directory is for ${PICO_PREVIOUS_PLATFORM}, but MODE=${MODE} needs ${PICO_PLATFORM}. "
            "Delete this build directory and configure again. "
            "Use separate directories: build/sender and build/receiver.")
    endif()
endif()

include(pico_sdk_import.cmake)

project(pico_project C CXX ASM)

pico_sdk_init()


if(MODE STREQUAL "SENDER")

    add_executable(sender
        sender/main.c
        libraries/sensor/bme280.c
        libraries/crc/crc.c
        libraries/uart/uart.c
    )

    target_include_directories(sender PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/libraries/sensor
        ${CMAKE_CURRENT_LIST_DIR}/libraries/crc
        ${CMAKE_CURRENT_LIST_DIR}/libraries/uart
    )

    target_link_libraries(sender
        pico_stdlib
        hardware_i2c
        hardware_uart
        pico_low_power
        pico_cyw43_arch_none
    )

    # USB only. uart0 on GP0/GP1 is the link to the receiver.
    pico_enable_stdio_uart(sender 0)
    pico_enable_stdio_usb(sender 1)

    pico_add_extra_outputs(sender)

elseif(MODE STREQUAL "RECEIVER")

    add_executable(receiver
        receiver/main.c
        libraries/lcd/lcd.c
        libraries/button/button.c
        libraries/crc/crc.c
        libraries/uart/uart.c
    )

    target_include_directories(receiver PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/libraries/lcd
        ${CMAKE_CURRENT_LIST_DIR}/libraries/button
        ${CMAKE_CURRENT_LIST_DIR}/libraries/crc
        ${CMAKE_CURRENT_LIST_DIR}/libraries/uart
    )

    target_link_libraries(receiver
        pico_stdlib
        hardware_uart
        hardware_i2c
        pico_cyw43_arch_none
    )

    pico_enable_stdio_uart(receiver 0)
    pico_enable_stdio_usb(receiver 1)

    pico_add_extra_outputs(receiver)

endif()