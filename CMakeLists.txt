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


project(bus_monitor C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


pico_sdk_init()


# ============================================================
# ARCHIVOS DEL PROYECTO
# ============================================================

add_executable(bus_monitor

    main.c

    bus/mil1553_rx.c
)


# ============================================================
# GENERAR HEADER DEL PIO RX
# ============================================================

pico_generate_pio_header(
    bus_monitor
    ${CMAKE_CURRENT_LIST_DIR}/bus/mil1553_rx.pio
)


# ============================================================
# DIRECTORIOS DE INCLUDES
# ============================================================

target_include_directories(
    bus_monitor
    PRIVATE

    ${CMAKE_CURRENT_LIST_DIR}
)


# ============================================================
# LIBRERÍAS
# ============================================================

target_link_libraries(
    bus_monitor

    pico_stdlib

    hardware_clocks
    hardware_pio
    hardware_gpio
)


# ============================================================
# USB / UART
# ============================================================

pico_enable_stdio_usb(
    bus_monitor
    1
)

pico_enable_stdio_uart(
    bus_monitor
    0
)


# ============================================================
# GENERAR UF2 / BIN / HEX
# ============================================================

pico_add_extra_outputs(
    bus_monitor
)