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

project(ARINC_SNIFFER C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ARINC_SNIFFER
    ARINC_SNIFFER.c
)

add_executable(sniffer_arinc429_logic
    ARINC_SNIFFER.c
)

add_executable(sniffer_arinc429_logic_pio_frame
    ARINC_SNIFFER.c
)

add_executable(ARINC_SNIFFER_GPIO_TEST
    ARINC_SNIFFER_GPIO_TEST.c
)

add_executable(ARINC_SNIFFER_SPI_MIN
    ARINC_SNIFFER_SPI_MIN.c
)

add_executable(ARINC_SNIFFER_SPI_SCOPE
    ARINC_SNIFFER_SPI_SCOPE.c
)

add_executable(ARINC_SNIFFER_SPI_MASTER_MIN
    ARINC_SNIFFER_SPI_MASTER_MIN.c
)

add_executable(ARINC_SNIFFER_SPI_BYTE_SLAVE
    ARINC_SNIFFER_SPI_BYTE_SLAVE.c
)

add_executable(ARINC_SNIFFER_SPI_PIO_FRAME
    ARINC_SNIFFER_SPI_PIO_FRAME.c
)

# Genera el header .pio.h para la state machine receptora del sniffer.
pico_generate_pio_header(ARINC_SNIFFER
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

pico_generate_pio_header(sniffer_arinc429_logic
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

pico_generate_pio_header(sniffer_arinc429_logic_pio_frame
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

pico_generate_pio_header(sniffer_arinc429_logic_pio_frame
    ${CMAKE_CURRENT_LIST_DIR}/spi_frame_slave.pio
)

pico_generate_pio_header(ARINC_SNIFFER_SPI_PIO_FRAME
    ${CMAKE_CURRENT_LIST_DIR}/spi_frame_slave.pio
)

target_link_libraries(ARINC_SNIFFER
    pico_stdlib
    hardware_pio
    hardware_spi
)

target_link_libraries(sniffer_arinc429_logic
    pico_stdlib
    hardware_pio
    hardware_spi
)

target_link_libraries(sniffer_arinc429_logic_pio_frame
    pico_stdlib
    hardware_pio
    hardware_spi
)

target_compile_definitions(sniffer_arinc429_logic PRIVATE
    ARINC429_LOGIC_MODE=1
)

target_compile_definitions(sniffer_arinc429_logic_pio_frame PRIVATE
    ARINC429_LOGIC_MODE=1
    SNIFFER_SPI_PIO_FRAME_TRANSPORT=1
)

target_link_libraries(ARINC_SNIFFER_GPIO_TEST
    pico_stdlib
)

target_link_libraries(ARINC_SNIFFER_SPI_MIN
    pico_stdlib
    hardware_spi
)

target_link_libraries(ARINC_SNIFFER_SPI_SCOPE
    pico_stdlib
)

target_link_libraries(ARINC_SNIFFER_SPI_MASTER_MIN
    pico_stdlib
    hardware_spi
)

target_link_libraries(ARINC_SNIFFER_SPI_BYTE_SLAVE
    pico_stdlib
    hardware_spi
)

target_link_libraries(ARINC_SNIFFER_SPI_PIO_FRAME
    pico_stdlib
    hardware_pio
)

# El sniffer publica por USB los registros que luego consume Flask.
pico_enable_stdio_usb(ARINC_SNIFFER 1)
pico_enable_stdio_uart(ARINC_SNIFFER 0)
pico_enable_stdio_usb(sniffer_arinc429_logic 1)
pico_enable_stdio_uart(sniffer_arinc429_logic 0)

pico_enable_stdio_usb(sniffer_arinc429_logic_pio_frame 1)
pico_enable_stdio_uart(sniffer_arinc429_logic_pio_frame 0)

pico_enable_stdio_usb(ARINC_SNIFFER_GPIO_TEST 1)
pico_enable_stdio_uart(ARINC_SNIFFER_GPIO_TEST 0)

pico_enable_stdio_usb(ARINC_SNIFFER_SPI_MIN 1)
pico_enable_stdio_uart(ARINC_SNIFFER_SPI_MIN 0)

pico_enable_stdio_usb(ARINC_SNIFFER_SPI_SCOPE 1)
pico_enable_stdio_uart(ARINC_SNIFFER_SPI_SCOPE 0)

pico_enable_stdio_usb(ARINC_SNIFFER_SPI_MASTER_MIN 1)
pico_enable_stdio_uart(ARINC_SNIFFER_SPI_MASTER_MIN 0)

pico_enable_stdio_usb(ARINC_SNIFFER_SPI_BYTE_SLAVE 1)
pico_enable_stdio_uart(ARINC_SNIFFER_SPI_BYTE_SLAVE 0)

pico_enable_stdio_usb(ARINC_SNIFFER_SPI_PIO_FRAME 1)
pico_enable_stdio_uart(ARINC_SNIFFER_SPI_PIO_FRAME 0)

pico_add_extra_outputs(ARINC_SNIFFER)
pico_add_extra_outputs(sniffer_arinc429_logic)
pico_add_extra_outputs(sniffer_arinc429_logic_pio_frame)
pico_add_extra_outputs(ARINC_SNIFFER_GPIO_TEST)
pico_add_extra_outputs(ARINC_SNIFFER_SPI_MIN)
pico_add_extra_outputs(ARINC_SNIFFER_SPI_SCOPE)
pico_add_extra_outputs(ARINC_SNIFFER_SPI_MASTER_MIN)
pico_add_extra_outputs(ARINC_SNIFFER_SPI_BYTE_SLAVE)
pico_add_extra_outputs(ARINC_SNIFFER_SPI_PIO_FRAME)
