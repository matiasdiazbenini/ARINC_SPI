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
#define TEST_TRANSACTIONS 1920u
#define MAX_WC 32u
#define RESPONSE_DELAY_US 8u
#define RX_TIMEOUT_US 1000ull
#define TEST_END_SILENCE_US 1500000ull
#define DIAG_CAPACITY 128u
#define RXSTALL_MASK (1u << (PIO_FDEBUG_RXSTALL_LSB+1u))
/* Permutacion de referencia para construir mascaras de posicion. */
static inline uint16_t test_data_word(unsigned tr, unsigned wc, unsigned pos)
{
    uint32_t index = tr * 528u + (wc * (wc - 1u)) / 2u + pos;
    uint16_t x = (uint16_t)(index * 40503u + 0xBEEFu);
    x ^= (uint16_t)(x >> 7);
    x ^= (uint16_t)(x << 9);
    x ^= (uint16_t)(x >> 8);
    return x;
}

static inline uint16_t position_mask(unsigned tr, unsigned wc, unsigned pos)
{
    return test_data_word(tr,wc,pos) ^ test_data_word(tr,wc,0u);
}

/* occurrence = repeticiones previas de este par TR/WC (todas las SA) en el transmisor.
 * Semillas distintas para las 64 combinaciones durante 1024 ciclos.
 * No se exige este contador en ningun receptor. */
static inline uint16_t make_seed(unsigned tr, unsigned wc, uint32_t occurrence)
{
    uint32_t index = 64u * occurrence + 2u * (wc - 1u) + tr;
    uint16_t x = (uint16_t)(index * 40503u + 0xBEEFu);
    x ^= (uint16_t)(x >> 7);
    x ^= (uint16_t)(x << 9);
    x ^= (uint16_t)(x >> 8);
    return x;
}

/* FNV-1a 64: COMMAND, cantidad DATA observada, DATA en orden.
 * Cada uint16 se incorpora LSB primero. Solo fuera del camino critico. */
static uint64_t signatures[2] = {UINT64_C(14695981039346656037), UINT64_C(14695981039346656037)};
static uint32_t signature_messages[2], signature_words[2];
static inline uint64_t hash_word(uint64_t h, uint16_t w)
{
    h = (h ^ (uint8_t)w) * UINT64_C(1099511628211);
    return (h ^ (uint8_t)(w >> 8)) * UINT64_C(1099511628211);
}
static void signature_add(unsigned tr, uint16_t command, const uint16_t *data, unsigned count)
{
    uint64_t h = hash_word(signatures[tr],command);
    h = hash_word(h,(uint16_t)count);
    for (unsigned i=0; i<count; i++) h=hash_word(h,data[i]);
    signatures[tr]=h;
    signature_messages[tr]++;
    signature_words[tr]+=count;
}
static void signature_print(void)
{
    for (unsigned tr=0; tr<2u; tr++)
        printf("FIRMA %s | MSG=%lu | DATA=%lu | FNV64=%016llX\n",
            tr?"RT->BC":"BC->RT",(unsigned long)signature_messages[tr],
            (unsigned long)signature_words[tr],(unsigned long long)signatures[tr]);
}


/* Cobertura exacta por SA, WC y direccion. Memoria fija: 15360 bytes.
 * Actualizar solamente al finalizar cada mensaje (incluidos timeouts). */
static uint32_t sa_started[2][30][32];
static uint32_t sa_ok[2][30][32];
static void sa_record(unsigned tr, unsigned sa, unsigned wc, bool valid)
{
    if (tr > 1u || sa < 1u || sa > 30u || wc < 1u || wc > 32u) return;
    sa_started[tr][sa-1u][wc-1u]++;
    if (valid) sa_ok[tr][sa-1u][wc-1u]++;
}
static void sa_print(void)
{
    unsigned covered=0u, all_ok=0u;
    printf("\n--- COBERTURA SA (OK / iniciadas) ---\nSA | BC->RT | RT->BC\n");
    for (unsigned a=0; a<30u; a++) {
        uint32_t n[2]={0}, k[2]={0};
        for (unsigned t=0; t<2u; t++) for (unsigned w=0; w<32u; w++) {
            uint32_t started=sa_started[t][a][w], ok=sa_ok[t][a][w];
            n[t]+=started; k[t]+=ok;
            if (started) covered++;
            if (started && started==ok) all_ok++;
        }
        printf("%2u | %lu/%lu | %lu/%lu\n", a+1u,
            (unsigned long)k[0],(unsigned long)n[0],(unsigned long)k[1],(unsigned long)n[1]);
    }
    printf("Combinaciones SA/WC/TR observadas: %u/1920\n",covered);
    printf("Combinaciones SA/WC/TR sin fallos: %u/1920\n",all_ok);
    printf("\n--- MATRIZ SA/WC (OK / iniciadas) ---\nSA WC | BC->RT | RT->BC\n");
    for (unsigned a=0; a<30u; a++) for (unsigned w=0; w<32u; w++)
        printf("%2u %2u | %lu/%lu | %lu/%lu\n",a+1u,w+1u,
            (unsigned long)sa_ok[0][a][w],(unsigned long)sa_started[0][a][w],
            (unsigned long)sa_ok[1][a][w],(unsigned long)sa_started[1][a][w]);
}

