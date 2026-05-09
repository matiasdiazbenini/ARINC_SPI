#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

/*
 * 0 → TR=0: BC transmite datos al RT
 * 1 → TR=1: BC solicita datos al RT
 */
#define TEST_TR_MODE 0

#define RT_ADDR     3u
#define SUBADDR     2u
#define WORD_COUNT  3u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    printf("BC MASTER HIGH LEVEL API TEST\n");

#if TEST_TR_MODE == 0

    const uint16_t tx_data[WORD_COUNT] = {
        0x1234,
        0xABCD,
        0x55AA
    };

    while (true) {
        uint16_t status = 0;

        if (bc_send_to_rt(RT_ADDR,
                          SUBADDR,
                          tx_data,
                          WORD_COUNT,
                          &status)) {

            printf("BC_SEND_TO_RT|STATUS=0x%04X|RT=%u|MSG_ERROR=%u",
                   status,
                   BUS_1553_STATUS_RT(status),
                   BUS_1553_STATUS_MSG_ERROR(status));

            for (uint8_t i = 0; i < WORD_COUNT; i++) {
                printf("|TX_D%u=0x%04X", i, tx_data[i]);
            }

            printf("\n");
        }

        sleep_ms(200);
    }

#else

    while (true) {
        uint16_t status = 0;
        uint16_t rx_data[BUS_1553_MAX_DATA_WORDS] = {0};

        if (bc_request_from_rt(RT_ADDR,
                               SUBADDR,
                               rx_data,
                               WORD_COUNT,
                               &status)) {

            printf("BC_REQUEST_FROM_RT|STATUS=0x%04X|RT=%u|MSG_ERROR=%u",
                   status,
                   BUS_1553_STATUS_RT(status),
                   BUS_1553_STATUS_MSG_ERROR(status));

            /*
             * Si MSG_ERROR=0, mostramos los datos recibidos.
             * Si MSG_ERROR=1, el RT respondió error y no hay datos válidos.
             */
            if (!BUS_1553_STATUS_MSG_ERROR(status)) {
                for (uint8_t i = 0; i < WORD_COUNT; i++){
                    printf("|RX_D%u=0x%04X", i, rx_data[i]);
                }
            }

            printf("\n");
        }

        sleep_ms(200);
    }

#endif
}