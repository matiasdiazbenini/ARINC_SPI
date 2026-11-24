
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"
#include "bus/mil1553_tx.h"

#define MY_RT_ADDR          20u
#define EXPECTED_SUBADDR    18u
#define WORD_COUNT          32u
#define TEST_TRANSACTIONS   1000u

#define RESPONSE_DELAY_US   8u
#define TEST_END_SILENCE_US 1500000ull

static const uint16_t test_data[WORD_COUNT] = {
    0x4444, 0x2222, 0x3333, 0x1111,
    0xAAAA, 0x5555, 0xFFFF, 0x0000,
    0x1234, 0x5678, 0xABCD, 0xDCBA,
    0x0F0F, 0xF0F0, 0x8001, 0x7FFE,
    0x0001, 0x0002, 0x0004, 0x0008,
    0x0010, 0x0020, 0x0040, 0x0080,
    0x0100, 0x0200, 0x0400, 0x0800,
    0x1000, 0x2000, 0x4000, 0x8000
};

static void decode_event(
    uint32_t event,
    uint16_t *word,
    bool *parity_ok)
{
    uint32_t payload =
        event & MIL1553_RX_PAYLOAD_MASK;

    *word = (uint16_t)(
        (payload >> MIL1553_RX_WORD_SHIFT)
        & MIL1553_RX_WORD_MASK
    );

    *parity_ok =
        (payload & MIL1553_RX_PARITY_MASK)
        == bus_compute_odd_parity(*word);
}

int main(void)
{
    set_sys_clock_khz(120000, true);

    stdio_init_all();

    sleep_ms(1000);

    mil1553_rx_init();
    mil1553_tx_init();

    mil1553_rx_clear();

    const uint16_t expected_command =
        BUS_1553_CMD_MAKE(
            MY_RT_ADDR,
            BUS_1553_TR_RT_TO_BC,
            EXPECTED_SUBADDR,
            WORD_COUNT
        );

    const uint16_t status_ok =
        BUS_1553_STATUS_MAKE(
            MY_RT_ADDR,
            false
        );

    uint32_t cmd_received = 0;
    uint32_t cmd_correct = 0;
    uint32_t cmd_unexpected = 0;
    uint32_t cmd_parity_errors = 0;

    uint32_t status_sent = 0;
    uint32_t data_sent = 0;
    uint32_t transactions_completed = 0;

    bool test_started = false;
    bool report_done = false;

    uint64_t last_activity_us = time_us_64();

    printf("\n========================================\n");
    printf("RT - TEST RT -> BC / WC = 32\n");
    printf("========================================\n");
    printf("Transacciones    : %u\n", TEST_TRANSACTIONS);
    printf("RT               : %u\n", MY_RT_ADDR);
    printf("Subaddress       : %u\n", EXPECTED_SUBADDR);
    printf("Word Count       : %u\n", WORD_COUNT);
    printf("COMMAND esperado : 0x%04X\n", expected_command);
    printf("STATUS           : 0x%04X\n", status_ok);
    printf("Delay configurado: %u us\n", RESPONSE_DELAY_US);
    printf("========================================\n\n");

    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();

            uint32_t type =
                event & MIL1553_RX_TYPE_MASK;

            if (type != MIL1553_RX_TYPE_CMD_STATUS)
                continue;

            uint16_t word;
            bool parity_ok;

            decode_event(
                event,
                &word,
                &parity_ok
            );

            test_started = true;
            last_activity_us = time_us_64();

            cmd_received++;

            if (!parity_ok)
            {
                cmd_parity_errors++;
                mil1553_rx_clear();
                continue;
            }

            if (word != expected_command)
            {
                cmd_unexpected++;
                mil1553_rx_clear();
                continue;
            }

            cmd_correct++;

            /*
             * Mantener los 8 us configurados.
             *
             * Respuesta continua:
             *
             * STATUS + DATA1 ... DATA32
             */

            busy_wait_us_32(RESPONSE_DELAY_US);

            mil1553_tx_send_word(
                BUS_1553_SYNC_CMD_STATUS,
                status_ok
            );

            for (uint32_t i = 0; i < WORD_COUNT; i++)
            {
                mil1553_tx_send_word(
                    BUS_1553_SYNC_DATA,
                    test_data[i]
                );
            }

            // Esperar la finalizacion del bloque completo.
            mil1553_tx_wait_idle();

            status_sent++;
            data_sent += WORD_COUNT;
            transactions_completed++;

            // Eliminar la recepcion propia.
            mil1553_rx_clear();

            last_activity_us = time_us_64();
        }

        uint64_t now_us = time_us_64();

        if (test_started &&
            !report_done &&
            now_us - last_activity_us >=
                TEST_END_SILENCE_US)
        {
            report_done = true;

            printf("\n========================================\n");
            printf("RT - RESULTADO RT -> BC / WC = 32\n");
            printf("========================================\n");

            printf("TRANSACCIONES ESPERADAS : %u\n",
                   TEST_TRANSACTIONS);

            printf("Respuestas completas    : %lu\n",
                   (unsigned long)transactions_completed);

            printf("\n--- COMMAND ---\n");
            printf("CMD recibidos           : %lu\n",
                   (unsigned long)cmd_received);

            printf("CMD correctos           : %lu\n",
                   (unsigned long)cmd_correct);

            printf("CMD inesperados         : %lu\n",
                   (unsigned long)cmd_unexpected);

            printf("CMD error paridad       : %lu\n",
                   (unsigned long)cmd_parity_errors);

            printf("\n--- TRANSMISION ---\n");
            printf("STATUS enviados         : %lu\n",
                   (unsigned long)status_sent);

            printf("DATA esperados          : %u\n",
                   TEST_TRANSACTIONS * WORD_COUNT);

            printf("DATA enviados           : %lu\n",
                   (unsigned long)data_sent);

            printf("\n--- TASAS ---\n");

            printf("COMMAND correctos: %.3f %%\n",
                100.0 * cmd_correct / TEST_TRANSACTIONS);

            printf("Respuestas completas: %.3f %%\n",
                100.0 * transactions_completed /
                TEST_TRANSACTIONS);

            printf("\n========================================\n");
            printf("FIN DEL INFORME RT\n");
            printf("========================================\n");
        }

        tight_loop_contents();
    }
}