/* Generar antes del ensayo para mantener liviano el camino critico. */
static uint16_t test_patterns[2][MAX_WC][MAX_WC];
static uint16_t next_tx[MAX_WC][MAX_WC];
static void prepare_next_tx(unsigned wc,uint32_t occurrence){
    uint16_t seed=make_seed(1u,wc,occurrence);
    for(unsigned i=0;i<wc;i++)next_tx[wc-1u][i]=seed^test_patterns[1][wc-1u][i];
}
static void prepare_test_patterns(void)
{
    for (unsigned tr=0; tr<2u; tr++)
        for (unsigned wc=1; wc<=MAX_WC; wc++)
            for (unsigned i=0; i<wc; i++)
                test_patterns[tr][wc-1u][i] = position_mask(tr,wc,i);
    for(unsigned wc=1;wc<=MAX_WC;wc++)prepare_next_tx(wc,0u);
}

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
    if(active.tx<=128 || !valid){if(saved_count<DIAG_CAPACITY)saved[saved_count++]=active;else dropped++;}
}
static void print_diag(const diag_t *d){
    printf("\nRT RX TX local %lu | %s | DATA %lu/%lu | Timeout=%s\n",(unsigned long)d->tx,d->valid?"OK":"BAD",(unsigned long)d->count,(unsigned long)d->wc,d->timeout?"SI":"NO");
    printf("RX1=%lu us | Poll max=%lu us | Lectura max=%lu us | FIFO max=%lu/8 | RXSTALL=%s | PC=%lu | FIFO cierre=%lu\n",(unsigned long)d->first_us,(unsigned long)d->max_poll,(unsigned long)d->max_read,(unsigned long)d->max_fifo,d->stall?"SI":"NO",(unsigned long)d->pc,(unsigned long)d->fifo_end);
    for(unsigned i=0;i<d->count && i<MAX_WC;i++)printf("DATA%02u RX=0x%04X ESP=0x%04X PAR=%s\n",i+1,(unsigned)d->words[i],(unsigned)(d->words[0]^test_patterns[0][d->wc-1u][i]),d->parity[i]?"OK":"ERROR");
}
int main(void){
    set_sys_clock_khz(120000,true);stdio_init_all();prepare_test_patterns();sleep_ms(1000);
    mil1553_rx_init();mil1553_tx_init();mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;
    const uint16_t status_ok=BUS_1553_STATUS_MAKE(20u,false),status_error=BUS_1553_STATUS_MAKE(20u,true);
    uint32_t started[2]={0},completed[2]={0},ok[2]={0},status_sent[2]={0};
    uint32_t cmd_received=0,cmd_parity_err=0,cmd_unexpected=0,rx_data=0,correct_data=0,data_mismatch=0,data_parity_err=0,tx_data=0,timeouts=0,sequence_errors=0,status_error_sent=0;
    uint32_t expected_rx=0,expected_tx=0,coverage[2][MAX_WC]={0},coverage_ok[2][MAX_WC]={0};
    const uint16_t *pattern=NULL;
    uint16_t seed=0u,rx_command=0u;
    unsigned position=0,wc=0;bool receiving=false,valid=true,test_started=false,report_done=false;
    uint64_t last_word=time_us_64(),last_activity=last_word;
    printf("\nRT - TEST BIDIRECCIONAL SA 1..30 / WC 1..32 / DATA SEMILLA VARIABLE\nTransacciones: %u\nRT=20 SA=1..30 WC=1..32\n",TEST_TRANSACTIONS);
    while(true){
        uint64_t now=time_us_64();
        if(receiving)diag_poll(now);
        if(mil1553_rx_available()){
            uint32_t e=mil1553_rx_get(),type=e&MIL1553_RX_TYPE_MASK;uint16_t w;bool p;decode(e,&w,&p);
            test_started=true;last_activity=now;
            if(!receiving){
                if(type!=MIL1553_RX_TYPE_CMD_STATUS)continue;
                cmd_received++;
                unsigned sa=BUS_1553_CMD_SUB(w);
                if(!p || BUS_1553_CMD_RT(w)!=20u || sa==0u || sa==31u){
                    if(!p)cmd_parity_err++;else cmd_unexpected++;
                    mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;continue;
                }
                unsigned tr=BUS_1553_CMD_TR(w);
                wc=BUS_1553_CMD_WC(w);
                if(wc==0u)wc=MAX_WC;
                pattern=tr?next_tx[wc-1u]:test_patterns[0][wc-1u];
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
                    sa_record(1u,sa,wc,true);
                    signature_add(1u,w,pattern,wc);
                    prepare_next_tx(wc,coverage[1][wc-1u]);
                    last_word=last_activity=time_us_64();
                }else{
                    started[0]++;expected_rx+=wc;position=0;valid=true;receiving=true;last_word=now;
                    rx_command=w;
                    // No rearmar: SM1 ya recibe a partir de IRQ0 del COMMAND.
                    diag_start(started[0]+started[1],wc,now);
                }
                continue;
            }
            if(type!=MIL1553_RX_TYPE_DATA){sequence_errors++;valid=false;continue;}
            rx_data++;
            if(!p){data_parity_err++;valid=false;}
            if(position==0u)seed=w;
            uint16_t expected=seed^pattern[position];
            if(w!=expected){data_mismatch++;valid=false;}
            if(p && w==expected)correct_data++;
            diag_word(w,p,now);position++;last_word=now;
            if(position==wc){
                busy_wait_us_32(RESPONSE_DELAY_US);
                mil1553_tx_send_word(BUS_1553_SYNC_CMD_STATUS,valid?status_ok:status_error);
                mil1553_tx_wait_idle();status_sent[0]++;completed[0]++;
                if(valid){ok[0]++;coverage_ok[0][wc-1u]++;}else status_error_sent++;
                diag_end(valid,false);
                mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;
                sa_record(0u,BUS_1553_CMD_SUB(rx_command),wc,valid);
                signature_add(0u,rx_command,active.words,position);
                receiving=false;position=0;last_word=last_activity=time_us_64();
            }
        }
        now=time_us_64();
        if(receiving && now-last_word>=RX_TIMEOUT_US){
            timeouts++;diag_end(false,true);mil1553_rx_clear();pio1->fdebug=RXSTALL_MASK;
            sa_record(0u,BUS_1553_CMD_SUB(rx_command),wc,false);
            signature_add(0u,rx_command,active.words,position);
            receiving=false;position=0;last_word=last_activity=time_us_64();
        }
        if(test_started && !report_done && !receiving && now>=last_activity && now-last_activity>=TEST_END_SILENCE_US){
            report_done=true;
            printf("\nRT - RESULTADO BIDIRECCIONAL SA 1..30 / WC 1..32 / DATA SEMILLA VARIABLE\n");
            for(unsigned tr=0;tr<2;tr++)printf("\n%s\nIniciadas: %lu\nCompletas: %lu\nOK: %lu\nBAD: %lu\nSTATUS enviados: %lu\n",tr?"RT -> BC":"BC -> RT",(unsigned long)started[tr],(unsigned long)completed[tr],(unsigned long)ok[tr],(unsigned long)(started[tr]-ok[tr]),(unsigned long)status_sent[tr]);
            printf("\nCMD recibidos: %lu\nCMD correctos: %lu\nCMD paridad error: %lu\nCMD inesperados: %lu\nDATA esperados RX: %lu\nDATA recibidos: %lu\nDATA compatibles (semilla: solo paridad): %lu\nDATA mismatch: %lu\nDATA paridad error: %lu\nDATA enviados: %lu\nTimeout DATA: %lu\nErrores secuencia: %lu\nSTATUS MSG_ERROR: %lu\n",(unsigned long)cmd_received,(unsigned long)(started[0]+started[1]),(unsigned long)cmd_parity_err,(unsigned long)cmd_unexpected,(unsigned long)expected_rx,(unsigned long)rx_data,(unsigned long)correct_data,(unsigned long)data_mismatch,(unsigned long)data_parity_err,(unsigned long)tx_data,(unsigned long)timeouts,(unsigned long)sequence_errors,(unsigned long)status_error_sent);
            printf("DATA esperados TX (COMMAND aceptados): %lu\n",(unsigned long)expected_tx);
            printf("\n--- COBERTURA WC (OK / iniciadas) ---\nWC | BC->RT | RT->BC\n");
            for(unsigned i=0;i<MAX_WC;i++)printf("%2u | %lu/%lu | %lu/%lu\n",i+1,(unsigned long)coverage_ok[0][i],(unsigned long)coverage[0][i],(unsigned long)coverage_ok[1][i],(unsigned long)coverage[1][i]);
            printf("FIFO max: %lu/8 | RXSTALL TX: %lu | Max polling: %lu us | Max lectura: %lu us | Omitidos: %lu\n",(unsigned long)max_fifo,(unsigned long)stall_count,(unsigned long)max_poll,(unsigned long)max_read,(unsigned long)dropped);
            for(unsigned i=0;i<saved_count;i++)print_diag(&saved[i]);
            sa_print();
            signature_print();
            printf("Transacciones OK locales: %lu/%u\nFIN DEL INFORME RT\n",(unsigned long)(ok[0]+ok[1]),TEST_TRANSACTIONS);
        }
        tight_loop_contents();
    }
}
