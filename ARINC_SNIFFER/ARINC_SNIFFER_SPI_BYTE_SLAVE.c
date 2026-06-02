#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/regs/spi.h"

#define TEST_STARTUP_DELAY_MS 1200u

#define TEST_SPI_PORT    spi0
#define TEST_SPI_RX_PIN  16u
#define TEST_SPI_CSN_PIN 17u
#define TEST_SPI_SCK_PIN 18u
#define TEST_SPI_TX_PIN  19u
#define TEST_DIAG_PIN    20u

#define CMD_RESET 0xF0u
#define CMD_NEXT  0xF1u
#define DEFAULT_TX 0xEEu

typedef struct {
    uint8_t response[32];
    uint8_t next_tx;
    uint8_t index;
    uint32_t transaction_count;
    uint32_t reset_count;
    uint32_t next_count;
    uint32_t other_count;
    uint8_t last_rx;
    uint8_t last_tx;
} spi_byte_state_t;

static void load_response(spi_byte_state_t *state) {
    memset(state->response, 0, sizeof(state->response));
    state->response[0] = 0xA4u;
    state->response[1] = 0x29u;
    state->response[2] = 0x53u; /* S */
    state->response[3] = 0x50u; /* P */
    state->response[4] = 0x49u; /* I */
    state->response[5] = 0x4Du; /* M */
    state->response[6] = 0x49u; /* I */
    state->response[7] = 0x4Eu; /* N */
    for (uint8_t i = 8u; i < 32u; ++i) {
        state->response[i] = i;
    }
}

static void prefill_tx(uint8_t value) {
    spi_hw_t *hw = spi_get_hw(TEST_SPI_PORT);
    while (hw->sr & SPI_SSPSR_RNE_BITS) {
        (void)hw->dr;
    }
    if (hw->sr & SPI_SSPSR_TNF_BITS) {
        hw->dr = value;
    }
}

static void init_slave(spi_byte_state_t *state) {
    memset(state, 0, sizeof(*state));
    load_response(state);
    state->next_tx = DEFAULT_TX;
    state->index = 0u;

    spi_init(TEST_SPI_PORT, 1000u * 1000u);
    spi_set_slave(TEST_SPI_PORT, true);
    spi_set_format(TEST_SPI_PORT, 8u, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(TEST_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(TEST_SPI_CSN_PIN, GPIO_FUNC_SPI);
    gpio_set_function(TEST_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(TEST_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_pull_up(TEST_SPI_CSN_PIN);

    gpio_init(TEST_DIAG_PIN);
    gpio_set_dir(TEST_DIAG_PIN, GPIO_OUT);
    gpio_put(TEST_DIAG_PIN, 0u);

    spi_get_hw(TEST_SPI_PORT)->dmacr = 0u;
    spi_get_hw(TEST_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
    prefill_tx(state->next_tx);
}

static void prepare_next_tx(spi_byte_state_t *state, uint8_t cmd) {
    if (cmd == CMD_RESET) {
        state->reset_count++;
        state->index = 1u;
        state->next_tx = state->response[0];
    } else if (cmd == CMD_NEXT) {
        state->next_count++;
        if (state->index < sizeof(state->response)) {
            state->next_tx = state->response[state->index++];
        } else {
            state->next_tx = DEFAULT_TX;
        }
    } else {
        state->other_count++;
        state->next_tx = DEFAULT_TX;
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(TEST_STARTUP_DELAY_MS);

    spi_byte_state_t state;
    init_slave(&state);

    printf("ARINC_SNIFFER_SPI_BYTE_SLAVE - slave SPI hardware, protocolo byte a byte\r\n");
    printf("Pico slave SPI0: GP16=RX(MOSI) | GP17=CSn | GP18=SCK | GP19=TX(MISO)\r\n");
    printf("Comandos: RESET=0xF0 | NEXT=0xF1\r\n");
    printf("Respuesta fija: A4 29 53 50 49 4D 49 4E ...\r\n");
    printf("Semantica: cada transaccion devuelve 1 byte y prepara el siguiente\r\n\r\n");

    while (true) {
        while (gpio_get(TEST_SPI_CSN_PIN) != 0u) {
            tight_loop_contents();
        }
        gpio_put(TEST_DIAG_PIN, 1u);

        while (gpio_get(TEST_SPI_CSN_PIN) == 0u) {
            tight_loop_contents();
        }

        uint8_t rx = 0u;
        if (spi_get_hw(TEST_SPI_PORT)->sr & SPI_SSPSR_RNE_BITS) {
            rx = (uint8_t)spi_get_hw(TEST_SPI_PORT)->dr;
        }
        state.transaction_count++;
        state.last_rx = rx;
        state.last_tx = state.next_tx;

        prepare_next_tx(&state, rx);
        spi_get_hw(TEST_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
        prefill_tx(state.next_tx);
        gpio_put(TEST_DIAG_PIN, 0u);

        printf("SPI-BYTE #%lu rx=%02X tx_sent=%02X next_tx=%02X idx=%u reset=%lu next=%lu other=%lu\r\n",
               (unsigned long)state.transaction_count,
               state.last_rx,
               state.last_tx,
               state.next_tx,
               state.index,
               (unsigned long)state.reset_count,
               (unsigned long)state.next_count,
               (unsigned long)state.other_count);
    }
}
