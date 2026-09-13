#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"
#include "hardware/clocks.h"


/* ============================================================
 * CONFIGURACIÓN DEL ENSAYO
 * ============================================================
 */

#define MY_RT_ADDR             20u
#define EXPECTED_SUBADDR       18u
#define EXPECTED_WORD_COUNT    4u

#define TEST_MESSAGES          10000u


/*
 * Si el test ya comenzó y pasan 1.5 segundos
 * sin recibir ninguna palabra, consideramos
 * que el BC terminó.
 */
#define TEST_END_SILENCE_US    1500000ull


/*
 * Cantidad máxima de mensajes defectuosos
 * que podemos registrar.
 */
#define MAX_BAD_RECORDS        TEST_MESSAGES


/* ============================================================
 * DATA ESPERADOS
 * ============================================================
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


/* ============================================================
 * REGISTRO DE MENSAJE DEFECTUOSO
 * ============================================================
 *
 * seen_mask:
 *
 * bit 0 -> DATA1 = 0x4444
 * bit 1 -> DATA2 = 0x2222
 * bit 2 -> DATA3 = 0x3333
 * bit 3 -> DATA4 = 0x1111
 */

typedef struct
{
    uint16_t message_number;
    uint8_t  seen_mask;

} bad_message_record_t;


static bad_message_record_t
    bad_records[MAX_BAD_RECORDS];

static uint32_t bad_record_count = 0u;


/* ============================================================
 * GUARDAR MENSAJE BAD
 * ============================================================
 */

static void save_bad_message(
    uint16_t message_number,
    uint8_t seen_mask)
{
    if (bad_record_count <
        MAX_BAD_RECORDS)
    {
        bad_records[
            bad_record_count
        ].message_number =
            message_number;

        bad_records[
            bad_record_count
        ].seen_mask =
            seen_mask;

        bad_record_count++;
    }
}


/* ============================================================
 * IDENTIFICAR SLOT SEGÚN VALOR
 * ============================================================
 *
 * 0 -> 0x4444
 * 1 -> 0x2222
 * 2 -> 0x3333
 * 3 -> 0x1111
 *
 * -1 -> otro valor
 */

static int data_slot_from_word(
    uint16_t word)
{
    if (word == 0x4444u)
    {
        return 0;
    }

    if (word == 0x2222u)
    {
        return 1;
    }

    if (word == 0x3333u)
    {
        return 2;
    }

    if (word == 0x1111u)
    {
        return 3;
    }

    return -1;
}


/* ============================================================
 * IMPRIMIR TASA
 * ============================================================
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
                ((uint64_t)correct *
                 100000ull)
                /
                expected
            );
    }

    printf(
        "%s: %lu.%03lu %%\n",
        name,
        (unsigned long)(
            scaled / 1000u
        ),
        (unsigned long)(
            scaled % 1000u
        )
    );
}


/* ============================================================
 * IMPRIMIR DATA FALTANTES
 * ============================================================
 */

static void print_missing_data(
    uint8_t seen_mask)
{
    if ((seen_mask &
         (1u << 0)) == 0u)
    {
        printf(
            " DATA1(0x4444)"
        );
    }

    if ((seen_mask &
         (1u << 1)) == 0u)
    {
        printf(
            " DATA2(0x2222)"
        );
    }

    if ((seen_mask &
         (1u << 2)) == 0u)
    {
        printf(
            " DATA3(0x3333)"
        );
    }

    if ((seen_mask &
         (1u << 3)) == 0u)
    {
        printf(
            " DATA4(0x1111)"
        );
    }
}


/* ============================================================
 * MAIN
 * ============================================================
 */

