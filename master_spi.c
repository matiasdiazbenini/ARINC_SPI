#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"


#define RT_ADDR     3u
#define SUBADDR     7u
#define WORD_COUNT  3u


int main(void)
{
    stdio_init_all();

    sleep_ms(1200);

    mil1553_tx_init();


    /*
     * TR = 1
     * RT = 3
     * SUB = 7
     * WC = 3
     *
     * CMD esperado = 0x1CE3
     */

    const uint16_t cmd =
        BUS_1553_CMD_MAKE(
            RT_ADDR,
            BUS_1553_TR_RT_TO_BC,
            SUBADDR,
            WORD_COUNT);


    printf("MIL1553 REAL TX TEST\n");
    printf("CMD=0x%04X\n", cmd);


   const uint16_t data_test = 0xA5A5;

    uint32_t tx_count = 0;

    while (true)
    {
        mil1553_tx_send_word(
            BUS_1553_SYNC_DATA,
            0xA5A5
        );

        sleep_ms(500);
    }
}