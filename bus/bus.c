#include "bus.h"
#include <stdio.h>
#include "hardware/gpio.h"
#if BUS_USE_PIO_TX
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "manchester_tx.pio.h"
#endif
#include "pico/stdlib.h"

#if BUS_USE_PIO_TX
static PIO bus_tx_pio = pio0;
static const uint bus_tx_sm = 0u;
static uint bus_tx_offset = 0u;
static bool bus_tx_pio_initialized = false;

static void bus_pio_take_tx_pins(void) {
    gpio_set_function(BUS_PIN_P, GPIO_FUNC_PIO0);
    gpio_set_function(BUS_PIN_N, GPIO_FUNC_PIO0);
    pio_sm_set_consecutive_pindirs(bus_tx_pio, bus_tx_sm, BUS_PIN_P, 2u, true);
}

static void bus_release_pins_to_sio(void) {
    gpio_set_function(BUS_PIN_P, GPIO_FUNC_SIO);
    gpio_set_function(BUS_PIN_N, GPIO_FUNC_SIO);
}

static void bus_tx_pio_init(void) {
    if (bus_tx_pio_initialized) {
        return;
    }

    bus_tx_offset = pio_add_program(bus_tx_pio, &manchester_tx_program);

    pio_sm_config c = manchester_tx_program_get_default_config(bus_tx_offset);

    sm_config_set_sideset_pins(&c, BUS_PIN_P);

    /*
     * MSB first.
     * bus_send_bit() carga 0x80000000 para bit=1 y 0x00000000 para bit=0.
     */
    sm_config_set_out_shift(&c, false, false, 32);

    /*
     * Valor conservador inicial. La temporización efectiva todavía se
     * completa con sleep_us(BIT_PERIOD_US) en bus_send_bit().
     */
    const float pio_cycles_per_bit = 16.0f;
    const float clkdiv =
        ((float)clock_get_hz(clk_sys) * ((float)BIT_PERIOD_US / 1000000.0f)) /
        pio_cycles_per_bit;

    sm_config_set_clkdiv(&c, clkdiv);

    pio_gpio_init(bus_tx_pio, BUS_PIN_P);
    pio_gpio_init(bus_tx_pio, BUS_PIN_N);

    bus_pio_take_tx_pins();

    pio_sm_init(bus_tx_pio, bus_tx_sm, bus_tx_offset, &c);
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);

    bus_tx_pio_initialized = true;
}
#endif

#if !BUS_USE_PIO_TX
static void bus_send_bit_software(bool bit) {
    const uint32_t half_period_us = BIT_PERIOD_US / 2u;

    if (bit) {
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_us(half_period_us);

        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_us(half_period_us);
    } else {
        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_us(half_period_us);

        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_us(half_period_us);
    }
}
#endif

static int bus_read_diff_level(void) {
    const int p = gpio_get(BUS_PIN_P);
    const int n = gpio_get(BUS_PIN_N);

    if (p == 1 && n == 0) {
        return 1;
    }

    if (p == 0 && n == 1) {
        return 0;
    }

    return -1;
}

void bus_init(void) {
    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);

#if BUS_USE_PIO_TX
    bus_tx_pio_init();
#endif

    bus_set_rx_mode();
}

void bus_set_tx_mode(void) {
#if BUS_USE_PIO_TX
    bus_tx_pio_init();
    bus_pio_take_tx_pins();
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);
#else
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
#endif
}

void bus_set_rx_mode(void) {
#if BUS_USE_PIO_TX
    if (bus_tx_pio_initialized) {
        /*
         * Esperar a que la FIFO se vacíe.
         * Ojo: esto no garantiza que el último byte ya terminó físicamente.
         */
        pio_sm_drain_tx_fifo(bus_tx_pio, bus_tx_sm);

        /*
         * Espera extra para que termine de salir el último byte.
         * Cada byte son 8 bits, usamos margen de 10 bits.
         */
        sleep_us(10 * BIT_PERIOD_US);

        pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);
    }

    bus_release_pins_to_sio();
#endif

    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);
}

void bus_idle(void) {
#if BUS_USE_PIO_TX
    if (bus_tx_pio_initialized) {
        /*
         * Esperar a que la FIFO se vacíe.
         */
        pio_sm_drain_tx_fifo(bus_tx_pio, bus_tx_sm);

        /*
         * Espera extra para no cortar el último byte.
         */
        sleep_us(10 * BIT_PERIOD_US);

        pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);
    }

    bus_release_pins_to_sio();

    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
#else
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
#endif

    gpio_put(BUS_PIN_P, 0);
    gpio_put(BUS_PIN_N, 0);
}

void bus_send_bit(bool bit) {
#if BUS_USE_PIO_TX
    bus_tx_pio_init();

    if (gpio_get_function(BUS_PIN_P) != GPIO_FUNC_PIO0 ||
        gpio_get_function(BUS_PIN_N) != GPIO_FUNC_PIO0) {
        bus_pio_take_tx_pins();
    }

    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);

    const uint32_t tx_word = bit ? 0x80000000u : 0x00000000u;
    pio_sm_put_blocking(bus_tx_pio, bus_tx_sm, tx_word);

    /*
     * Espera conservadora:
     * evita que el código pase a RX/IDLE o cargue el siguiente bit antes
     * de que el PIO termine de consumir el bit actual.
     */
