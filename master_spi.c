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

#define TEST_TRANSACTIONS 1920u
#define MAX_WC 32u
#define INTER_TRANSACTION_MS 100u
#define RESPONSE_TIMEOUT_US 1500ull
#define RXSTALL_MASK (1u << (PIO_FDEBUG_RXSTALL_LSB + 1u))
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
static void prepare_test_patterns(void)
{
    for (unsigned tr=0; tr<2u; tr++)
        for (unsigned wc=1; wc<=MAX_WC; wc++)
            for (unsigned i=0; i<wc; i++)
                test_patterns[tr][wc-1u][i] = position_mask(tr,wc,i);
}

static void decode(uint32_t e,uint16_t *w,bool *p){
    uint32_t a=e&MIL1553_RX_PAYLOAD_MASK;
    *w=(uint16_t)((a>>MIL1553_RX_WORD_SHIFT)&MIL1553_RX_WORD_MASK);
    *p=(a&MIL1553_RX_PARITY_MASK)==bus_compute_odd_parity(*w);
}
static const char *direction(unsigned tr){return tr?"RT -> BC":"BC -> RT";}
int main(void){
    set_sys_clock_khz(120000,true);
    stdio_init_all(); prepare_test_patterns(); mil1553_rx_init(); mil1553_tx_init(); sleep_ms(5000);
    const uint16_t status_expected=BUS_1553_STATUS_MAKE(20u,false);
    uint32_t started[2]={0},ok[2]={0},status_rx[2]={0},status_ok[2]={0},status_timeout[2]={0},status_parity_err[2]={0};
    uint32_t expected_rx=0, coverage[2][MAX_WC]={0}, coverage_ok[2][MAX_WC]={0};
    uint32_t cmd_echo_ok=0,tx_data=0,rx_data=0,correct_data=0,data_mismatch=0,data_parity_err=0,data_timeout=0,stall_count=0,global_fifo=0;
    printf("\nBC - TEST BIDIRECCIONAL SA 1..30 / WC 1..32 / DATA SEMILLA VARIABLE\nTransacciones: %u\n",TEST_TRANSACTIONS);
    for(uint32_t tx=1;tx<=TEST_TRANSACTIONS;tx++){
        unsigned tr=(tx-1u)&1u;
        unsigned wc = 1u + (((tx - 1u) / 2u) % MAX_WC);
        unsigned sa = 1u + (((tx - 1u) / (2u * MAX_WC)) % 30u);
        uint16_t command=BUS_1553_CMD_MAKE(20u,tr,sa,wc);
        const uint16_t *mask=test_patterns[tr][wc-1u];
        uint16_t pattern[MAX_WC],seed=0u;
        if(tr==BUS_1553_TR_BC_TO_RT){
            seed=make_seed(tr,wc,coverage[tr][wc-1u]);
            for(unsigned i=0;i<wc;i++)pattern[i]=seed^mask[i];
        }
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
                    if(count==0u)seed=w;
                    pattern[count]=seed^mask[count];
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
        sa_record(tr,sa,wc,valid);
        if(tr==BUS_1553_TR_BC_TO_RT)signature_add(tr,command,pattern,wc);
        else signature_add(tr,command,received,count);
        printf("\nTX %lu | %s | CMD=0x%04X | WC=%u | %s\n",(unsigned long)tx,direction(tr),(unsigned)command,wc,valid?"OK":"BAD");
        printf("SA=%u\n",sa);
        if(tr==BUS_1553_TR_BC_TO_RT || count)printf("SEMILLA=0x%04X (%s)\n",(unsigned)seed,tr?"RX":"TX");
        if(status_seen)printf("STATUS=0x%04X | valido=%s\n",(unsigned)status,status_valid?"SI":"NO");
        else printf("STATUS NO RECIBIDO\n");
        if(tr==BUS_1553_TR_RT_TO_BC){
            printf("CMD echo=%s | DATA=%lu/%u | FIFO max=%lu/8 | RXSTALL=%s | PC=%lu\n",echo_valid?"OK":"ERROR",(unsigned long)count,wc,(unsigned long)fifo_max,stalled?"SI":"NO",(unsigned long)pc);
            if(status_seen && count && first_data_us>=status_us)printf("STATUS a RX1: %llu us\n",(unsigned long long)(first_data_us-status_us));
            if(tx<=128 || !valid){
                for(unsigned i=0;i<count;i++)printf("DATA%02u RX=0x%04X ESP=0x%04X PAR=%s\n",i+1,(unsigned)received[i],(unsigned)pattern[i],parity[i]?"OK":"ERROR");
            }
        }
        sleep_ms(INTER_TRANSACTION_MS);
    }
    printf("\nBC - RESULTADO BIDIRECCIONAL SA 1..30 / WC 1..32 / DATA SEMILLA VARIABLE\n");
    for(unsigned tr=0;tr<2;tr++){
        printf("\n%s\nIniciadas: %lu\nOK: %lu\nBAD: %lu\nSTATUS recibidos: %lu\nSTATUS correctos: %lu\nSTATUS paridad error: %lu\nTimeout STATUS: %lu\n",direction(tr),(unsigned long)started[tr],(unsigned long)ok[tr],(unsigned long)(started[tr]-ok[tr]),(unsigned long)status_rx[tr],(unsigned long)status_ok[tr],(unsigned long)status_parity_err[tr],(unsigned long)status_timeout[tr]);
    }
    printf("\nCMD enviados: %lu\nCMD echo OK (RT -> BC): %lu\nDATA enviados: %lu\nDATA esperados RX: %lu\nDATA recibidos: %lu\nDATA compatibles (semilla: solo paridad): %lu\nDATA mismatch: %lu\nDATA error paridad: %lu\nTimeout DATA: %lu\nFIFO max RX DATA: %lu/8\nTX con RXSTALL RX DATA: %lu\n",(unsigned long)(started[0]+started[1]),(unsigned long)cmd_echo_ok,(unsigned long)tx_data,(unsigned long)expected_rx,(unsigned long)rx_data,(unsigned long)correct_data,(unsigned long)data_mismatch,(unsigned long)data_parity_err,(unsigned long)data_timeout,(unsigned long)global_fifo,(unsigned long)stall_count);
    printf("\n--- COBERTURA WC (OK / iniciadas) ---\nWC | BC->RT | RT->BC\n");
    for(unsigned i=0;i<MAX_WC;i++)printf("%2u | %lu/%lu | %lu/%lu\n",i+1,(unsigned long)coverage_ok[0][i],(unsigned long)coverage[0][i],(unsigned long)coverage_ok[1][i],(unsigned long)coverage[1][i]);
    sa_print();
    signature_print();
    printf("Transacciones OK: %lu/%u\nFIN DEL INFORME BC\n",(unsigned long)(ok[0]+ok[1]),TEST_TRANSACTIONS);
    while(true)tight_loop_contents();
}
