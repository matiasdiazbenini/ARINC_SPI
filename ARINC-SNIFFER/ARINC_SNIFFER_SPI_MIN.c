#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/regs/spi.h"

#define TEST_STARTUP_DELAY_MS 1200u

#define TEST_SPI_PORT    spi0
#define TEST_SPI_IRQ     SPI0_IRQ
#define TEST_SPI_RX_PIN  16u
#define TEST_SPI_CSN_PIN 17u
#define TEST_SPI_SCK_PIN 18u
#define TEST_SPI_TX_PIN  19u
#define TEST_DIAG_PIN    20u

#define TEST_FRAME_SIZE 32u

typedef struct {
    uint8_t tx_frame[TEST_FRAME_SIZE];
    uint8_t rx_frame[TEST_FRAME_SIZE];
    uint16_t rx_count;
    uint16_t tx_loaded;
    uint32_t transaction_count;
    bool transaction_active;
    uint64_t start_us;
    uint64_t end_us;
    uint32_t irq_count;
    uint32_t rx_irq_count;
    uint32_t tx_irq_count;
    uint32_t rt_irq_count;
    uint32_t ror_irq_count;
    uint32_t last_sr;
    uint32_t last_ris;
    uint32_t last_mis;
} spi_min_state_t;

static volatile spi_min_state_t g_spi_state;

static void fill_tx_frame(volatile spi_min_state_t *state) {
    memset((void *)state->tx_frame, 0, sizeof(state->tx_frame));
    state->tx_frame[0] = 0xA4u;
    state->tx_frame[1] = 0x29u;
    state->tx_frame[2] = 0x53u; /* S */
    state->tx_frame[3] = 0x50u; /* P */
    state->tx_frame[4] = 0x49u; /* I */
    state->tx_frame[5] = 0x4Du; /* M */
    state->tx_frame[6] = 0x49u; /* I */
    state->tx_frame[7] = 0x4Eu; /* N */
    for (uint8_t i = 8u; i < TEST_FRAME_SIZE; ++i) {
        state->tx_frame[i] = i;
    }
    state->tx_loaded = 0u;
}

static inline void spi_min_prefill_tx(volatile spi_min_state_t *state) {
    spi_hw_t *hw = spi_get_hw(TEST_SPI_PORT);
    while ((hw->sr & SPI_SSPSR_TNF_BITS) && state->tx_loaded < TEST_FRAME_SIZE) {
        hw->dr = state->tx_frame[state->tx_loaded++];
    }
}

static inline void spi_min_capture_rx(volatile spi_min_state_t *state) {
    spi_hw_t *hw = spi_get_hw(TEST_SPI_PORT);
    while (hw->sr & SPI_SSPSR_RNE_BITS) {
        const uint8_t byte = (uint8_t)hw->dr;
        if (state->rx_count < TEST_FRAME_SIZE) {
            state->rx_frame[state->rx_count++] = byte;
        }
    }
}

static inline void spi_min_flush_rx(void) {
    spi_hw_t *hw = spi_get_hw(TEST_SPI_PORT);
    while (hw->sr & SPI_SSPSR_RNE_BITS) {
        (void)hw->dr;
    }
}

static void __not_in_flash_func(spi_min_irq_handler)(void) {
    spi_hw_t *hw = spi_get_hw(TEST_SPI_PORT);
    volatile spi_min_state_t *state = &g_spi_state;
    const uint32_t mis = hw->mis;

    state->irq_count++;
    state->last_mis = mis;
    state->last_ris = hw->ris;

    if (mis & SPI_SSPMIS_RORMIS_BITS) {
        state->ror_irq_count++;
        hw->icr = SPI_SSPICR_RORIC_BITS;
    }
    if (mis & SPI_SSPMIS_RTMIS_BITS) {
        state->rt_irq_count++;
        hw->icr = SPI_SSPICR_RTIC_BITS;
    }
    if (mis & (SPI_SSPMIS_RXMIS_BITS | SPI_SSPMIS_RTMIS_BITS | SPI_SSPMIS_RORMIS_BITS)) {
        state->rx_irq_count++;
        spi_min_capture_rx(state);
    }
    if (mis & SPI_SSPMIS_TXMIS_BITS) {
        state->tx_irq_count++;
        spi_min_prefill_tx(state);
    }

    state->last_sr = hw->sr;
}

