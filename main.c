/* RT WC32: diagnostic variant based on MIL-E-pio (main.c).
 * Replace RT main.c only. No changes to the PIO or RX/TX drivers.
 * Diagnostic timestamps are CPU observation times, not wire times.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/regs/pio.h"
#include "bus/bus.h"
#include "bus/mil1553_rx.h"
#include "bus/mil1553_tx.h"

#define MY_RT_ADDR 20u
#define EXPECTED_SUBADDR 18u
#define EXPECTED_WORD_COUNT 32u
#define TEST_TRANSACTIONS 10000u
#define RX_TIMEOUT_US 1000ull
#define TEST_END_SILENCE_US 1500000ull
#define DIAG_CAPACITY 128u
#define DATA_SM 1u
#define RXSTALL_MASK (1u << (PIO_FDEBUG_RXSTALL_LSB + DATA_SM))

static const uint16_t expected_data[EXPECTED_WORD_COUNT] = {
    0x4444,0x2222,0x3333,0x1111,0xAAAA,0x5555,0xFFFF,0x0000,
    0x1234,0x5678,0xABCD,0xDCBA,0x0F0F,0xF0F0,0x8001,0x7FFE,
    0x0001,0x0002,0x0004,0x0008,0x0010,0x0020,0x0040,0x0080,
    0x0100,0x0200,0x0400,0x0800,0x1000,0x2000,0x4000,0x8000
};
typedef enum { RT_WAIT_COMMAND=0, RT_RECEIVE_DATA } rt_state_t;
typedef struct {
    uint32_t tx, count, first_us, max_poll_us, max_read_us;
    uint32_t fifo_max, pc_end, fifo_end;
    bool timeout, rxstall, rxstall_at_command, valid;
    uint16_t words[EXPECTED_WORD_COUNT];
    bool parity[EXPECTED_WORD_COUNT];
} data_diag_t;
static data_diag_t active_diag, saved[DIAG_CAPACITY];
static uint32_t saved_count, omitted_count, stall_transactions;
static uint32_t global_fifo_max, global_poll_max, global_read_max;
static uint64_t command_us, previous_poll_us, previous_read_us;

static inline uint32_t max_u32(uint32_t a,uint32_t b) { return a>b?a:b; }
static inline bool rx_stalled(void) { return (pio1->fdebug & RXSTALL_MASK)!=0; }
static inline void clear_stall(void) { pio1->fdebug=RXSTALL_MASK; }
static void diag_begin(uint32_t tx,uint64_t now) {
    /* No FIFO clearing or PIO rearming here. */
    active_diag.tx=tx;
    active_diag.count=0;
    active_diag.first_us=0;
    active_diag.max_poll_us=0;
    active_diag.max_read_us=0;
    active_diag.fifo_max=0;
    active_diag.rxstall_at_command=rx_stalled();
    command_us=previous_poll_us=now;
    previous_read_us=0;
}
static inline void diag_poll(uint64_t now) {
    active_diag.max_poll_us=max_u32(active_diag.max_poll_us,(uint32_t)(now-previous_poll_us));
    previous_poll_us=now;
    active_diag.fifo_max=max_u32(active_diag.fifo_max,pio_sm_get_rx_fifo_level(pio1,DATA_SM));
}
static inline void diag_word(uint16_t word,bool parity_ok,uint64_t now) {
    uint32_t i=active_diag.count;
    if(i==0) active_diag.first_us=(uint32_t)(now-command_us);
    else active_diag.max_read_us=max_u32(active_diag.max_read_us,(uint32_t)(now-previous_read_us));
    previous_read_us=now;
    if(i<EXPECTED_WORD_COUNT) {
        active_diag.words[i]=word;
        active_diag.parity[i]=parity_ok;
    }
    active_diag.count=i+1;
}
static void diag_finish(bool timeout,bool valid) {
    /* Called before clearing RX on timeout, or AFTER sending STATUS.
     * For completed messages PC/FIFO may reflect own STATUS reception.
     */
    active_diag.timeout=timeout;
    active_diag.valid=valid;
    active_diag.rxstall=rx_stalled();
    active_diag.pc_end=pio_sm_get_pc(pio1,DATA_SM);
    active_diag.fifo_end=pio_sm_get_rx_fifo_level(pio1,DATA_SM);
    global_fifo_max=max_u32(global_fifo_max,active_diag.fifo_max);
    global_poll_max=max_u32(global_poll_max,active_diag.max_poll_us);
    global_read_max=max_u32(global_read_max,active_diag.max_read_us);
    if(active_diag.rxstall) stall_transactions++;
    if(!valid || active_diag.tx<=3u) {
        if(saved_count<DIAG_CAPACITY) saved[saved_count++]=active_diag;
        else omitted_count++;
    }
}
static void decode_rx_event(uint32_t event,uint16_t *word,bool *parity_ok) {
    uint32_t payload=event & MIL1553_RX_PAYLOAD_MASK;
    *word=(uint16_t)((payload >> MIL1553_RX_WORD_SHIFT) & MIL1553_RX_WORD_MASK);
    *parity_ok=((payload & MIL1553_RX_PARITY_MASK)==bus_compute_odd_parity(*word));
}
static void print_rate(const char *name,uint32_t correct,uint32_t expected) {
    uint32_t scaled=expected?(uint32_t)(((uint64_t)correct*100000ull)/expected):0u;
    printf("%s: %lu.%03lu %%\n",name,(unsigned long)(scaled/1000u),(unsigned long)(scaled%1000u));
}
static void print_diag(const data_diag_t *d) {
    uint32_t seen=0,duplicates=0,unknown=0,out_of_order=0;
    int last=-1;
    printf("\n--- DIAGNOSTICO DATA RT TX %lu ---\n",(unsigned long)d->tx);
    printf("Valida                : %s\n",d->valid?"SI":"NO");
    printf("DATA recibidos        : %lu/32\n",(unsigned long)d->count);
    printf("Inicio fase a RX1     : %lu us\n",(unsigned long)d->first_us);
    printf("Max intervalo polling : %lu us\n",(unsigned long)d->max_poll_us);
    printf("Max intervalo lecturas: %lu us\n",(unsigned long)d->max_read_us);
    printf("FIFO maxima observada : %lu / 8\n",(unsigned long)d->fifo_max);
    printf("RXSTALL al COMMAND    : %s\n",d->rxstall_at_command?"SI":"NO");
    printf("RXSTALL al cierre     : %s\n",d->rxstall?"SI":"NO");
    printf("PC SM1 al cierre      : %lu\n",(unsigned long)d->pc_end);
    printf("FIFO SM1 al cierre    : %lu\n",(unsigned long)d->fifo_end);
    printf("Cierre                : %s\n",d->timeout?"TIMEOUT (antes de rearmar)":"POST STATUS (antes de rearmar)");
    printf("RX   VALOR   PARIDAD  POSICION REAL EN PATRON\n");
    for(uint32_t i=0;i<d->count && i<EXPECTED_WORD_COUNT;i++) {
        int pos=-1;
        for(uint32_t j=0;j<EXPECTED_WORD_COUNT;j++) if(d->words[i]==expected_data[j]) {pos=(int)j;break;}
        printf("%2lu   0x%04X  %-5s    ",(unsigned long)(i+1),(unsigned)d->words[i],d->parity[i]?"OK":"ERROR");
        if(pos<0) {unknown++;printf("FUERA DEL PATRON\n");}
        else {
            uint32_t mask=1u<<(unsigned)pos;
            if(seen&mask) duplicates++;
            if(pos<=last) out_of_order++;
            seen|=mask;last=pos;
            printf("DATA%d\n",pos+1);
        }
    }
    printf("DATA del patron no observados:\n");
    for(unsigned j=0;j<EXPECTED_WORD_COUNT;j++) if(!(seen&(1u<<j))) printf("  DATA%u = 0x%04X\n",j+1,(unsigned)expected_data[j]);
    if(seen==UINT32_MAX) printf("  Ninguno\n");
    printf("Duplicados            : %lu\n",(unsigned long)duplicates);
    printf("Fuera del patron      : %lu\n",(unsigned long)unknown);
    printf("Orden no creciente    : %lu\n",(unsigned long)out_of_order);
}
int main(void) {
    set_sys_clock_khz(120000,true);
    stdio_init_all();
    sleep_ms(1000);
    mil1553_rx_init();
    mil1553_tx_init();
    mil1553_rx_clear();
    clear_stall();
    const uint16_t expected_cmd=BUS_1553_CMD_MAKE(MY_RT_ADDR,BUS_1553_TR_BC_TO_RT,EXPECTED_SUBADDR,EXPECTED_WORD_COUNT);
    const uint16_t status_ok=BUS_1553_STATUS_MAKE(MY_RT_ADDR,false);
    const uint16_t status_error=BUS_1553_STATUS_MAKE(MY_RT_ADDR,true);
    uint32_t cmd_received=0,cmd_correct=0,cmd_parity_errors=0,cmd_unexpected=0;
    const uint32_t data_expected=TEST_TRANSACTIONS*EXPECTED_WORD_COUNT;
    uint32_t data_received=0,data_correct=0,data_parity_errors=0,data_mismatch=0,sequence_errors=0;
    uint32_t transactions_started=0,transactions_ok=0,transactions_bad=0,timeout_count=0,status_sent=0,status_error_sent=0;
    rt_state_t state=RT_WAIT_COMMAND;
    uint8_t data_position=0;
    bool transaction_valid=true,test_started=false,report_done=false;
    uint64_t last_word_us=time_us_64(),last_activity_us=time_us_64();
    printf("\n========================================\nRT - TEST BC -> RT / WC = 32\n========================================\n");
    printf("Transacciones    : %lu\nRT               : %u\nSubaddress       : %u\nWord Count       : %u\nCOMMAND esperado : 0x%04X\nSTATUS OK        : 0x%04X\n========================================\n",(unsigned long)TEST_TRANSACTIONS,MY_RT_ADDR,EXPECTED_SUBADDR,EXPECTED_WORD_COUNT,(unsigned)expected_cmd,(unsigned)status_ok);
    while(true) {
        uint64_t now_us=time_us_64();
        if(state==RT_RECEIVE_DATA) diag_poll(now_us);
        if(mil1553_rx_available()) {
            uint32_t event=mil1553_rx_get();
            uint32_t type=event&MIL1553_RX_TYPE_MASK;
            uint16_t word=0;bool parity_ok=false;
            decode_rx_event(event,&word,&parity_ok);
            test_started=true;last_activity_us=now_us;
            if(state==RT_WAIT_COMMAND) {
                if(type!=MIL1553_RX_TYPE_CMD_STATUS) continue;
                cmd_received++;
                if(!parity_ok) {cmd_parity_errors++;continue;}
                if(word!=expected_cmd) {cmd_unexpected++;continue;}
                cmd_correct++;transactions_started++;
                data_position=0;transaction_valid=true;state=RT_RECEIVE_DATA;last_word_us=now_us;
                diag_begin(transactions_started,now_us);
                continue;
            }
            if(state==RT_RECEIVE_DATA) {
                if(type!=MIL1553_RX_TYPE_DATA) {sequence_errors++;transaction_valid=false;continue;}
                data_received++;
                if(!parity_ok) {data_parity_errors++;transaction_valid=false;}
                if(data_position<EXPECTED_WORD_COUNT) {
                    if(word==expected_data[data_position]) data_correct++;
                    else {data_mismatch++;transaction_valid=false;}
                } else {sequence_errors++;transaction_valid=false;}
                diag_word(word,parity_ok,now_us);
                data_position++;last_word_us=now_us;
                if(data_position>=EXPECTED_WORD_COUNT) {
                    busy_wait_us_32(8u);
                    uint16_t status_to_send=transaction_valid?status_ok:status_error;
                    mil1553_tx_send_word(BUS_1553_SYNC_CMD_STATUS,status_to_send);
                    mil1553_tx_wait_idle();
                    status_sent++;
                    if(!transaction_valid) status_error_sent++;
                    diag_finish(false,transaction_valid);
                    mil1553_rx_clear();clear_stall();
                    if(transaction_valid) transactions_ok++;else transactions_bad++;
                    data_position=0;transaction_valid=true;state=RT_WAIT_COMMAND;
                    last_word_us=time_us_64();last_activity_us=last_word_us;
                }
            }
        }
        if(state==RT_RECEIVE_DATA && (now_us-last_word_us)>=RX_TIMEOUT_US) {
            timeout_count++;transactions_bad++;
            diag_finish(true,false);
            mil1553_rx_clear();clear_stall();
            data_position=0;transaction_valid=true;state=RT_WAIT_COMMAND;
            last_word_us=time_us_64();last_activity_us=last_word_us;
        }
        if(test_started && !report_done && state==RT_WAIT_COMMAND) {
            now_us=time_us_64();
            if(now_us-last_activity_us>=TEST_END_SILENCE_US) {
                report_done=true;
                printf("\n========================================\nRT - RESULTADO BC -> RT / WC = 32\n========================================\n");
#define REPORT(label,v) printf(label " : %lu\n",(unsigned long)(v))
                REPORT("TRANSACCIONES ESPERADAS",TEST_TRANSACTIONS);
                REPORT("Iniciadas              ",transactions_started);REPORT("OK                     ",transactions_ok);REPORT("BAD                    ",transactions_bad);
                printf("\n--- COMMAND ---\n");
                REPORT("CMD recibidos          ",cmd_received);REPORT("CMD correctos          ",cmd_correct);REPORT("CMD error paridad      ",cmd_parity_errors);REPORT("CMD inesperados        ",cmd_unexpected);
                printf("\n--- DATA ---\n");
                REPORT("DATA esperados         ",data_expected);REPORT("DATA recibidos         ",data_received);REPORT("DATA correctos         ",data_correct);REPORT("DATA error paridad     ",data_parity_errors);REPORT("DATA incorrectos       ",data_mismatch);
                printf("\n--- STATUS ---\n");
                REPORT("STATUS enviados        ",status_sent);REPORT("STATUS MSG_ERROR       ",status_error_sent);
                printf("\n--- ERRORES ---\n");
                REPORT("Timeout DATA           ",timeout_count);REPORT("Errores secuencia      ",sequence_errors);
                printf("\n--- RESUMEN DIAGNOSTICO DATA RT ---\n");
                REPORT("TX con RXSTALL al cierre",stall_transactions);REPORT("FIFO maxima observada  ",global_fifo_max);REPORT("Max polling (us)       ",global_poll_max);REPORT("Max lecturas (us)      ",global_read_max);REPORT("Registros guardados    ",saved_count);REPORT("Registros no guardados ",omitted_count);
#undef REPORT
                for(uint32_t i=0;i<saved_count;i++) print_diag(&saved[i]);
                printf("\n--- TASAS ---\n");
                print_rate("CMD correctos",cmd_correct,TEST_TRANSACTIONS);print_rate("DATA recibidos",data_received,data_expected);print_rate("DATA correctos",data_correct,data_expected);print_rate("Transacciones completas",transactions_ok,TEST_TRANSACTIONS);
                printf("\nCOMMAND esperado       : 0x%04X\nSTATUS OK              : 0x%04X\n",(unsigned)expected_cmd,(unsigned)status_ok);
                printf("\n========================================\nFIN DEL INFORME RT\n========================================\n");
            }
        }
        tight_loop_contents();
    }
}
