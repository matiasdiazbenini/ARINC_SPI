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

#define TEST_TRANSACTIONS 1024u
#define MAX_WC 32u
#define INTER_TRANSACTION_MS 100u
#define RESPONSE_TIMEOUT_US 1500ull
#define RXSTALL_MASK (1u << (PIO_FDEBUG_RXSTALL_LSB + 1u))
static const uint16_t pattern[MAX_WC]={
    0x4444,0x2222,0x3333,0x1111,0xAAAA,0x5555,0xFFFF,0x0000,
    0x1234,0x5678,0xABCD,0xDCBA,0x0F0F,0xF0F0,0x8001,0x7FFE,
    0x0001,0x0002,0x0004,0x0008,0x0010,0x0020,0x0040,0x0080,
    0x0100,0x0200,0x0400,0x0800,0x1000,0x2000,0x4000,0x8000
};
static void decode(uint32_t e,uint16_t *w,bool *p){
    uint32_t a=e&MIL1553_RX_PAYLOAD_MASK;
    *w=(uint16_t)((a>>MIL1553_RX_WORD_SHIFT)&MIL1553_RX_WORD_MASK);
    *p=(a&MIL1553_RX_PARITY_MASK)==bus_compute_odd_parity(*w);
}
static const char *direction(unsigned tr){return tr?"RT -> BC":"BC -> RT";}
int main(void){
    set_sys_clock_khz(120000,true);
    stdio_init_all(); mil1553_rx_init(); mil1553_tx_init(); sleep_ms(5000);
    const uint16_t status_expected=BUS_1553_STATUS_MAKE(20u,false);
    uint32_t started[2]={0},ok[2]={0},status_rx[2]={0},status_ok[2]={0},status_timeout[2]={0},status_parity_err[2]={0};
    uint32_t expected_rx=0, coverage[2][MAX_WC]={0}, coverage_ok[2][MAX_WC]={0};
    uint32_t cmd_echo_ok=0,tx_data=0,rx_data=0,correct_data=0,data_mismatch=0,data_parity_err=0,data_timeout=0,stall_count=0,global_fifo=0;
    printf("\nBC - TEST BIDIRECCIONAL WC 1..32\nTransacciones: %u\n",TEST_TRANSACTIONS);
    for(uint32_t tx=1;tx<=TEST_TRANSACTIONS;tx++){
        unsigned tr=(tx-1u)&1u;
        unsigned wc=1u+(((tx-1u)/2u)%MAX_WC);
        uint16_t command=BUS_1553_CMD_MAKE(20u,tr,18u,wc);
        bool echo_seen=false,echo_valid=false,status_seen=false,status_valid=false,data_valid=true;
        uint16_t status=0,received[MAX_WC]={0}; bool parity[MAX_WC]={0};
        uint32_t count=0,fifo_max=0;
        uint64_t status_us=0,first_data_us=0;
        mil1553_rx_clear(); pio1->fdebug=RXSTALL_MASK;
        started[tr]++;
        coverage[tr][wc-1u]++;
        if(tr==BUS_1553_TR_RT_TO_BC)expected_rx+=wc;
        uint64_t start_us=time_us_64();
        mil1553_tx_send_word(BUS_1553_SYNC_CMD_STATUS,command);
        if(tr==BUS_1553_TR_BC_TO_RT){
            // Secuencia validada: bloque continuo, luego rearme ligero de SM0.
            for(unsigned i=0;i<wc;i++) mil1553_tx_send_word(BUS_1553_SYNC_DATA,pattern[i]);
            tx_data+=wc;
            mil1553_tx_wait_idle();
            mil1553_rx_cmd_status_rearm();
            start_us=time_us_64();
            // SM1 no se usa en esta direccion.
        }
        while(time_us_64()-start_us<RESPONSE_TIMEOUT_US){
            if(!status_seen && mil1553_rx_cmd_status_available()){
                uint16_t w;bool p;decode(mil1553_rx_get_cmd_status(),&w,&p);
                if(tr==BUS_1553_TR_RT_TO_BC && !echo_seen && w==command){
                    echo_seen=true;echo_valid=p;if(p)cmd_echo_ok++;
                }else{
                    status_seen=true;status=w;status_us=time_us_64();status_rx[tr]++;
                    if(!p)status_parity_err[tr]++;
                    status_valid=p && w==status_expected;
                    if(status_valid)status_ok[tr]++;
                }
            }
            if(tr==BUS_1553_TR_RT_TO_BC){
                uint32_t level=pio_sm_get_rx_fifo_level(pio1,1u);
                if(level>fifo_max)fifo_max=level;
                if(count<wc && mil1553_rx_data_available()){
                    uint16_t w;bool p;decode(mil1553_rx_get_data(),&w,&p);
                    if(!count)first_data_us=time_us_64();
                    received[count]=w;parity[count]=p;rx_data++;
                    if(!p){data_parity_err++;data_valid=false;}
                    if(w!=pattern[count]){data_mismatch++;data_valid=false;}
                    if(p && w==pattern[count])correct_data++;
                    count++;
                }
            }
            if(status_seen && (tr==BUS_1553_TR_BC_TO_RT || count==wc))break;
            tight_loop_contents();
        }
        // Diagnostico RX DATA solo para RT -> BC: en BC -> RT SM1 no se lee.
        bool stalled=false;uint32_t pc=0;
        if(tr==BUS_1553_TR_RT_TO_BC){
            stalled=(pio1->fdebug&RXSTALL_MASK)!=0;
            pc=pio_sm_get_pc(pio1,1u);
            if(stalled)stall_count++;
            if(fifo_max>global_fifo)global_fifo=fifo_max;
            if(count!=wc)data_timeout++;
        }
        if(!status_seen)status_timeout[tr]++;
        bool valid=status_valid && (tr==BUS_1553_TR_BC_TO_RT || (echo_valid && data_valid && count==wc));
        if(valid){ok[tr]++;coverage_ok[tr][wc-1u]++;}
        printf("\nTX %lu | %s | CMD=0x%04X | WC=%u | %s\n",(unsigned long)tx,direction(tr),(unsigned)command,wc,valid?"OK":"BAD");
        if(status_seen)printf("STATUS=0x%04X | valido=%s\n",(unsigned)status,status_valid?"SI":"NO");
        else printf("STATUS NO RECIBIDO\n");
        if(tr==BUS_1553_TR_RT_TO_BC){
            printf("CMD echo=%s | DATA=%lu/%u | FIFO max=%lu/8 | RXSTALL=%s | PC=%lu\n",echo_valid?"OK":"ERROR",(unsigned long)count,wc,(unsigned long)fifo_max,stalled?"SI":"NO",(unsigned long)pc);
            if(status_seen && count && first_data_us>=status_us)printf("STATUS a RX1: %llu us\n",(unsigned long long)(first_data_us-status_us));
            if(tx<=4 || !valid){
                for(unsigned i=0;i<count;i++)printf("DATA%02u RX=0x%04X ESP=0x%04X PAR=%s\n",i+1,(unsigned)received[i],(unsigned)pattern[i],parity[i]?"OK":"ERROR");
            }
        }
        sleep_ms(INTER_TRANSACTION_MS);
    }
    printf("\nBC - RESULTADO BIDIRECCIONAL WC 1..32\n");
    for(unsigned tr=0;tr<2;tr++){
        printf("\n%s\nIniciadas: %lu\nOK: %lu\nBAD: %lu\nSTATUS recibidos: %lu\nSTATUS correctos: %lu\nSTATUS paridad error: %lu\nTimeout STATUS: %lu\n",direction(tr),(unsigned long)started[tr],(unsigned long)ok[tr],(unsigned long)(started[tr]-ok[tr]),(unsigned long)status_rx[tr],(unsigned long)status_ok[tr],(unsigned long)status_parity_err[tr],(unsigned long)status_timeout[tr]);
    }
    printf("\nCMD enviados: %lu\nCMD echo OK (RT -> BC): %lu\nDATA enviados: %lu\nDATA esperados RX: %lu\nDATA recibidos: %lu\nDATA correctos: %lu\nDATA mismatch: %lu\nDATA error paridad: %lu\nTimeout DATA: %lu\nFIFO max RX DATA: %lu/8\nTX con RXSTALL RX DATA: %lu\n",(unsigned long)(started[0]+started[1]),(unsigned long)cmd_echo_ok,(unsigned long)tx_data,(unsigned long)expected_rx,(unsigned long)rx_data,(unsigned long)correct_data,(unsigned long)data_mismatch,(unsigned long)data_parity_err,(unsigned long)data_timeout,(unsigned long)global_fifo,(unsigned long)stall_count);
    printf("\n--- COBERTURA WC (OK / iniciadas) ---\nWC | BC->RT | RT->BC\n");
    for(unsigned i=0;i<MAX_WC;i++)printf("%2u | %lu/%lu | %lu/%lu\n",i+1,(unsigned long)coverage_ok[0][i],(unsigned long)coverage[0][i],(unsigned long)coverage_ok[1][i],(unsigned long)coverage[1][i]);
    printf("Transacciones OK: %lu/%u\nFIN DEL INFORME BC\n",(unsigned long)(ok[0]+ok[1]),TEST_TRANSACTIONS);
    while(true)tight_loop_contents();
}
