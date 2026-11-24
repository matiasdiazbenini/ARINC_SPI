"""BM WC 1..32: actualiza solo main.c desde FLASK-MIL validado WC32.
Uso desde la raiz BM: python preparar_bm_wc.py --check
                       python preparar_bm_wc.py
Alternativa: python preparar_bm_wc.py C:\\ruta\\BM
"""
import argparse
import hashlib
from pathlib import Path
import shutil

EXPECTED = 'd9314c7684bf400a40f5bd94ef9271fb08e4191c'

def blob_sha(data):
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def once(s, old, new):
    n=s.count(old)
    if n != 1:
        raise ValueError(f'Esperaba 1 coincidencia, encontre {n}: {old[:80]!r}')
    return s.replace(old,new,1)


def transform(s):
    s=once(s,'#define TEST_TRANSACTIONS           10000u','#define TEST_TRANSACTIONS           64u')
    s=s.replace('BM - TEST DIAGNOSTICO DATA / WC = 32','BM - TEST DIAGNOSTICO DATA / WC 1..32')
    s=s.replace('BM - RESULTADO DIAGNOSTICO DATA / WC = 32','BM - RESULTADO DIAGNOSTICO DATA / WC 1..32')
    s=once(s,'"WC                   : 32\\n"','"WC                   : 1..32\\n"')
    s=once(s,'"DATA32               : 0x8000\\n"','"Patron DATA          : prefijo de longitud WC\\n"')
    s=once(s,'"Rearme SM0 DATA32    : HABILITADO\\n"','"Rearme SM0 ultimo DATA: HABILITADO\\n"')
    # Solo renombrar la funcion: se sigue ejecutando al ultimo DATA real.
    if s.count('monitor_p_after_data32')!=2:
        raise ValueError('No coincide el monitor de STATUS.')
    s=s.replace('monitor_p_after_data32','monitor_p_after_last_data')
    s=s.replace('MONITOREAR P DESPUES DE DATA32','MONITOREAR P DESPUES DEL ULTIMO DATA')
    s=once(s,'static void print_missing_data(\n', '''/* Contadores de la prueba, independientes de la adquisicion PIO. */
static uint32_t test_wc_started[2][32];
static uint32_t test_wc_ok[2][32];
static uint32_t test_expected_words, test_correct_words, test_pattern_errors;

static bool is_test_command(const mil1553_transaction_t *t)
{
    return t->rt_address == 20u && t->subaddress == 18u &&
           t->tr <= 1u && t->word_count >= 1u && t->word_count <= 32u;
}

static void print_missing_data(
''')
    s=once(s,'if (t->command != 0xA240u ||\n        t->word_count != 32u)','if (!is_test_command(t))')
    s=once(s,'for (int j = 0; j < 32; j++)','for (int j = 0; j < (int)t->word_count; j++)')
    s=once(s,'if (seen == UINT32_MAX)','if (seen == (UINT32_MAX >> (32u - t->word_count)))')
    s=once(s,'for (uint8_t i = 0u; i < 32u; i++)','for (uint8_t i = 0u; i < t->word_count; i++)')
    s=once(s,'    if (!complete)\n    {\n        t->valid = false;', '''    /* Comparar el patron al CIERRE, no durante la recepcion DATA.
     * Solo para el ensayo RT20/SA18; otros mensajes mantienen
     * la semantica de validez de protocolo existente.
     */
    if (is_test_command(t))
    {
        unsigned tr = t->tr;
        unsigned wc = t->word_count;
        test_wc_started[tr][wc - 1u]++;
        test_expected_words += wc;
        for (unsigned i = 0; i < t->data_count && i < wc; i++)
        {
            if (t->data[i] != diag_expected_data[i])
            {
                test_pattern_errors++;
                t->valid = false;
            }
            else if (t->data_parity_ok[i])
                test_correct_words++;
        }
        if (t->status_seen && t->status != BUS_1553_STATUS_MAKE(20u, false))
            t->valid = false;
        if (complete && t->valid && t->data_count == wc && t->status_seen)
            test_wc_ok[tr][wc - 1u]++;
    }

    if (!complete)
    {
        t->valid = false;''')
    s=once(s,'                printf(\n                    "--- DETALLE POR TRANSACCION ---\\n"\n                );', '''                printf("\\n--- COBERTURA WC (OK / iniciadas) ---\\nWC | BC->RT | RT->BC\\n");
                for (unsigned i=0; i<32u; i++)
                    printf("%2u | %lu/%lu | %lu/%lu\\n", i+1,
                        (unsigned long)test_wc_ok[0][i], (unsigned long)test_wc_started[0][i],
                        (unsigned long)test_wc_ok[1][i], (unsigned long)test_wc_started[1][i]);
                printf("DATA esperados (COMMAND capturados): %lu\\n", (unsigned long)test_expected_words);
                printf("DATA correctos de patron y paridad: %lu\\n", (unsigned long)test_correct_words);
                printf("DATA mismatch de patron: %lu\\n\\n", (unsigned long)test_pattern_errors);

                printf(
                    "--- DETALLE POR TRANSACCION ---\\n"
                );''')
    return s


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root',nargs='?',default='.',help='Raiz del proyecto BM')
    ap.add_argument('--check',action='store_true',help='Verificar y simular sin modificar')
    args=ap.parse_args(); root=Path(args.root).resolve(); p=root/'main.c'
    try:
        raw=p.read_bytes(); normalized=raw.replace(b'\r\n',b'\n')
        if EXPECTED not in (blob_sha(raw),blob_sha(normalized)):
            raise ValueError('main.c no coincide con FLASK-MIL WC32 validado (10000).')
        result=transform(normalized.decode('utf-8'))
        if args.check:
            print('OK: version y cambios verificados. No se escribio nada.');return
        backup=root/'main_WC32_validado.c.bak'
        if backup.exists():raise ValueError(f'Ya existe el backup: {backup}')
        shutil.copy2(p,backup)
        data=result.encode('utf-8')
        if b'\r\n' in raw:data=data.replace(b'\n',b'\r\n')
        tmp=root/'main_WC_dinamico.c.tmp';tmp.write_bytes(data);tmp.replace(p)
        print('BM WC dinamico preparado. Backup:',backup)
        print('No se modificaron drivers, PIO ni bus.h.')
    except (OSError,ValueError) as e:
        raise SystemExit(f'ABORTADO: {e}')

if __name__=='__main__':main()
