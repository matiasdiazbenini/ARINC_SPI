#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"


#define RT_ADDR        3u
#define SUBADDR        7u
#define WORD_COUNT     4u

#define TEST_MESSAGES  1000u


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
    stdio_init_all();


    /*
     * Espera inicial para dar tiempo a que:
     *
     * - arranque el RT
     * - se inicialice su PIO RX
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
     * TR  = 0
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
     * al transmisor PIO.
     *
     * No se utiliza sleep_us() entre palabras.
     *
     * Entre mensajes completos esperamos 500 ms.
     *
     * Durante el ensayo no se usa printf.
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
         * Separación entre mensajes.
         */
        sleep_ms(500);
    }


    /*
     * Recién cuando termina el ensayo
     * mostramos el resumen del BC.
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
            TEST_MESSAGES * WORD_COUNT
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
     * a transmitir ninguna palabra.
     */
    while (true)
    {
        tight_loop_contents();
    }
}