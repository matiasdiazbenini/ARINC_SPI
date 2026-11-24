"""Actualiza BM a DATA variable, conservando rearme anticipado SM0."""
import argparse, hashlib, shutil
from pathlib import Path
EXPECTED="32fd193cbef08352d604100178d7b64339f33cae"
OLD='/* Patron de ensayo reproducible por direccion, WC y posicion (base 0).\n * Indices 0..1055 sin colisiones: multiplicador impar y xorshift de 16 bits.\n * No usa el contador de transacciones ni el reloj.\n */\nstatic inline uint16_t test_data_word(unsigned tr, unsigned wc, unsigned pos)\n{\n    uint32_t index = tr * 528u + (wc * (wc - 1u)) / 2u + pos;\n    uint16_t x = (uint16_t)(index * 40503u + 0xBEEFu);\n    x ^= (uint16_t)(x >> 7);\n    x ^= (uint16_t)(x << 9);\n    x ^= (uint16_t)(x >> 8);\n    return x;\n}'
NEW='/* Permutacion de referencia para construir mascaras de posicion. */\nstatic inline uint16_t test_data_word(unsigned tr, unsigned wc, unsigned pos)\n{\n    uint32_t index = tr * 528u + (wc * (wc - 1u)) / 2u + pos;\n    uint16_t x = (uint16_t)(index * 40503u + 0xBEEFu);\n    x ^= (uint16_t)(x >> 7);\n    x ^= (uint16_t)(x << 9);\n    x ^= (uint16_t)(x >> 8);\n    return x;\n}\n\nstatic inline uint16_t position_mask(unsigned tr, unsigned wc, unsigned pos)\n{\n    return test_data_word(tr,wc,pos) ^ test_data_word(tr,wc,0u);\n}\n\n/* occurrence = repeticiones previas de este COMMAND en el transmisor.\n * Semillas distintas para las 64 combinaciones durante 1024 ciclos.\n * No se exige este contador en ningun receptor. */\nstatic inline uint16_t make_seed(unsigned tr, unsigned wc, uint32_t occurrence)\n{\n    uint32_t index = 64u * occurrence + 2u * (wc - 1u) + tr;\n    uint16_t x = (uint16_t)(index * 40503u + 0xBEEFu);\n    x ^= (uint16_t)(x >> 7);\n    x ^= (uint16_t)(x << 9);\n    x ^= (uint16_t)(x >> 8);\n    return x;\n}\n\n/* FNV-1a 64: COMMAND, cantidad DATA observada, DATA en orden.\n * Cada uint16 se incorpora LSB primero. Solo fuera del camino critico. */\nstatic uint64_t signatures[2] = {UINT64_C(14695981039346656037), UINT64_C(14695981039346656037)};\nstatic uint32_t signature_messages[2], signature_words[2];\nstatic inline uint64_t hash_word(uint64_t h, uint16_t w)\n{\n    h = (h ^ (uint8_t)w) * UINT64_C(1099511628211);\n    return (h ^ (uint8_t)(w >> 8)) * UINT64_C(1099511628211);\n}\nstatic void signature_add(unsigned tr, uint16_t command, const uint16_t *data, unsigned count)\n{\n    uint64_t h = hash_word(signatures[tr],command);\n    h = hash_word(h,(uint16_t)count);\n    for (unsigned i=0; i<count; i++) h=hash_word(h,data[i]);\n    signatures[tr]=h;\n    signature_messages[tr]++;\n    signature_words[tr]+=count;\n}\nstatic void signature_print(void)\n{\n    for (unsigned tr=0; tr<2u; tr++)\n        printf("FIRMA %s | MSG=%lu | DATA=%lu | FNV64=%016llX\\n",\n            tr?"RT->BC":"BC->RT",(unsigned long)signature_messages[tr],\n            (unsigned long)signature_words[tr],(unsigned long long)signatures[tr]);\n}'
def blob(b):return hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()
def once(s,a,b):
    if s.count(a)!=1:raise ValueError(f"Ancla no coincide: {a!r}")
    return s.replace(a,b,1)
def transform(s):
    s=once(s,OLD,NEW)
    s=once(s,"#define TEST_TRANSACTIONS           1024u","#define TEST_TRANSACTIONS           128u")
    s=once(s,"#define PRINT_FIRST_TRANSACTIONS    64u","#define PRINT_FIRST_TRANSACTIONS    128u")
    s=once(s,"#define BM_DIAG_MAX_SAVED           128u","#define BM_DIAG_MAX_SAVED           192u")
    s=once(s,"    uint32_t seen = 0u;", "    if (!t->data_count) { printf(\"Sin DATA: semilla no disponible\\n\"); return; }\n    printf(\"Patron condicionado a DATA1 (semilla RX=0x%04X)\\n\",(unsigned)t->data[0]);\n    uint32_t seen = 0u;")
    s=once(s,"test_data_word(t->tr,t->word_count,(unsigned)j)","(t->data[0] ^ position_mask(t->tr,t->word_count,(unsigned)j))")
    if s.count("test_data_word(t->tr,t->word_count,i)")!=2:raise ValueError("Comparaciones DATA no coinciden")
    s=s.replace("test_data_word(t->tr,t->word_count,i)","(t->data[0] ^ position_mask(t->tr,t->word_count,i))")
    s=once(s,"        test_expected_words += wc;","        test_expected_words += wc;\n        signature_add(tr,t->command,t->data,t->data_count);")
    s=once(s,"                printf(\"DATA mismatch de patron: %lu\\n\\n\", (unsigned long)test_pattern_errors);","                printf(\"DATA mismatch de patron: %lu\\n\\n\", (unsigned long)test_pattern_errors);\n                signature_print();")
    s=s.replace("DATA PRNG","DATA SEMILLA VARIABLE").replace("PRNG por direccion, WC y posicion","Semilla DATA1 + mascara por direccion, WC y posicion").replace("DATA correctos de patron y paridad","DATA compatibles (semilla: solo paridad)")
    return s

def main():
    p=argparse.ArgumentParser();p.add_argument("root",nargs="?",default=".");p.add_argument("--check",action="store_true");a=p.parse_args()
    f=Path(a.root).resolve()/"main.c"
    try:
        raw=f.read_bytes();normalized=raw.replace(b"\r\n",b"\n")
        if EXPECTED not in (blob(raw),blob(normalized)):raise ValueError("main.c no coincide con FLASK-MIL validado; no se modifico nada")
        s=transform(normalized.decode("utf-8"))
        if a.check:print("OK: version y anclas verificadas. Sin cambios.");return
        backup=f.with_name("main_rearme_anticipado_validado.c.bak")
        if backup.exists():raise ValueError("Backup existente: "+str(backup))
        shutil.copy2(f,backup)
        out=s.encode("utf-8")
        if b"\r\n" in raw:out=out.replace(b"\n",b"\r\n")
        tmp=f.with_name("main_variable.c.tmp");tmp.write_bytes(out);tmp.replace(f)
        print("BM actualizado; backup:",backup)
    except (ValueError,OSError) as e:raise SystemExit("ABORTADO: "+str(e))
if __name__=="__main__":main()
