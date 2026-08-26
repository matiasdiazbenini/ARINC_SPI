#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"


#define RT_ADDR     3u
#define SUBADDR     7u
#define WORD_COUNT  3u


static const uint16_t data_test[WORD_COUNT] =
{
    0x3333,
    0x2222,
    0x1111
};


int main(void)
{
    stdio_init_all();

    sleep_ms(1200);

    /*
     * Inicializa el transmisor físico PIO.
     */
    mil1553_tx_init();


    /*
     * Receive Command:
     *
     * RT  = 3
     * TR  = 0  -> BC transmite hacia RT
     * SUB = 7
     * WC  = 3
     *
     * CMD esperado = 0x18E3
     */
    const uint16_t cmd =
        BUS_1553_CMD_MAKE(
            RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
            SUBADDR,
            WORD_COUNT
        );


    printf("MIL-STD-1553 BC - CMD + 3 DATA\n");

    printf(
        "CMD: RT=%u TR=0 SUB=%u WC=%u -> 0x%04X\n",
        RT_ADDR,
        SUBADDR,
        WORD_COUNT,
        cmd
    );

    printf("DATA[0] = 0x%04X\n", data_test[0]);
    printf("DATA[1] = 0x%04X\n", data_test[1]);
    printf("DATA[2] = 0x%04X\n\n", data_test[2]);


    while (true)
{
    mil1553_tx_send_word(
        BUS_1553_SYNC_CMD_STATUS,
        cmd
    );

    sleep_us(5);

    mil1553_tx_send_word(
        BUS_1553_SYNC_DATA,
        data_test[0]
    );

    sleep_us(5);

    mil1553_tx_send_word(
        BUS_1553_SYNC_DATA,
        data_test[1]
    );

    sleep_us(5);

    mil1553_tx_send_word(
        BUS_1553_SYNC_DATA,
        data_test[2]
    );

    sleep_ms(500);
}
}