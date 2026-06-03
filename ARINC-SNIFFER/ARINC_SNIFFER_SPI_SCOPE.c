#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"

#define TEST_STARTUP_DELAY_MS 1200u

#define PIN_MOSI 16u
#define PIN_CSN  17u
#define PIN_SCK  18u
#define PIN_DIAG 20u

#define MAX_CAPTURE_BITS 32u
#define CAPTURE_QUEUE_SIZE 64u

typedef struct {
    uint32_t id;
    uint64_t start_us;
    uint64_t end_us;
    uint32_t cs_falls;
    uint32_t cs_rises;
    uint32_t rising_edges;
    uint8_t sampled_bits[MAX_CAPTURE_BITS];
    uint32_t sampled_count;
} spi_scope_capture_t;

static volatile spi_scope_capture_t g_queue[CAPTURE_QUEUE_SIZE];
static volatile uint32_t g_write_idx = 0u;
static volatile uint32_t g_read_idx = 0u;
static volatile bool g_active = false;
static volatile spi_scope_capture_t g_current;
static volatile uint32_t g_next_id = 1u;

static void print_bits(const uint8_t *bits, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        putchar(bits[i] ? '1' : '0');
    }
}

static void queue_push(const volatile spi_scope_capture_t *capture) {
    const uint32_t idx = g_write_idx % CAPTURE_QUEUE_SIZE;
    memcpy((void *)&g_queue[idx], (const void *)capture, sizeof(spi_scope_capture_t));
    g_write_idx++;
    if (g_write_idx - g_read_idx > CAPTURE_QUEUE_SIZE) {
        g_read_idx = g_write_idx - CAPTURE_QUEUE_SIZE;
    }
}

static void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == PIN_CSN) {
        if (events & GPIO_IRQ_EDGE_FALL) {
            if (!g_active) {
                memset((void *)&g_current, 0, sizeof(g_current));
                g_current.id = g_next_id++;
                g_current.start_us = time_us_64();
                g_active = true;
                gpio_put(PIN_DIAG, 1u);
            }
            g_current.cs_falls++;
        }
        if (events & GPIO_IRQ_EDGE_RISE) {
            if (g_active) {
                g_current.cs_rises++;
                g_current.end_us = time_us_64();
                gpio_put(PIN_DIAG, 0u);
                queue_push(&g_current);
                g_active = false;
            }
        }
    } else if (gpio == PIN_SCK) {
        if (g_active && (events & GPIO_IRQ_EDGE_RISE)) {
            if (g_current.sampled_count < MAX_CAPTURE_BITS) {
                g_current.sampled_bits[g_current.sampled_count++] = gpio_get(PIN_MOSI) ? 1u : 0u;
            }
            g_current.rising_edges++;
        }
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(TEST_STARTUP_DELAY_MS);

    gpio_init(PIN_MOSI);
    gpio_set_dir(PIN_MOSI, GPIO_IN);
    gpio_pull_down(PIN_MOSI);

    gpio_init(PIN_CSN);
    gpio_set_dir(PIN_CSN, GPIO_IN);
    gpio_pull_up(PIN_CSN);

    gpio_init(PIN_SCK);
    gpio_set_dir(PIN_SCK, GPIO_IN);
    gpio_pull_down(PIN_SCK);

    gpio_init(PIN_DIAG);
    gpio_set_dir(PIN_DIAG, GPIO_OUT);
    gpio_put(PIN_DIAG, 0u);

    gpio_set_irq_enabled_with_callback(PIN_CSN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled(PIN_SCK, GPIO_IRQ_EDGE_RISE, true);

    printf("ARINC_SNIFFER_SPI_SCOPE - observador GPIO de CS/SCK/MOSI con IRQ\r\n");
    printf("Entradas: GP16=MOSI | GP17=CSn | GP18=SCK\r\n");
    printf("Salida : GP20 alto mientras CS esta bajo\r\n");
    printf("Objetivo: detectar si CS tiene un unico pulso o microcortes durante una transaccion.\r\n\r\n");

    while (true) {
        while (g_read_idx != g_write_idx) {
            const uint32_t idx = g_read_idx % CAPTURE_QUEUE_SIZE;
            spi_scope_capture_t capture;
            memcpy(&capture, (const void *)&g_queue[idx], sizeof(capture));
            g_read_idx++;

            const uint64_t duration_us = capture.end_us - capture.start_us;
            printf("SPI-SCOPE #%lu cs_low_us=%llu cs_falls=%lu cs_rises=%lu rising_edges=%lu sampled_bits=",
                   (unsigned long)capture.id,
                   (unsigned long long)duration_us,
                   (unsigned long)capture.cs_falls,
                   (unsigned long)capture.cs_rises,
                   (unsigned long)capture.rising_edges);
            print_bits(capture.sampled_bits, capture.sampled_count);
            printf("\r\n");
        }
        tight_loop_contents();
    }
}
