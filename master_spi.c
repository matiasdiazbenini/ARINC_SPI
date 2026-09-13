#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"
#include "hardware/clocks.h"


#define RT_ADDR        20u
#define SUBADDR        18u
#define WORD_COUNT     4u

#define TEST_MESSAGES  10000u


/*
 * Orden de DATA para esta prueba:
 *
 * DATA1 = 0x4444
 * DATA2 = 0x2222
 * DATA3 = 0x3333
 * DATA4 = 0x1111
 */
static const uint16_t data_test[WORD_COUNT] =
{
    0x4444u,
    0x2222u,
    0x3333u,
    0x1111u
};


int main(void)
{
    set_sys_clock_khz(120000, true);
    stdio_init_all();


    /*
     * Espera inicial para permitir que:
     *
     * - arranque el RT
     * - se inicialice el PIO RX
     * - quede listo antes de comenzar el ensayo
     */
    sleep_ms(10000);


    /*
     * Inicializar transmisor PIO.
     */
    mil1553_tx_init();


    /*
     * Receive Command:
     *
     * RT  = 3
     * TR  = 0  -> BC transmite hacia RT
     * SUB = 7
     * WC  = 4
     *
     * CMD esperado = 0x18E4
     */
    const uint16_t cmd =
        BUS_1553_CMD_MAKE(
            RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
            SUBADDR,
            WORD_COUNT
        );


    /*
     * ========================================================
     * ENSAYO
     * ========================================================
     *
     * Se transmiten 1000 mensajes.
     *
     * Cada mensaje:
     *
     * CMD  0x18E4
     * DATA 0x4444
     * DATA 0x2222
     * DATA 0x3333
     * DATA 0x1111
     *
     * Todas las palabras se entregan consecutivamente
     * al PIO.
     *
     * NO se introducen delays entre palabras.
     *
     * Entre mensajes completos:
     *
     *      500 ms
     *
     * Durante la recepción no se utiliza printf.
     * ========================================================
     */

    for (uint32_t i = 0u;
         i < TEST_MESSAGES;
         i++)
    {
        /*
         * COMMAND
         */
        mil1553_tx_send_word(
            BUS_1553_SYNC_CMD_STATUS,
            cmd
        );


        /*
         * DATA1 = 0x4444
         */
        mil1553_tx_send_word(
            BUS_1553_SYNC_DATA,
            data_test[0]
        );


        /*
         * DATA2 = 0x2222
         */
        mil1553_tx_send_word(
            BUS_1553_SYNC_DATA,
            data_test[1]
        );


        /*
         * DATA3 = 0x3333
         */
        mil1553_tx_send_word(
            BUS_1553_SYNC_DATA,
            data_test[2]
        );


        /*
         * DATA4 = 0x1111
         */
        mil1553_tx_send_word(
            BUS_1553_SYNC_DATA,
            data_test[3]
        );


        /*
         * Separación entre mensajes completos.
         */
        sleep_ms(100);
    }


    /*
     * ========================================================
     * REPORTE FINAL DEL BC
     * ========================================================
     */

    printf("\n");
    printf("========================================\n");
    printf("BC - TEST FINALIZADO\n");
    printf("========================================\n");

    printf(
        "Mensajes enviados : %lu\n",
        (unsigned long)TEST_MESSAGES
    );

    printf(
        "CMD enviados      : %lu\n",
        (unsigned long)TEST_MESSAGES
    );

    printf(
        "DATA enviados     : %lu\n",
        (unsigned long)(
            TEST_MESSAGES *
            WORD_COUNT
        )
    );

    printf(
        "Palabras totales  : %lu\n",
        (unsigned long)(
            TEST_MESSAGES *
            (WORD_COUNT + 1u)
        )
    );

    printf("========================================\n");


    /*
     * El BC queda detenido y no vuelve
     * a transmitir.
     */
    while (true)
    {
        tight_loop_contents();
    }
}