cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)

include(pico_sdk_import.cmake)

project(slave_spi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(slave_spi
    slave_spi.c
)

target_link_libraries(slave_spi
    pico_stdlib
    hardware_spi
)

pico_enable_stdio_usb(slave_spi 1)
pico_enable_stdio_uart(slave_spi 0)

pico_add_extra_outputs(slave_spi)