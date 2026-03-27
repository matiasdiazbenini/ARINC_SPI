cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(slave_p1_spi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(slave_p1_spi
    slave_p1_spi.c
)

target_link_libraries(slave_p1_spi
    pico_stdlib
    hardware_spi
)

pico_enable_stdio_usb(slave_p1_spi 1)
pico_enable_stdio_uart(slave_p1_spi 0)

pico_add_extra_outputs(slave_p1_spi)