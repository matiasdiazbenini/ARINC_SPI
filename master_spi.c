
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/regs/pio.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"
#include "bus/mil1553_rx.h"

#define RT_ADDR                 20u
#define SUBADDR                 18u
#define WORD_COUNT              32u
#define TEST_TRANSACTIONS       1000u
#define INTER_TRANSACTION_MS    100u
#define RESPONSE_TIMEOUT_US     1500ull

#define RXSTALL_MASK \
    (1u << (PIO_FDEBUG_RXSTALL_LSB + 1u))

static const uint16_t expected_data[WORD_COUNT] = {
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

static void print_rate(
    const char *label,
    uint32_t correct,
    uint32_t expected)
{
    double rate = expected
        ? 100.0 * correct / expected
        : 0.0;

    printf("%s: %.3f %%\n", label, rate);
}

int main(void)
{
    set_sys_clock_khz(120000, true);

    stdio_init_all();

    mil1553_rx_init();
    mil1553_tx_init();

    sleep_ms(5000);

    const uint16_t command =
        BUS_1553_CMD_MAKE(
            RT_ADDR,
            BUS_1553_TR_RT_TO_BC,
            SUBADDR,
            WORD_COUNT
        );

    const uint16_t expected_status =
        BUS_1553_STATUS_MAKE(RT_ADDR, false);

    uint32_t transactions_ok = 0;
    uint32_t transactions_bad = 0;

    uint32_t commands_sent = 0;
    uint32_t commands_echo_ok = 0;

    uint32_t status_received = 0;
    uint32_t status_correct = 0;
    uint32_t status_parity_errors = 0;
    uint32_t status_timeout = 0;

    uint32_t data_received = 0;
    uint32_t data_correct = 0;
    uint32_t data_parity_errors = 0;
    uint32_t data_mismatch = 0;
    uint32_t data_timeout = 0;

    uint32_t rxstall_count = 0;
    uint32_t global_fifo_max = 0;

    printf("\n========================================\n");
    printf("BC - TEST RT -> BC / WC = 32\n");
    printf("========================================\n");
    printf("Transacciones    : %u\n", TEST_TRANSACTIONS);
    printf("COMMAND          : 0x%04X\n", command);
    printf("STATUS esperado  : 0x%04X\n", expected_status);
    printf("Gate DATA        : DOBLE IRQ0\n");
    printf("Timeout          : %llu us\n",
           (unsigned long long)RESPONSE_TIMEOUT_US);
    printf("========================================\n\n");

    for (uint32_t tx = 1;
         tx <= TEST_TRANSACTIONS;
         tx++)
    {
        mil1553_rx_clear();

        // Limpiar flag RXSTALL.
        pio1->fdebug = RXSTALL_MASK;

        bool command_seen = false;
        bool command_valid = false;

        bool status_seen = false;
        bool status_valid = false;

        bool data_valid = true;

        uint16_t status_word = 0;

        uint16_t received[WORD_COUNT] = {0};
        bool parity[WORD_COUNT] = {0};

        uint32_t data_count = 0;
        uint32_t fifo_max = 0;

        uint64_t status_us = 0;
        uint64_t first_data_us = 0;

        uint64_t start_us = time_us_64();

        // Unicamente se transmite el COMMAND.
        mil1553_tx_send_word(
            BUS_1553_SYNC_CMD_STATUS,
            command
        );

        commands_sent++;

        /*
         * No esperar TX idle ni rearmar RX.
         *
         * SM0 recibe el COMMAND propio y STATUS.
         * SM1 espera ambas IRQ automaticamente.
         */

        while ((time_us_64() - start_us)
               < RESPONSE_TIMEOUT_US)
        {
            uint32_t level =
                pio_sm_get_rx_fifo_level(pio1, 1);

            if (level > fifo_max)
                fifo_max = level;

            if (!status_seen &&
                mil1553_rx_cmd_status_available())
            {
                uint32_t event =
                    mil1553_rx_get_cmd_status();

                uint16_t word;
                bool parity_ok;

                decode_event(
                    event,
                    &word,
                    &parity_ok
                );

                // Primera palabra: COMMAND propio.
                if (!command_seen && word == command)
                {
                    command_seen = true;
                    command_valid = parity_ok;

                    if (parity_ok)
                        commands_echo_ok++;
                }
                else
                {
                    // Segunda palabra: STATUS del RT.
                    status_seen = true;
                    status_word = word;
                    status_us = time_us_64();

                    status_received++;

                    if (!parity_ok)
                        status_parity_errors++;

                    status_valid =
                        parity_ok &&
                        word == expected_status;

                    if (status_valid)
                        status_correct++;
                }
            }

            // Leer DATA sin rearmar SM1.
            if (data_count < WORD_COUNT &&
                mil1553_rx_data_available())
            {
                uint32_t event =
                    mil1553_rx_get_data();

                uint16_t word;
                bool parity_ok;

                decode_event(
                    event,
                    &word,
                    &parity_ok
                );

                if (data_count == 0)
                    first_data_us = time_us_64();

                received[data_count] = word;
                parity[data_count] = parity_ok;

                data_received++;

                if (!parity_ok)
                {
                    data_parity_errors++;
                    data_valid = false;
                }

                if (word == expected_data[data_count]
                    && parity_ok)
                {
                    data_correct++;
                }
                else
                {
                    data_mismatch++;
                    data_valid = false;
                }

                data_count++;
            }

            if (status_seen &&
                data_count == WORD_COUNT)
            {
                break;
            }

            tight_loop_contents();
        }

        bool stalled =
            (pio1->fdebug & RXSTALL_MASK) != 0;

        uint32_t pc_data =
            pio_sm_get_pc(pio1, 1);

        if (stalled)
            rxstall_count++;

        if (fifo_max > global_fifo_max)
            global_fifo_max = fifo_max;

        if (!status_seen)
            status_timeout++;

        if (data_count != WORD_COUNT)
            data_timeout++;

        bool valid =
            command_valid &&
            status_valid &&
            data_valid &&
            data_count == WORD_COUNT;

        if (valid)
            transactions_ok++;
        else
            transactions_bad++;

        printf("\n--- BC TX %lu ---\n",
               (unsigned long)tx);

        printf("COMMAND echo : %s\n",
               command_valid ? "OK" : "ERROR");

        if (status_seen)
            printf("STATUS       : 0x%04X\n", status_word);
        else
            printf("STATUS       : NO RECIBIDO\n");

        printf("STATUS valido: %s\n",
               status_valid ? "SI" : "NO");

        printf("DATA         : %lu/32\n",
               (unsigned long)data_count);

        printf("FIFO max     : %lu/8\n",
               (unsigned long)fifo_max);

        printf("RXSTALL      : %s\n",
               stalled ? "SI" : "NO");

        printf("PC SM1       : %lu\n",
               (unsigned long)pc_data);

        if (status_seen && data_count &&
            first_data_us >= status_us)
        {
            printf("STATUS a RX1 : %llu us\n",
                (unsigned long long)
                (first_data_us - status_us));
        }

        printf("Resultado    : %s\n",
               valid ? "OK" : "BAD");

        // Detalle de las primeras tres y las invalidas.
        if (tx <= 3 || !valid)
        {
            printf("RX   RECIBIDO   ESPERADO   PARIDAD\n");

            for (uint32_t i = 0; i < data_count; i++)
            {
                printf("%2lu   0x%04X     0x%04X     %s\n",
                    (unsigned long)(i + 1),
                    received[i],
                    expected_data[i],
                    parity[i] ? "OK" : "ERROR");
            }
        }

        sleep_ms(INTER_TRANSACTION_MS);
    }

    printf("\n========================================\n");
    printf("BC - RESULTADO RT -> BC / WC = 32\n");
    printf("========================================\n");

    printf("TRANSACCIONES : %u\n", TEST_TRANSACTIONS);
    printf("OK            : %lu\n", (unsigned long)transactions_ok);
    printf("BAD           : %lu\n", (unsigned long)transactions_bad);

    printf("\n--- COMMAND ---\n");
    printf("CMD enviados  : %lu\n", (unsigned long)commands_sent);
    printf("CMD echo OK   : %lu\n", (unsigned long)commands_echo_ok);

    printf("\n--- STATUS ---\n");
    printf("Recibidos     : %lu\n", (unsigned long)status_received);
    printf("Correctos     : %lu\n", (unsigned long)status_correct);
    printf("Error paridad : %lu\n", (unsigned long)status_parity_errors);
    printf("Timeout       : %lu\n", (unsigned long)status_timeout);

    printf("\n--- DATA ---\n");
    printf("Esperados     : %u\n", TEST_TRANSACTIONS * WORD_COUNT);
    printf("Recibidos     : %lu\n", (unsigned long)data_received);
    printf("Correctos     : %lu\n", (unsigned long)data_correct);
    printf("Incorrectos   : %lu\n", (unsigned long)data_mismatch);
    printf("Error paridad : %lu\n", (unsigned long)data_parity_errors);
    printf("Timeout       : %lu\n", (unsigned long)data_timeout);

    printf("\n--- DIAGNOSTICO ---\n");
    printf("FIFO max      : %lu/8\n", (unsigned long)global_fifo_max);
    printf("TX RXSTALL    : %lu\n", (unsigned long)rxstall_count);

    printf("\n--- TASAS ---\n");

    print_rate(
        "DATA correctos",
        data_correct,
        TEST_TRANSACTIONS * WORD_COUNT
    );

    print_rate(
        "Transacciones completas",
        transactions_ok,
        TEST_TRANSACTIONS
    );

    printf("\n========================================\n");
    printf("FIN DEL INFORME BC\n");
    printf("========================================\n");

    while (true)
        tight_loop_contents();
}
