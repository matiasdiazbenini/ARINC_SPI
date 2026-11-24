#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/regs/pio.h"
#include "bus/bus.h"
#include "bus/mil1553_rx.h"
#include "bus/mil1553_tx.h"
#define TEST_TRANSACTIONS 1024u
#define MAX_WC 32u
#define RESPONSE_DELAY_US 8u
#define RX_TIMEOUT_US 1000ull
#define TEST_END_SILENCE_US 1500000ull
#define DIAG_CAPACITY 128u
#define RXSTALL_MASK (1u << (PIO_FDEBUG_RXSTALL_LSB+1u))
static const uint16_t pattern[MAX_WC]={
    0x4444,0x2222,0x3333,0x1111,0xAAAA,0x5555,0xFFFF,0x0000,
    0x1234,0x5678,0xABCD,0xDCBA,0x0F0F,0xF0F0,0x8001,0x7FFE,
    0x0001,0x0002,0x0004,0x0008,0x0010,0x0020,0x0040,0x0080,
    0x0100,0x0200,0x0400,0x0800,0x1000,0x2000,0x4000,0x8000
};
typedef struct {uint32_t tx,wc,count,first_us,max_poll,max_read,max_fifo,pc,fifo_end;bool valid,timeout,stall;uint16_t words[MAX_WC];bool parity[MAX_WC];} diag_t;
static diag_t active,saved[DIAG_CAPACITY];
static uint32_t saved_count,dropped,stall_count,max_fifo,max_poll,max_read;
static uint64_t cmd_us,last_poll,last_read;
static uint32_t max32(uint32_t a,uint32_t b){return a>b?a:b;}
static void decode(uint32_t e,uint16_t *w,bool *p){uint32_t a=e&MIL1553_RX_PAYLOAD_MASK;*w=(uint16_t)((a>>MIL1553_RX_WORD_SHIFT)&MIL1553_RX_WORD_MASK);*p=(a&MIL1553_RX_PARITY_MASK)==bus_compute_odd_parity(*w);}
static void diag_start(uint32_t tx,unsigned wc,uint64_t now){active.tx=tx;active.wc=wc;active.count=active.first_us=active.max_poll=active.max_read=active.max_fifo=0;cmd_us=last_poll=now;last_read=0;}
static void diag_poll(uint64_t now){active.max_poll=max32(active.max_poll,(uint32_t)(now-last_poll));last_poll=now;active.max_fifo=max32(active.max_fifo,pio_sm_get_rx_fifo_level(pio1,1u));}
static void diag_word(uint16_t w,bool p,uint64_t now){unsigned i=active.count;if(!i)active.first_us=(uint32_t)(now-cmd_us);else active.max_read=max32(active.max_read,(uint32_t)(now-last_read));last_read=now;if(i<MAX_WC){active.words[i]=w;active.parity[i]=p;}active.count++;}
static void diag_end(bool valid,bool timeout){
    active.valid=valid;active.timeout=timeout;active.stall=(pio1->fdebug&RXSTALL_MASK)!=0;
    active.pc=pio_sm_get_pc(pio1,1u);active.fifo_end=pio_sm_get_rx_fifo_level(pio1,1u);
    if(active.stall)stall_count++;
    max_fifo=max32(max_fifo,active.max_fifo);max_poll=max32(max_poll,active.max_poll);max_read=max32(max_read,active.max_read);
    if(active.tx<=6 || !valid){if(saved_count<DIAG_CAPACITY)saved[saved_count++]=active;else dropped++;}
}
static void print_diag(const diag_t *d){
    printf("\nRT RX TX local %lu | %s | DATA %lu/%lu | Timeout=%s\n",(unsigned long)d->tx,d->valid?"OK":"BAD",(unsigned long)d->count,(unsigned long)d->wc,d->timeout?"SI":"NO");
    printf("RX1=%lu us | Poll max=%lu us | Lectura max=%lu us | FIFO max=%lu/8 | RXSTALL=%s | PC=%lu | FIFO cierre=%lu\n",(unsigned long)d->first_us,(unsigned long)d->max_poll,(unsigned long)d->max_read,(unsigned long)d->max_fifo,d->stall?"SI":"NO",(unsigned long)d->pc,(unsigned long)d->fifo_end);
    for(unsigned i=0;i<d->count && i<MAX_WC;i++)printf("DATA%02u RX=0x%04X ESP=0x%04X PAR=%s\n",i+1,(unsigned)d->words[i],(unsigned)pattern[i],d->parity[i]?"OK":"ERROR");
}
int main(void){
    set_sys_clock_khz(120000,true);stdio_init_all();sleep_ms(1000);
    mil1553_rx_init();mil1553_tx_init();mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;
    const uint16_t status_ok=BUS_1553_STATUS_MAKE(20u,false),status_error=BUS_1553_STATUS_MAKE(20u,true);
    uint32_t started[2]={0},completed[2]={0},ok[2]={0},status_sent[2]={0};
    uint32_t cmd_received=0,cmd_parity_err=0,cmd_unexpected=0,rx_data=0,correct_data=0,data_mismatch=0,data_parity_err=0,tx_data=0,timeouts=0,sequence_errors=0,status_error_sent=0;
    uint32_t expected_rx=0,expected_tx=0,coverage[2][MAX_WC]={0},coverage_ok[2][MAX_WC]={0};
    unsigned position=0,wc=0;bool receiving=false,valid=true,test_started=false,report_done=false;
    uint64_t last_word=time_us_64(),last_activity=last_word;
    printf("\nRT - TEST BIDIRECCIONAL WC 1..32\nTransacciones: %u\nRT=20 SA=18 WC=1..32\n",TEST_TRANSACTIONS);
    while(true){
        uint64_t now=time_us_64();
        if(receiving)diag_poll(now);
        if(mil1553_rx_available()){
            uint32_t e=mil1553_rx_get(),type=e&MIL1553_RX_TYPE_MASK;uint16_t w;bool p;decode(e,&w,&p);
            test_started=true;last_activity=now;
            if(!receiving){
                if(type!=MIL1553_RX_TYPE_CMD_STATUS)continue;
                cmd_received++;
                if(!p || BUS_1553_CMD_RT(w)!=20u || BUS_1553_CMD_SUB(w)!=18u){
                    if(!p)cmd_parity_err++;else cmd_unexpected++;
                    mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;continue;
                }
                unsigned tr=BUS_1553_CMD_TR(w);
                wc=BUS_1553_CMD_WC(w);
                if(wc==0u)wc=MAX_WC;
                coverage[tr][wc-1u]++;
                if(tr==BUS_1553_TR_RT_TO_BC){
                    expected_tx+=wc;
                    started[1]++;
                    // Secuencia RT -> BC validada: STATUS y DATA continuos.
                    busy_wait_us_32(RESPONSE_DELAY_US);
                    mil1553_tx_send_word(BUS_1553_SYNC_CMD_STATUS,status_ok);
                    for(unsigned i=0;i<wc;i++)mil1553_tx_send_word(BUS_1553_SYNC_DATA,pattern[i]);
                    mil1553_tx_wait_idle();
                    status_sent[1]++;tx_data+=wc;completed[1]++;ok[1]++;coverage_ok[1][wc-1u]++;
                    mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;
                    last_word=last_activity=time_us_64();
                }else{
                    started[0]++;expected_rx+=wc;position=0;valid=true;receiving=true;last_word=now;
                    // No rearmar: SM1 ya recibe a partir de IRQ0 del COMMAND.
                    diag_start(started[0]+started[1],wc,now);
                }
                continue;
            }
            if(type!=MIL1553_RX_TYPE_DATA){sequence_errors++;valid=false;continue;}
            rx_data++;
            if(!p){data_parity_err++;valid=false;}
            if(w!=pattern[position]){data_mismatch++;valid=false;}
            if(p && w==pattern[position])correct_data++;
            diag_word(w,p,now);position++;last_word=now;
            if(position==wc){
                busy_wait_us_32(RESPONSE_DELAY_US);
                mil1553_tx_send_word(BUS_1553_SYNC_CMD_STATUS,valid?status_ok:status_error);
                mil1553_tx_wait_idle();status_sent[0]++;completed[0]++;
                if(valid){ok[0]++;coverage_ok[0][wc-1u]++;}else status_error_sent++;
                diag_end(valid,false);
                mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;
                receiving=false;position=0;last_word=last_activity=time_us_64();
            }
        }
        now=time_us_64();
        if(receiving && now-last_word>=RX_TIMEOUT_US){
            timeouts++;diag_end(false,true);mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;
            receiving=false;position=0;last_word=last_activity=time_us_64();
        }
        if(test_started && !report_done && !receiving && now>=last_activity && now-last_activity>=TEST_END_SILENCE_US){
            report_done=true;
            printf("\nRT - RESULTADO BIDIRECCIONAL WC 1..32\n");
            for(unsigned tr=0;tr<2;tr++)printf("\n%s\nIniciadas: %lu\nCompletas: %lu\nOK: %lu\nBAD: %lu\nSTATUS enviados: %lu\n",tr?"RT -> BC":"BC -> RT",(unsigned long)started[tr],(unsigned long)completed[tr],(unsigned long)ok[tr],(unsigned long)(started[tr]-ok[tr]),(unsigned long)status_sent[tr]);
            printf("\nCMD recibidos: %lu\nCMD correctos: %lu\nCMD paridad error: %lu\nCMD inesperados: %lu\nDATA esperados RX: %lu\nDATA recibidos: %lu\nDATA correctos: %lu\nDATA mismatch: %lu\nDATA paridad error: %lu\nDATA enviados: %lu\nTimeout DATA: %lu\nErrores secuencia: %lu\nSTATUS MSG_ERROR: %lu\n",(unsigned long)cmd_received,(unsigned long)(started[0]+started[1]),(unsigned long)cmd_parity_err,(unsigned long)cmd_unexpected,(unsigned long)expected_rx,(unsigned long)rx_data,(unsigned long)correct_data,(unsigned long)data_mismatch,(unsigned long)data_parity_err,(unsigned long)tx_data,(unsigned long)timeouts,(unsigned long)sequence_errors,(unsigned long)status_error_sent);
            printf("DATA esperados TX (COMMAND aceptados): %lu\n",(unsigned long)expected_tx);
            printf("\n--- COBERTURA WC (OK / iniciadas) ---\nWC | BC->RT | RT->BC\n");
            for(unsigned i=0;i<MAX_WC;i++)printf("%2u | %lu/%lu | %lu/%lu\n",i+1,(unsigned long)coverage_ok[0][i],(unsigned long)coverage[0][i],(unsigned long)coverage_ok[1][i],(unsigned long)coverage[1][i]);
            printf("FIFO max: %lu/8 | RXSTALL TX: %lu | Max polling: %lu us | Max lectura: %lu us | Omitidos: %lu\n",(unsigned long)max_fifo,(unsigned long)stall_count,(unsigned long)max_poll,(unsigned long)max_read,(unsigned long)dropped);
            for(unsigned i=0;i<saved_count;i++)print_diag(&saved[i]);
            printf("Transacciones OK locales: %lu/%u\nFIN DEL INFORME RT\n",(unsigned long)(ok[0]+ok[1]),TEST_TRANSACTIONS);
        }
        tight_loop_contents();
    }
}
