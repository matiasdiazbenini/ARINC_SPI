# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)

include(pico_sdk_import.cmake)

project(ARINC_RX C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(arinc_rx
    arinc_rx.c
)

add_executable(arinc_rx_arinc429_logic
    arinc_rx.c
)

# Genera el header .pio.h para las state machines RX y TX.
pico_generate_pio_header(arinc_rx
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

pico_generate_pio_header(arinc_rx_arinc429_logic
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

target_link_libraries(arinc_rx
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_rx_arinc429_logic
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_compile_definitions(arinc_rx_arinc429_logic PRIVATE
    ARINC429_LOGIC_MODE=1
)

# Log por USB para debug; UART tradicional desactivada.
pico_enable_stdio_usb(arinc_rx 1)
pico_enable_stdio_uart(arinc_rx 0)
pico_enable_stdio_usb(arinc_rx_arinc429_logic 1)
pico_enable_stdio_uart(arinc_rx_arinc429_logic 0)

pico_add_extra_outputs(arinc_rx)
pico_add_extra_outputs(arinc_rx_arinc429_logic)