static void spi_min_init(volatile spi_min_state_t *state) {
    memset((void *)state, 0, sizeof(*state));

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

    /* Dejamos el bloque SSI sin DMA para no agregar otra variable al ensayo. */
    spi_get_hw(TEST_SPI_PORT)->dmacr = 0u;
    spi_get_hw(TEST_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
    spi_get_hw(TEST_SPI_PORT)->imsc = 0u;

    irq_set_exclusive_handler(TEST_SPI_IRQ, spi_min_irq_handler);
    irq_set_enabled(TEST_SPI_IRQ, true);

    fill_tx_frame(state);
    spi_min_flush_rx();
    spi_min_prefill_tx(state);
}

static void print_rx_summary(const volatile spi_min_state_t *state) {
    const uint8_t b0 = state->rx_count > 0u ? state->rx_frame[0] : 0u;
    const uint8_t b1 = state->rx_count > 1u ? state->rx_frame[1] : 0u;
    const uint8_t b2 = state->rx_count > 2u ? state->rx_frame[2] : 0u;
    const uint8_t b3 = state->rx_count > 3u ? state->rx_frame[3] : 0u;
    const uint8_t b4 = state->rx_count > 4u ? state->rx_frame[4] : 0u;
    const uint8_t b5 = state->rx_count > 5u ? state->rx_frame[5] : 0u;
    const uint8_t b6 = state->rx_count > 6u ? state->rx_frame[6] : 0u;
    const uint8_t b7 = state->rx_count > 7u ? state->rx_frame[7] : 0u;
    const uint32_t dur_us = (uint32_t)(state->end_us - state->start_us);

    printf(
        "SPI-MIN #%lu len=%u tx_loaded=%u dur_us=%lu irq=%lu rx_irq=%lu tx_irq=%lu rt=%lu ror=%lu "
        "sr=%02lX ris=%02lX mis=%02lX first8=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
        (unsigned long)state->transaction_count,
        state->rx_count,
        state->tx_loaded,
        (unsigned long)dur_us,
        (unsigned long)state->irq_count,
        (unsigned long)state->rx_irq_count,
        (unsigned long)state->tx_irq_count,
        (unsigned long)state->rt_irq_count,
        (unsigned long)state->ror_irq_count,
        (unsigned long)state->last_sr,
        (unsigned long)state->last_ris,
        (unsigned long)state->last_mis,
        b0,
        b1,
        b2,
        b3,
        b4,
        b5,
        b6,
        b7);
}

static void spi_min_begin_transaction(volatile spi_min_state_t *state) {
    state->transaction_active = true;
    state->transaction_count++;
    state->rx_count = 0u;
    memset((void *)state->rx_frame, 0, sizeof(state->rx_frame));
    state->irq_count = 0u;
    state->rx_irq_count = 0u;
    state->tx_irq_count = 0u;
    state->rt_irq_count = 0u;
    state->ror_irq_count = 0u;
    state->last_sr = 0u;
    state->last_ris = 0u;
    state->last_mis = 0u;
    state->start_us = time_us_64();
    state->end_us = 0u;

    fill_tx_frame(state);
    spi_min_flush_rx();
    spi_get_hw(TEST_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
    spi_min_prefill_tx(state);
    spi_get_hw(TEST_SPI_PORT)->imsc =
        SPI_SSPIMSC_RXIM_BITS |
        SPI_SSPIMSC_RTIM_BITS |
        SPI_SSPIMSC_TXIM_BITS |
        SPI_SSPIMSC_RORIM_BITS;

    gpio_put(TEST_DIAG_PIN, 1u);
}

static void spi_min_end_transaction(volatile spi_min_state_t *state) {
    spi_hw_t *hw = spi_get_hw(TEST_SPI_PORT);

    hw->imsc = 0u;
    spi_min_capture_rx(state);
    state->last_sr = hw->sr;
    state->last_ris = hw->ris;
    state->last_mis = hw->mis;
    state->end_us = time_us_64();
    state->transaction_active = false;
    gpio_put(TEST_DIAG_PIN, 0u);

    print_rx_summary(state);

    spi_min_flush_rx();
    hw->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
    fill_tx_frame(state);
    spi_min_prefill_tx(state);
}

static void service_spi_min(volatile spi_min_state_t *state) {
    static bool cs_prev_high = true;
    const bool cs_now_high = gpio_get(TEST_SPI_CSN_PIN) != 0u;

    if (!state->transaction_active && cs_prev_high && !cs_now_high) {
        spi_min_begin_transaction(state);
    }

    if (state->transaction_active && !cs_prev_high && cs_now_high) {
        spi_min_end_transaction(state);
    }

    cs_prev_high = cs_now_high;
}

int main(void) {
    stdio_init_all();
    sleep_ms(TEST_STARTUP_DELAY_MS);

    printf("ARINC_SNIFFER_SPI_MIN - prueba minima SPI slave hardware con IRQ\r\n");
    printf("Pico SPI slave: GP16=RX(MOSI) | GP17=CSn | GP18=SCK | GP19=TX(MISO)\r\n");
    printf("Diag GP20: nivel alto mientras CS esta activo\r\n");
    printf("TX fijo: A4 29 53 50 49 4D 49 4E ...\r\n");
    printf("Modo: IRQ + DMA deshabilitado + resumen de SR/RIS/MIS\r\n\r\n");

    spi_min_init(&g_spi_state);

    while (true) {
        service_spi_min(&g_spi_state);
        tight_loop_contents();
    }
}