#else
    bus_send_bit_software(bit);
#endif
}

bool bus_read_bit(bool *bit) {
    int first_half = 0;
    int second_half = 0;

    if (bit == NULL) {
        return false;
    }

    first_half = bus_read_diff_level();
    if (first_half < 0) {
        return false;
    }

    sleep_us(BIT_PERIOD_US / 2u);

    second_half = bus_read_diff_level();
    if (second_half < 0) {
        return false;
    }

    if (first_half == 1 && second_half == 0) {
        *bit = true;
    } else if (first_half == 0 && second_half == 1) {
        *bit = false;
    } else {
        return false;
    }

    sleep_us(BIT_PERIOD_US / 2u);
    return true;
}

void bus_send_byte(uint8_t byte) {
#if BUS_USE_PIO_TX
    bus_tx_pio_init();
    bus_pio_take_tx_pins();
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);

    uint32_t v = ((uint32_t)byte) << 24u;  // MSB first
    pio_sm_put_blocking(bus_tx_pio, bus_tx_sm, v);
#else
    for (int i = 7; i >= 0; i--) {
        bus_send_bit(((byte >> i) & 1u) != 0u);
    }
#endif
}

bool bus_read_byte(uint8_t *byte) {
    uint8_t value = 0;

    if (byte == NULL) {
        return false;
    }

    for (int i = 0; i < 8; i++) {
        bool bit = false;

        if (!bus_read_bit(&bit)) {
            return false;
        }

        value = (uint8_t)((value << 1) | (bit ? 1u : 0u));
    }

    *byte = value;
    return true;
}

static void bus_send_preamble(void){
    for(uint8_t i = 0; i < SYNC_PREAMBLE_COUNT; i++){
        bus_send_byte(SYNC_PREAMBLE_BYTE);
    }
}
void bus_send_sync_cmd_status(void) {
    bus_send_preamble();
    bus_send_byte(SYNC_CMD_STATUS);
}
void bus_send_sync_data(void) {
    bus_send_preamble();
    bus_send_byte(SYNC_DATA);
}
bool bus_read_sync(uint8_t *type) {
    uint8_t byte = 0;
    uint8_t preamble_seen = 0;

    while(true){
        if(!bus_read_byte(&byte)){
            return false;
        }

        printf("SYNC_SCAN_BYTE=%02X\n", byte);

        if(byte == SYNC_PREAMBLE_BYTE){
            if(preamble_seen < SYNC_PREAMBLE_COUNT){
                preamble_seen++;
            }
            continue;
        }
        if(preamble_seen >= SYNC_PREAMBLE_COUNT){
            if(byte == SYNC_CMD_STATUS){
                if(type != NULL){
                    *type = BUS_SYNC_TYPE_CMD_STATUS;
                }
                return true;
            }
            if(byte == SYNC_DATA){
                if(type != NULL){
                    *type = BUS_SYNC_TYPE_DATA;
                }
                return true;
            }
        }
        preamble_seen = 0;
    }
}


bool bus_read_word16(uint16_t *word) {
    uint16_t value = 0;

    if (word == NULL) {
        return false;
    }

    for (int i = 0; i < 16; i++) {
        bool bit = false;

        if (!bus_read_bit(&bit)) {
            return false;
        }

        value = (uint16_t)((value << 1) | (bit ? 1u : 0u));
    }

    *word = value;
    return true;
}

uint8_t bus_compute_odd_parity(uint16_t word) {
    uint8_t parity = 0u;

    for (int i = 0; i < 16; i++) {
        parity ^= (uint8_t)((word >> i) & 1u);
    }

    return (uint8_t)(parity ^ 1u);
}
void bus_send_word16(uint16_t word) {
#if BUS_USE_PIO_TX
    bus_send_byte((uint8_t)((word >> 8) & 0xFFu));
    bus_send_byte((uint8_t)(word & 0xFFu));
#else
    for (int i = 15; i >= 0; i--) {
        bus_send_bit(((word >> i) & 1u) != 0u);
    }
#endif
}
void bus_send_status_word(uint8_t rt_addr, bool msg_error) {
    uint16_t status = BUS_1553_STATUS_MAKE(rt_addr, msg_error);
    bus_send_byte(0xF0);
    bus_send_word16(status);
}
void bus_send_word16_parity(uint16_t word) {
    bus_send_word16(word);
    bus_send_bit(bus_compute_odd_parity(word) != 0u);
}
void bus_send_packet_checked(uint16_t cmd, const uint16_t data[], uint8_t wc) {
    uint16_t chk = cmd;

    bus_send_byte(0xF0);
    bus_send_word16(cmd);

    bus_send_byte(0x0F);

    for (uint8_t i = 0; i < wc; i++) {
        uint16_t word = data[i];

        bus_send_word16(word);
        chk ^= word;
    }

    bus_send_word16(chk);
}

bool bus_read_word16_parity(uint16_t *word) {
    uint16_t value = 0;
    bool parity_bit = false;

    if (word == NULL) {
        return false;
    }

    if (!bus_read_word16(&value)) {
        return false;
    }

    if (!bus_read_bit(&parity_bit)) {
        return false;
    }

    if ((parity_bit ? 1u : 0u) != bus_compute_odd_parity(value)) {
        return false;
    }

    *word = value;
    return true;
}