int main(void)
{
    set_sys_clock_khz(120000, true);
    stdio_init_all();


    /*
     * Dar tiempo a que se inicialice
     * USB / terminal serie.
     *
     * El BC espera 10 segundos antes
     * de comenzar a transmitir.
     */
    sleep_ms(1000);


    /*
     * Inicializar receptor PIO.
     */
    mil1553_rx_init();


    /*
     * ========================================================
     * MENSAJE DE DIAGNÓSTICO
     * ========================================================
     *
     * Este printf ocurre ANTES de que el BC
     * comience a transmitir.
     *
     * Sirve para comprobar que:
     *
     * - la Pico RT arrancó
     * - main() está funcionando
     * - el USB/UART está funcionando
     * - mil1553_rx_init() terminó
     *
     * Después de este mensaje NO se vuelve
     * a utilizar printf hasta terminar el test.
     */

    printf("\n");
    printf("========================================\n");
    printf("RT INICIALIZADO - ESPERANDO TRAFICO\n");
    printf("========================================\n");
    printf("\n");


    /*
     * ========================================================
     * COMMAND ESPERADO
     * ========================================================
     *
     * RT  = 20
     * TR  = 0
     * SUB = 18
     * WC  = 4
     *
     * CMD esperado = 0xA244
     */

    const uint16_t expected_cmd =
        BUS_1553_CMD_MAKE(
            MY_RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
            EXPECTED_SUBADDR,
            EXPECTED_WORD_COUNT
        );


    /* ========================================================
     * CONTADORES COMMAND
     * ========================================================
     */

    uint32_t cmd_total      = 0u;
    uint32_t cmd_ok         = 0u;
    uint32_t cmd_par_err    = 0u;
    uint32_t cmd_unexpected = 0u;


    /* ========================================================
     * CONTADORES DATA
     * ========================================================
     */

    uint32_t data_total    = 0u;
    uint32_t data_par_ok   = 0u;
    uint32_t data_par_err  = 0u;

    uint32_t data_slot_ok  = 0u;
    uint32_t data_mismatch = 0u;
    uint32_t data_orphan   = 0u;


    /*
     * Conteo bruto por valor.
     */
    uint32_t data_1111 = 0u;
    uint32_t data_2222 = 0u;
    uint32_t data_3333 = 0u;
    uint32_t data_4444 = 0u;

    uint32_t data_other = 0u;


    /* ========================================================
     * CONTADORES DE MENSAJES
     * ========================================================
     */

    uint32_t messages_started = 0u;
    uint32_t messages_ok      = 0u;
    uint32_t messages_bad     = 0u;


    /* ========================================================
     * ESTADO DEL MENSAJE ACTUAL
     * ========================================================
     */

    bool message_active = false;
    bool message_ok     = true;

    uint8_t data_position = 0u;


    /*
     * Máscara de DATA realmente vistos.
     *
     * bit 0 = 4444
     * bit 1 = 2222
     * bit 2 = 3333
     * bit 3 = 1111
     */
    uint8_t seen_mask = 0u;


    /*
     * Número del mensaje actualmente
     * en recepción.
     */
    uint16_t current_message_number =
        0u;


    /* ========================================================
     * CONTROL DEL TEST
     * ========================================================
     */

    bool test_started = false;
    bool report_done  = false;

    uint64_t last_rx_us = 0u;


    /* ========================================================
     * LOOP PRINCIPAL
     * ========================================================
     */

    while (true)
    {
        /* ====================================================
         * RECEPCIÓN
         * ====================================================
         */

        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();


            /*
             * El test comienza cuando aparece
             * la primera palabra recibida.
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
             * Extraer payload.
             *
             * bits 16..1 = WORD
             * bit 0      = PARITY
             */
            uint32_t payload =
                event &
                MIL1553_RX_PAYLOAD_MASK;


            uint16_t word =
                (uint16_t)(
                    (
                        payload >>
                        MIL1553_RX_WORD_SHIFT
                    )
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
                    received_parity ==
                    expected_parity
                );


            /* =================================================
             * COMMAND / STATUS
             * =================================================
             */

            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                cmd_total++;


                /*
                 * Si aparece un nuevo CMD mientras
                 * había un mensaje anterior abierto,
                 * el anterior estaba incompleto.
                 */
                if (message_active)
                {
                    messages_bad++;


                    save_bad_message(
                        current_message_number,
                        seen_mask
                    );


                    message_active = false;
                }


                /*
                 * CMD con error de paridad.
                 */
                if (!parity_ok)
                {
                    cmd_par_err++;

                    continue;
                }


                /*
                 * CMD correcto.
                 */
                if (word ==
                    expected_cmd)
                {
                    cmd_ok++;

                    messages_started++;


                    current_message_number =
                        (uint16_t)
                            messages_started;


                    message_active = true;
                    message_ok     = true;

                    data_position = 0u;
                    seen_mask     = 0u;
                }
                else
                {
                    cmd_unexpected++;
                }
            }


            /* =================================================
             * DATA
             * =================================================
             */

            else if (type ==
                     MIL1553_RX_TYPE_DATA)
            {
                data_total++;


                /*
                 * ------------------------------------------------
                 * CONTADORES BRUTOS
                 * ------------------------------------------------
                 */

                if (parity_ok)
                {
                    data_par_ok++;


                    if (word ==
                        0x1111u)
                    {
                        data_1111++;
                    }
                    else if (word ==
                             0x2222u)
                    {
                        data_2222++;
                    }
                    else if (word ==
                             0x3333u)
                    {
                        data_3333++;
                    }
                    else if (word ==
                             0x4444u)
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
                 * DATA sin Command activo.
                 */
                if (!message_active)
                {
                    data_orphan++;

                    continue;
                }


                /*
                 * ------------------------------------------------
                 * REGISTRAR QUÉ DATA APARECIÓ
                 * ------------------------------------------------
                 */

                if (parity_ok)
                {
                    int slot =
                        data_slot_from_word(
                            word
                        );


                    if (slot >= 0)
                    {
                        seen_mask |=
                            (uint8_t)(
                                1u <<
                                (uint)slot
                            );
                    }
                }


                /*
                 * ------------------------------------------------
                 * COMPROBACIÓN SECUENCIAL
                 * ------------------------------------------------
                 */

                if (!parity_ok)
                {
                    message_ok =
                        false;
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

                        message_ok =
                            false;
                    }
                }


                /*
                 * Consumir posición.
                 */
                data_position++;


                /*
                 * Si recibimos cuatro DATA,
                 * cerrar mensaje.
                 */
                if (data_position >=
                    EXPECTED_WORD_COUNT)
                {
                    if (message_ok &&
                        seen_mask == 0x0Fu)
                    {
                        messages_ok++;
                    }
                    else
                    {
                        messages_bad++;


                        save_bad_message(
                            current_message_number,
                            seen_mask
                        );
                    }


                    message_active =
                        false;
                }
            }
        }


        /* ====================================================
         * FIN DEL TEST
         * ====================================================
         */

        if (test_started &&
            !report_done)
        {
            uint64_t now_us =
                time_us_64();


            if ((now_us -
                 last_rx_us)
                >=
                TEST_END_SILENCE_US)
            {
                /*
                 * Mensaje incompleto al finalizar.
                 */
                if (message_active)
                {
                    messages_bad++;


                    save_bad_message(
                        current_message_number,
                        seen_mask
                    );


                    message_active =
                        false;
                }


                report_done =
                    true;


                /* =================================================
                 * DESDE ACÁ RECIÉN SE UTILIZA PRINTF
                 * =================================================
                 */


                const uint32_t expected_cmds =
                    TEST_MESSAGES;


                const uint32_t
                    expected_data_words =
                        TEST_MESSAGES *
                        EXPECTED_WORD_COUNT;


                const uint32_t
                    expected_total_words =
                        expected_cmds +
                        expected_data_words;


                const uint32_t
                    correct_total_words =
                        cmd_ok +
                        data_slot_ok;


                const uint32_t
                    captured_total_words =
                        cmd_total +
                        data_total;


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


                /* --------------------------------------------
                 * COMMAND
                 * --------------------------------------------
                 */

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


                /* --------------------------------------------
                 * DATA
                 * --------------------------------------------
                 */

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


                /* --------------------------------------------
                 * VALORES DATA
                 * --------------------------------------------
                 */

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


                /* --------------------------------------------
                 * MENSAJES
                 * --------------------------------------------
                 */

                printf(
                    "--- MENSAJES ---\n"
                );

                printf(
                    "Mensajes iniciados   : %lu\n",
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


                /* --------------------------------------------
                 * TASAS
                 * --------------------------------------------
                 */

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
                    "Captura fisica total",
                    captured_total_words,
                    expected_total_words
                );


                print_rate(
                    "Exito mensajes completos",
                    messages_ok,
                    TEST_MESSAGES
                );


                /* --------------------------------------------
                 * DETALLE MENSAJES BAD
                 * --------------------------------------------
                 */

                printf("\n");

                printf(
                    "--- DETALLE MENSAJES BAD ---\n"
                );


                if (bad_record_count == 0u)
                {
                    printf(
                        "No se detectaron mensajes defectuosos.\n"
                    );
                }
                else
                {
                    for (uint32_t i = 0u;
                         i < bad_record_count;
                         i++)
                    {
                        printf(
                            "Mensaje %u : faltan",
                            (unsigned int)
                                bad_records[i].
                                message_number
                        );


                        print_missing_data(
                            bad_records[i].
                            seen_mask
                        );


                        printf("\n");
                    }
                }


                printf(
                    "\nTotal mensajes BAD registrados: %lu\n",
                    (unsigned long)
                        bad_record_count
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