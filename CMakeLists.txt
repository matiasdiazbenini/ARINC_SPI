cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)

include(pico_sdk_import.cmake)

project(master_spi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(master_spi
    master_spi.c
)

# Genera el header .pio.h a partir del ensamblador PIO del enlace.
pico_generate_pio_header(master_spi
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

target_link_libraries(master_spi
    pico_stdlib
    hardware_pio
    # Se usa para calcular el clock divider real del state machine.
    hardware_clocks
)

# Log por USB para debug; UART tradicional desactivada.
pico_enable_stdio_usb(master_spi 1)
pico_enable_stdio_uart(master_spi 0)

pico_add_extra_outputs(master_spi)
