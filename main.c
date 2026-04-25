#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);

    printf("SLAVE BIT TEST\n");

    uint8_t shift_reg = 0;

    while (true) {
    int p = gpio_get(BUS_PIN_P);
    int n = gpio_get(BUS_PIN_N);

    bool valid_start = (p == 1 && n == 0) || (p == 0 && n == 1);

    if (!valid_start) {
        sleep_ms(20);
        continue;
    }

    // Caer cerca del centro del primer bit
    sleep_ms(250);

    uint8_t sync = 0;
    for (int i = 0; i < 8; i++) {
        int p = gpio_get(BUS_PIN_P);
        int n = gpio_get(BUS_PIN_N);

        int bit = (p == 1 && n == 0) ? 1 :
                  (p == 0 && n == 1) ? 0 : -1;

        if (bit < 0) {
            printf("SYNC_INVALID\n");
            break;
        }

        sync = (sync << 1) | bit;
        sleep_ms(500);
    }

    printf("SYNC=0x%02X\n", sync);

    if (sync != 0xF0) {
        printf("SYNC_ERROR\n");
        continue;
    }

    printf("SYNC DETECTADO\n");

    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        int p = gpio_get(BUS_PIN_P);
        int n = gpio_get(BUS_PIN_N);

        int bit = (p == 1 && n == 0) ? 1 :
                  (p == 0 && n == 1) ? 0 : -1;

        if (bit < 0) {
            printf("DATA_INVALID\n");
            break;
        }

        data = (data << 1) | bit;
        sleep_ms(500);
    }

    printf("DATA=0x%02X\n", data);
}
}