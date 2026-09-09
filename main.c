#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"


#define MY_RT_ADDR             3u
#define EXPECTED_SUBADDR       7u
#define EXPECTED_WORD_COUNT    4u

#define TEST_MESSAGES          1000u


/*
 * Si ya comenzó el test y pasan 1.5 segundos
 * sin recibir ninguna palabra, consideramos
 * que el BC terminó.
 */
#define TEST_END_SILENCE_US    1500000ull


/*
 * Orden esperado:
 *
 * DATA1 = 0x4444
 * DATA2 = 0x2222
 * DATA3 = 0x3333
 * DATA4 = 0x1111
 */
static const uint16_t expected_data[
    EXPECTED_WORD_COUNT
] =
{
    0x4444u,
    0x2222u,
    0x3333u,
    0x1111u
};


/*
 * Imprime una tasa porcentual con
 * tres cifras decimales.
 *
 * No requiere printf con float.
 */
static void print_rate(
    const char *name,
    uint32_t correct,
    uint32_t expected)
{
    uint32_t scaled = 0u;

    if (expected != 0u)
    {
        scaled =
            (uint32_t)(
                ((uint64_t)correct * 100000ull)
                /
                expected
            );
    }

    printf(
        "%s: %lu.%03lu %%\n",
        name,
        (unsigned long)(scaled / 1000u),
        (unsigned long)(scaled % 1000u)
    );
}


