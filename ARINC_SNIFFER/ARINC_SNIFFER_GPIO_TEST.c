#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#define TEST_STARTUP_DELAY_MS 1200u

#define PIN_IN_MOSI   16u
#define PIN_IN_CSN    17u
#define PIN_IN_SCK    18u
#define PIN_OUT_MISO  19u
#define PIN_OUT_DRDY  20u

#define MISO_TOGGLE_MS 300u
#define DRDY_TOGGLE_MS 700u
#define SNAPSHOT_MS    1000u

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef struct {
    uint gpio;
    const char *name;
    bool last_value;
} observed_input_t;

static void print_input_snapshot(const observed_input_t *inputs, size_t count) {
    printf("GPIO-TEST SNAPSHOT");
    for (size_t i = 0; i < count; ++i) {
        printf(" | %s=%u", inputs[i].name, inputs[i].last_value ? 1u : 0u);
    }
    printf("\r\n");
}

int main(void) {
    stdio_init_all();
    sleep_ms(TEST_STARTUP_DELAY_MS);

    observed_input_t inputs[] = {
        {.gpio = PIN_IN_MOSI, .name = "GP16<=MOSI", .last_value = false},
        {.gpio = PIN_IN_CSN,  .name = "GP17<=CSn",  .last_value = false},
        {.gpio = PIN_IN_SCK,  .name = "GP18<=SCK",  .last_value = false},
    };

    for (size_t i = 0; i < ARRAY_COUNT(inputs); ++i) {
        gpio_init(inputs[i].gpio);
        gpio_set_dir(inputs[i].gpio, GPIO_IN);
        gpio_pull_down(inputs[i].gpio);
        inputs[i].last_value = gpio_get(inputs[i].gpio);
    }

    gpio_init(PIN_OUT_MISO);
    gpio_set_dir(PIN_OUT_MISO, GPIO_OUT);
    gpio_put(PIN_OUT_MISO, 0u);

    gpio_init(PIN_OUT_DRDY);
    gpio_set_dir(PIN_OUT_DRDY, GPIO_OUT);
    gpio_put(PIN_OUT_DRDY, 0u);

    printf("ARINC_SNIFFER_GPIO_TEST - continuidad GPIO para enlace SPI\r\n");
    printf("Entradas Pico: GP16<=Pi GPIO10(MOSI) | GP17<=Pi GPIO8(CE0) | GP18<=Pi GPIO11(SCLK)\r\n");
    printf("Salidas Pico : GP19=>Pi GPIO9(MISO)  | GP20=>Pi GPIO25(DRDY)\r\n");
    printf("Patrones: GP19 conmuta cada %u ms | GP20 conmuta cada %u ms\r\n",
           MISO_TOGGLE_MS,
           DRDY_TOGGLE_MS);
    printf("Usa el script gpio_spi_continuity_test.py en la Raspberry Pi 3B+.\r\n\r\n");

    absolute_time_t next_miso_toggle = make_timeout_time_ms(MISO_TOGGLE_MS);
    absolute_time_t next_drdy_toggle = make_timeout_time_ms(DRDY_TOGGLE_MS);
    absolute_time_t next_snapshot = make_timeout_time_ms(SNAPSHOT_MS);
    bool miso_level = false;
    bool drdy_level = false;

    print_input_snapshot(inputs, ARRAY_COUNT(inputs));

    while (true) {
        const absolute_time_t now = get_absolute_time();

        if (absolute_time_diff_us(now, next_miso_toggle) <= 0) {
            miso_level = !miso_level;
            gpio_put(PIN_OUT_MISO, miso_level);
            next_miso_toggle = delayed_by_ms(next_miso_toggle, MISO_TOGGLE_MS);
        }

        if (absolute_time_diff_us(now, next_drdy_toggle) <= 0) {
            drdy_level = !drdy_level;
            gpio_put(PIN_OUT_DRDY, drdy_level);
            next_drdy_toggle = delayed_by_ms(next_drdy_toggle, DRDY_TOGGLE_MS);
        }

        bool changed = false;
        for (size_t i = 0; i < ARRAY_COUNT(inputs); ++i) {
            const bool value = gpio_get(inputs[i].gpio);
            if (value != inputs[i].last_value) {
                inputs[i].last_value = value;
                printf("GPIO-TEST CHANGE | %s=%u\r\n", inputs[i].name, value ? 1u : 0u);
                changed = true;
            }
        }

        if (changed || absolute_time_diff_us(now, next_snapshot) <= 0) {
            print_input_snapshot(inputs, ARRAY_COUNT(inputs));
            if (absolute_time_diff_us(now, next_snapshot) <= 0) {
                next_snapshot = delayed_by_ms(next_snapshot, SNAPSHOT_MS);
            }
        }

        tight_loop_contents();
    }
}
