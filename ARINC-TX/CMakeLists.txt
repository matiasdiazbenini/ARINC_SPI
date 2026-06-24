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

project(ARINC_TX C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(arinc_tx
    arinc_tx.c
)

add_executable(arinc_tx_arinc429_logic
    arinc_tx.c
)

add_executable(arinc_tx_arinc429_logic_stream
    arinc_tx.c
)

add_executable(arinc_tx_arinc429_logic_stream_12k5
    arinc_tx.c
)

add_executable(arinc_tx_arinc429_logic_stream_autorate
    arinc_tx.c
)

add_executable(arinc_tx_arinc429_logic_parity_test
    arinc_tx.c
)

add_executable(arinc_tx_stress
    arinc_tx.c
)

add_executable(arinc_tx_gap2
    arinc_tx.c
)

add_executable(arinc_tx_gap3
    arinc_tx.c
)

add_executable(arinc_tx_gap0
    arinc_tx.c
)

# Genera el header .pio.h a partir del ensamblador PIO del enlace.
pico_generate_pio_header(arinc_tx
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

pico_generate_pio_header(arinc_tx_arinc429_logic
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

pico_generate_pio_header(arinc_tx_arinc429_logic_stream
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

pico_generate_pio_header(arinc_tx_arinc429_logic_stream_12k5
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

pico_generate_pio_header(arinc_tx_arinc429_logic_stream_autorate
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

pico_generate_pio_header(arinc_tx_arinc429_logic_parity_test
    ${CMAKE_CURRENT_LIST_DIR}/arinc429_logic.pio
)

pico_generate_pio_header(arinc_tx_stress
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

pico_generate_pio_header(arinc_tx_gap2
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

pico_generate_pio_header(arinc_tx_gap3
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

pico_generate_pio_header(arinc_tx_gap0
    ${CMAKE_CURRENT_LIST_DIR}/arinc_gpio_link.pio
)

target_link_libraries(arinc_tx
    pico_stdlib
    hardware_pio
    # Se usa para calcular el clock divider real del state machine.
    hardware_clocks
)

target_link_libraries(arinc_tx_arinc429_logic
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_arinc429_logic_stream
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_arinc429_logic_stream_12k5
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_arinc429_logic_stream_autorate
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_arinc429_logic_parity_test
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_stress
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_gap2
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_gap3
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_link_libraries(arinc_tx_gap0
    pico_stdlib
    hardware_pio
    hardware_clocks
)

target_compile_definitions(arinc_tx_stress PRIVATE
    MASTER_PROFILE_VARIANT=1
    MASTER_WORDS_PER_BATCH=1000
    MASTER_POST_ACK_GUARD_US=100
    MASTER_ENABLE_TX_BATCH_LOG=0
)

target_compile_definitions(arinc_tx_arinc429_logic PRIVATE
    ARINC429_LOGIC_MODE=1
)

target_compile_definitions(arinc_tx_arinc429_logic_stream PRIVATE
    ARINC429_LOGIC_MODE=1
    MASTER_STREAM_MODE=1
    MASTER_PROFILE_VARIANT=0
    MASTER_ENABLE_TX_BATCH_LOG=0
    MASTER_STREAM_MIN_BURST_WORDS=1
    MASTER_STREAM_MAX_BURST_WORDS=2000
    MASTER_STREAM_GAP_MIN_US=0
    MASTER_STREAM_GAP_MAX_US=25000
    MASTER_STREAM_LOG_EVERY=16
    MASTER_STREAM_MONITOR_REV=1
)

target_compile_definitions(arinc_tx_arinc429_logic_stream_12k5 PRIVATE
    ARINC429_LOGIC_MODE=1
    MASTER_BIT_RATE_HZ=12500
    MASTER_STREAM_MODE=1
    MASTER_PROFILE_VARIANT=0
    MASTER_ENABLE_TX_BATCH_LOG=0
    MASTER_STREAM_MIN_BURST_WORDS=1
    MASTER_STREAM_MAX_BURST_WORDS=2000
    MASTER_STREAM_GAP_MIN_US=0
    MASTER_STREAM_GAP_MAX_US=25000
    MASTER_STREAM_LOG_EVERY=16
    MASTER_STREAM_MONITOR_REV=1
)

target_compile_definitions(arinc_tx_arinc429_logic_stream_autorate PRIVATE
    ARINC429_LOGIC_MODE=1
    MASTER_STREAM_MODE=1
    MASTER_RANDOM_BIT_RATE=1
    MASTER_PROFILE_VARIANT=0
    MASTER_ENABLE_TX_BATCH_LOG=0
    MASTER_STREAM_MIN_BURST_WORDS=1
    MASTER_STREAM_MAX_BURST_WORDS=2000
    MASTER_STREAM_GAP_MIN_US=0
    MASTER_STREAM_GAP_MAX_US=25000
    MASTER_STREAM_LOG_EVERY=16
    MASTER_STREAM_MONITOR_REV=1
)

target_compile_definitions(arinc_tx_arinc429_logic_parity_test PRIVATE
    ARINC429_LOGIC_MODE=1
    MASTER_STREAM_MODE=1
    MASTER_PROFILE_VARIANT=0
    MASTER_ENABLE_TX_BATCH_LOG=0
    MASTER_STREAM_MIN_BURST_WORDS=1
    MASTER_STREAM_MAX_BURST_WORDS=2000
    MASTER_STREAM_GAP_MIN_US=0
    MASTER_STREAM_GAP_MAX_US=25000
    MASTER_STREAM_LOG_EVERY=16
    MASTER_STREAM_MONITOR_REV=1
    MASTER_PARITY_FAULT_EVERY=100
)

target_compile_definitions(arinc_tx_gap2 PRIVATE
    MASTER_PROFILE_VARIANT=0
    MASTER_WORDS_PER_BATCH=1000
    MASTER_WORD_GAP_BITS=2
    MASTER_POST_ACK_GUARD_US=100
    MASTER_ENABLE_TX_BATCH_LOG=0
)

target_compile_definitions(arinc_tx_gap3 PRIVATE
    MASTER_PROFILE_VARIANT=0
    MASTER_WORDS_PER_BATCH=1000
    MASTER_WORD_GAP_BITS=3
    MASTER_POST_ACK_GUARD_US=100
    MASTER_ENABLE_TX_BATCH_LOG=0
)

target_compile_definitions(arinc_tx_gap0 PRIVATE
    MASTER_PROFILE_VARIANT=0
    MASTER_WORDS_PER_BATCH=1000
    MASTER_WORD_GAP_BITS=0
    MASTER_POST_ACK_GUARD_US=100
    MASTER_ENABLE_TX_BATCH_LOG=0
)

# Log por USB para debug; UART tradicional desactivada.
pico_enable_stdio_usb(arinc_tx 1)
pico_enable_stdio_uart(arinc_tx 0)
pico_enable_stdio_usb(arinc_tx_arinc429_logic 1)
pico_enable_stdio_uart(arinc_tx_arinc429_logic 0)
pico_enable_stdio_usb(arinc_tx_arinc429_logic_stream 1)
pico_enable_stdio_uart(arinc_tx_arinc429_logic_stream 0)
pico_enable_stdio_usb(arinc_tx_arinc429_logic_stream_12k5 1)
pico_enable_stdio_uart(arinc_tx_arinc429_logic_stream_12k5 0)
pico_enable_stdio_usb(arinc_tx_arinc429_logic_stream_autorate 1)
pico_enable_stdio_uart(arinc_tx_arinc429_logic_stream_autorate 0)
pico_enable_stdio_usb(arinc_tx_arinc429_logic_parity_test 1)
pico_enable_stdio_uart(arinc_tx_arinc429_logic_parity_test 0)
pico_enable_stdio_usb(arinc_tx_stress 1)
pico_enable_stdio_uart(arinc_tx_stress 0)
pico_enable_stdio_usb(arinc_tx_gap2 1)
pico_enable_stdio_uart(arinc_tx_gap2 0)
pico_enable_stdio_usb(arinc_tx_gap3 1)
pico_enable_stdio_uart(arinc_tx_gap3 0)
pico_enable_stdio_usb(arinc_tx_gap0 1)
pico_enable_stdio_uart(arinc_tx_gap0 0)

pico_add_extra_outputs(arinc_tx)
pico_add_extra_outputs(arinc_tx_arinc429_logic)
pico_add_extra_outputs(arinc_tx_arinc429_logic_stream)
pico_add_extra_outputs(arinc_tx_arinc429_logic_stream_12k5)
pico_add_extra_outputs(arinc_tx_arinc429_logic_stream_autorate)
pico_add_extra_outputs(arinc_tx_arinc429_logic_parity_test)
pico_add_extra_outputs(arinc_tx_stress)
pico_add_extra_outputs(arinc_tx_gap2)
pico_add_extra_outputs(arinc_tx_gap3)
pico_add_extra_outputs(arinc_tx_gap0)