int main(void)
{
    stdio_init_all();


    /*
     * Dar tiempo al sistema USB/UART,
     * pero todavía no hay tráfico del BC
     * porque este espera 10 segundos.
     */
    sleep_ms(1000);


    /*
     * Inicializar RX PIO.
     */
    mil1553_rx_init();


    /*
     * Command esperado:
     *
     * RT  = 3
     * TR  = 0
     * SUB = 7
     * WC  = 4
     *
     * CMD = 0x18E4
     */
    const uint16_t expected_cmd =
        BUS_1553_CMD_MAKE(
            MY_RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
            EXPECTED_SUBADDR,
            EXPECTED_WORD_COUNT
        );


    // ========================================================
    // COMMAND
    // ========================================================

    uint32_t cmd_total      = 0u;
    uint32_t cmd_ok         = 0u;
    uint32_t cmd_par_err    = 0u;
    uint32_t cmd_unexpected = 0u;


    // ========================================================
    // DATA
    // ========================================================

    uint32_t data_total      = 0u;
    uint32_t data_par_ok     = 0u;
    uint32_t data_par_err    = 0u;

    uint32_t data_slot_ok    = 0u;
    uint32_t data_mismatch   = 0u;
    uint32_t data_orphan     = 0u;


    /*
     * Conteo bruto por valor.
     */
    uint32_t data_1111 = 0u;
    uint32_t data_2222 = 0u;
    uint32_t data_3333 = 0u;
    uint32_t data_4444 = 0u;

    uint32_t data_other = 0u;


    // ========================================================
    // MENSAJES
    // ========================================================

    uint32_t messages_started = 0u;
    uint32_t messages_ok      = 0u;
    uint32_t messages_bad     = 0u;


    /*
     * Estado del mensaje actualmente recibido.
     */
    bool message_active = false;
    bool message_ok     = true;

    uint8_t data_position = 0u;


    // ========================================================
    // CONTROL DEL TEST
    // ========================================================

    bool test_started = false;
    bool report_done  = false;

    uint64_t last_rx_us = 0u;


    while (true)
    {
        // ====================================================
        // RECEPCIÓN
        // ====================================================

        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();


            /*
             * El test comienza en cuanto aparece
             * la primera palabra.
             */
            test_started = true;

            last_rx_us =
                time_us_64();


            /*
             * Extraer tipo.
             */
            uint32_t type =
                event &
                MIL1553_RX_TYPE_MASK;


            /*
             * Extraer payload de 17 bits:
             *
             * bits 16..1 = WORD
             * bit 0      = PARITY
             */
            uint32_t payload =
                event &
                MIL1553_RX_PAYLOAD_MASK;


            uint16_t word =
                (uint16_t)(
                    (payload >>
                     MIL1553_RX_WORD_SHIFT)
                    &
                    MIL1553_RX_WORD_MASK
                );


            uint8_t received_parity =
                (uint8_t)(
                    payload &
                    MIL1553_RX_PARITY_MASK
                );


            uint8_t expected_parity =
                bus_compute_odd_parity(
                    word
                );


            bool parity_ok =
                (
                    received_parity
                    ==
                    expected_parity
                );


            // =================================================
            // COMMAND / STATUS
            // =================================================

            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                cmd_total++;


                /*
                 * Si llega un nuevo CMD y todavía
                 * había un mensaje anterior abierto,
                 * el anterior estaba incompleto.
                 */
                if (message_active)
                {
                    messages_bad++;

                    message_active = false;
                }


                /*
                 * Paridad incorrecta.
                 */
                if (!parity_ok)
                {
                    cmd_par_err++;

                    continue;
                }


                /*
                 * Para este ensayo esperamos
                 * exactamente CMD 0x18E4.
                 */
                if (word == expected_cmd)
                {
                    cmd_ok++;

                    messages_started++;

                    message_active = true;
                    message_ok     = true;

                    data_position = 0u;
                }
                else
                {
                    cmd_unexpected++;
                }
            }


            // =================================================
            // DATA
            // =================================================

            else if (type ==
                     MIL1553_RX_TYPE_DATA)
            {
                data_total++;


                /*
                 * Contar primero según paridad
                 * y valor bruto recibido.
                 */
                if (parity_ok)
                {
                    data_par_ok++;


                    if (word == 0x1111u)
                    {
                        data_1111++;
                    }
                    else if (word == 0x2222u)
                    {
                        data_2222++;
                    }
                    else if (word == 0x3333u)
                    {
                        data_3333++;
                    }
                    else if (word == 0x4444u)
                    {
                        data_4444++;
                    }
                    else
                    {
                        data_other++;
                    }
                }
                else
                {
                    data_par_err++;
                }


                /*
                 * DATA recibido sin que exista
                 * un Command válido activo.
                 */
                if (!message_active)
                {
                    data_orphan++;

                    continue;
                }


                /*
                 * Comprobar la posición esperada.
                 */
                if (!parity_ok)
                {
                    message_ok = false;
                }
                else
                {
                    uint16_t expected_word =
                        expected_data[
                            data_position
                        ];


                    if (word ==
                        expected_word)
                    {
                        data_slot_ok++;
                    }
                    else
                    {
                        data_mismatch++;

                        message_ok = false;
                    }
                }


                /*
                 * La posición se consume aunque
                 * esa palabra tenga un error.
                 */
                data_position++;


                /*
                 * Ya recibimos las cuatro posiciones
                 * del mensaje.
                 */
                if (data_position >=
                    EXPECTED_WORD_COUNT)
                {
                    if (message_ok)
                    {
                        messages_ok++;
                    }
                    else
                    {
                        messages_bad++;
                    }


                    message_active = false;
                }
            }
        }


        // ====================================================
        // FIN DEL TEST
        // ====================================================

        if (test_started &&
            !report_done)
        {
            uint64_t now_us =
                time_us_64();


            if ((now_us - last_rx_us)
                >= TEST_END_SILENCE_US)
            {
                /*
                 * Si quedó un mensaje abierto cuando
                 * terminó el tráfico, también cuenta
                 * como mensaje incompleto.
                 */
                if (message_active)
                {
                    messages_bad++;

                    message_active = false;
                }


                report_done = true;


                /*
                 * =================================================
                 * DESDE ACÁ RECIÉN USAMOS PRINTF
                 * =================================================
                 */


                const uint32_t expected_cmds =
                    TEST_MESSAGES;


                const uint32_t expected_data_words =
                    TEST_MESSAGES *
                    EXPECTED_WORD_COUNT;


                const uint32_t expected_total_words =
                    expected_cmds +
                    expected_data_words;


                const uint32_t correct_total_words =
                    cmd_ok +
                    data_slot_ok;


                printf("\n\n");

                printf(
                    "========================================\n"
                );

                printf(
                    "RESULTADO TEST MIL-STD-1553\n"
                );

                printf(
                    "========================================\n\n"
                );


                printf(
                    "MENSAJES ESPERADOS : %lu\n\n",
                    (unsigned long)
                        TEST_MESSAGES
                );


                // --------------------------------------------
                // COMMAND
                // --------------------------------------------

                printf(
                    "--- COMMAND ---\n"
                );

                printf(
                    "CMD esperados       : %lu\n",
                    (unsigned long)
                        expected_cmds
                );

                printf(
                    "CMD recibidos total : %lu\n",
                    (unsigned long)
                        cmd_total
                );

                printf(
                    "CMD correctos       : %lu\n",
                    (unsigned long)
                        cmd_ok
                );

                printf(
                    "CMD error paridad   : %lu\n",
                    (unsigned long)
                        cmd_par_err
                );

                printf(
                    "CMD inesperados     : %lu\n\n",
                    (unsigned long)
                        cmd_unexpected
                );


                // --------------------------------------------
                // DATA
                // --------------------------------------------

                printf(
                    "--- DATA ---\n"
                );

                printf(
                    "DATA esperados       : %lu\n",
                    (unsigned long)
                        expected_data_words
                );

                printf(
                    "DATA recibidos total : %lu\n",
                    (unsigned long)
                        data_total
                );

                printf(
                    "DATA paridad OK      : %lu\n",
                    (unsigned long)
                        data_par_ok
                );

                printf(
                    "DATA error paridad   : %lu\n",
                    (unsigned long)
                        data_par_err
                );

                printf(
                    "DATA posicion OK     : %lu\n",
                    (unsigned long)
                        data_slot_ok
                );

                printf(
                    "DATA valor incorrecto: %lu\n",
                    (unsigned long)
                        data_mismatch
                );

                printf(
                    "DATA sin CMD activo  : %lu\n\n",
                    (unsigned long)
                        data_orphan
                );


                // --------------------------------------------
                // VALORES
                // --------------------------------------------

                printf(
                    "--- VALORES DATA RECIBIDOS ---\n"
                );

                printf(
                    "0x4444 : %lu\n",
                    (unsigned long)
                        data_4444
                );

                printf(
                    "0x2222 : %lu\n",
                    (unsigned long)
                        data_2222
                );

                printf(
                    "0x3333 : %lu\n",
                    (unsigned long)
                        data_3333
                );

                printf(
                    "0x1111 : %lu\n",
                    (unsigned long)
                        data_1111
                );

                printf(
                    "OTROS  : %lu\n\n",
                    (unsigned long)
                        data_other
                );


                // --------------------------------------------
                // MENSAJES
                // --------------------------------------------

                printf(
                    "--- MENSAJES ---\n"
                );

                printf(
                    "Mensajes iniciados  : %lu\n",
                    (unsigned long)
                        messages_started
                );

                printf(
                    "Mensajes completos OK: %lu\n",
                    (unsigned long)
                        messages_ok
                );

                printf(
                    "Mensajes detectados BAD: %lu\n\n",
                    (unsigned long)
                        messages_bad
                );


                // --------------------------------------------
                // TASAS
                // --------------------------------------------

                printf(
                    "--- TASAS DE EXITO ---\n"
                );


                print_rate(
                    "Exito CMD",
                    cmd_ok,
                    expected_cmds
                );


                print_rate(
                    "Exito DATA por posicion",
                    data_slot_ok,
                    expected_data_words
                );


                print_rate(
                    "Exito palabras total",
                    correct_total_words,
                    expected_total_words
                );


                print_rate(
                    "Exito mensajes completos",
                    messages_ok,
                    TEST_MESSAGES
                );


                printf("\n");

                printf(
                    "========================================\n"
                );

                printf(
                    "FIN DEL INFORME\n"
                );

                printf(
                    "========================================\n"
                );
            }
        }


        /*
         * Sin printf durante la recepción.
         */
        tight_loop_contents();
    }
}