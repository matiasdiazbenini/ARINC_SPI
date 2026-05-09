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
#define SUBADDR     7u
#define WORD_COUNT  3u

/*
 * Para probar errores en TR=1:
 *
 * 1 → el BC lee solo STATUS.
 *     Usar cuando SUBADDR es inválida, por ejemplo SUBADDR=7.
 *
 * 0 → el BC lee STATUS + DATA.
 *     Usar cuando SUBADDR es válida, por ejemplo SUBADDR=2.
 */
#define TR1_READ_STATUS_ONLY 0

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    const uint8_t rt_addr = RT_ADDR;
    const uint8_t subaddr = SUBADDR;
    const uint8_t wc = WORD_COUNT;

#if TEST_TR_MODE == 0

    const uint16_t cmd = BUS_1553_CMD_MAKE(
        rt_addr,
        BUS_1553_TR_BC_TO_RT,
        subaddr,
        wc
    );

    const uint16_t tx_data[WORD_COUNT] = {
        0x1234,
        0xABCD,
        0x55AA
    };

    printf("BC MASTER TEST | MODE=TR0 BC_TO_RT PARITY\n");

    while (true) {
        bus_set_tx_mode();

        bus_send_packet_parity(cmd, tx_data, wc);

        sleep_us(50 * BIT_PERIOD_US);

        bus_idle();
        bus_set_rx_mode();

        uint16_t status = 0;
        bool status_ok = false;

        for (int attempt = 0; attempt < 20; attempt++) {
            if (bus_read_status_word_parity_pio(&status)) {
                status_ok = true;
                break;
            }

            sleep_ms(10);
        }

        if (status_ok) {
            printf("BC_TO_RT_PARITY_OK|CMD=0x%04X|STATUS=0x%04X|RT=%u|MSG_ERROR=%u",
                   cmd,
                   status,
                   BUS_1553_STATUS_RT(status),
                   BUS_1553_STATUS_MSG_ERROR(status));

            for (uint8_t i = 0; i < wc; i++) {
                printf("|TX_D%u=0x%04X", i, tx_data[i]);
            }

            printf("\n");
        } else {
            //printf("BC_TO_RT_STATUS_NOT_FOUND|CMD=0x%04X\n", cmd);
        }

        sleep_ms(200);
    }

#else

    const uint16_t cmd = BUS_1553_CMD_MAKE(
        rt_addr,
        BUS_1553_TR_RT_TO_BC,
        subaddr,
        wc
    );

    printf("BC MASTER TEST | MODE=TR1 RT_TO_BC PARITY\n");

    while (true) {
        bus_set_tx_mode();

        bus_send_command_word_parity(cmd);

        sleep_us(50 * BIT_PERIOD_US);

        bus_idle();
        bus_set_rx_mode();

#if TR1_READ_STATUS_ONLY

        /*
         * Modo para probar error:
         * El RT responde solo STATUS + PARITY.
         */
        uint16_t status = 0;
        bool status_ok = false;

        for (int attempt = 0; attempt < 20; attempt++) {
            if (bus_read_status_word_parity_pio(&status)) {
                status_ok = true;
                break;
            }

            sleep_ms(10);
        }

        if (status_ok) {
            printf("RT_TO_BC_STATUS|CMD=0x%04X|STATUS=0x%04X|RT=%u|MSG_ERROR=%u\n",
                   cmd,
                   status,
                   BUS_1553_STATUS_RT(status),
                   BUS_1553_STATUS_MSG_ERROR(status));
        } else {
            printf("RT_TO_BC_STATUS_NOT_FOUND|CMD=0x%04X\n", cmd);
        }

#else

        /*
         * Modo normal:
         * El RT responde STATUS + DATA.
         */
        uint16_t status = 0;
        uint16_t rx_data[BUS_1553_MAX_DATA_WORDS] = {0};
        bool response_ok = false;

        for (int attempt = 0; attempt < 20; attempt++) {
            if (bus_read_status_data_parity_pio(&status, rx_data, wc)) {
                response_ok = true;
                break;
            }

            sleep_ms(10);
        }

        if (response_ok) {
            printf("RT_TO_BC_PARITY_OK|CMD=0x%04X|STATUS=0x%04X|RT=%u|MSG_ERROR=%u",
                   cmd,
                   status,
                   BUS_1553_STATUS_RT(status),
                   BUS_1553_STATUS_MSG_ERROR(status));

            for (uint8_t i = 0; i < wc; i++) {
                printf("|RX_D%u=0x%04X", i, rx_data[i]);
            }

            printf("\n");
        } else {
            //printf("RT_TO_BC_DATA_NOT_FOUND|CMD=0x%04X\n", cmd);
        }

#endif

        sleep_ms(200);
    }

#endif
}