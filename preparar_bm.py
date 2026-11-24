"""SA dinamica BM: aplica solo a la version GitHub validada, sin tocar RX/PIO."""
import argparse, hashlib, shutil
from pathlib import Path
EXPECTED="91ac6225771dc9fad5b3bfe4ce0564a460b9b16f"
PATCHES=[('#define TEST_TRANSACTIONS           10240u', '#define TEST_TRANSACTIONS           1920u'), ('static bool is_test_command(const mil1553_transaction_t *t)', '\n/* Cobertura exacta por SA, WC y direccion. Memoria fija: 15360 bytes.\n * Actualizar solamente al finalizar cada mensaje (incluidos timeouts). */\nstatic uint32_t sa_started[2][30][32];\nstatic uint32_t sa_ok[2][30][32];\nstatic void sa_record(unsigned tr, unsigned sa, unsigned wc, bool valid)\n{\n    if (tr > 1u || sa < 1u || sa > 30u || wc < 1u || wc > 32u) return;\n    sa_started[tr][sa-1u][wc-1u]++;\n    if (valid) sa_ok[tr][sa-1u][wc-1u]++;\n}\nstatic void sa_print(void)\n{\n    unsigned covered=0u, all_ok=0u;\n    printf("\\n--- COBERTURA SA (OK / iniciadas) ---\\nSA | BC->RT | RT->BC\\n");\n    for (unsigned a=0; a<30u; a++) {\n        uint32_t n[2]={0}, k[2]={0};\n        for (unsigned t=0; t<2u; t++) for (unsigned w=0; w<32u; w++) {\n            uint32_t started=sa_started[t][a][w], ok=sa_ok[t][a][w];\n            n[t]+=started; k[t]+=ok;\n            if (started) covered++;\n            if (started && started==ok) all_ok++;\n        }\n        printf("%2u | %lu/%lu | %lu/%lu\\n", a+1u,\n            (unsigned long)k[0],(unsigned long)n[0],(unsigned long)k[1],(unsigned long)n[1]);\n    }\n    printf("Combinaciones SA/WC/TR observadas: %u/1920\\n",covered);\n    printf("Combinaciones SA/WC/TR sin fallos: %u/1920\\n",all_ok);\n    printf("\\n--- MATRIZ SA/WC (OK / iniciadas) ---\\nSA WC | BC->RT | RT->BC\\n");\n    for (unsigned a=0; a<30u; a++) for (unsigned w=0; w<32u; w++)\n        printf("%2u %2u | %lu/%lu | %lu/%lu\\n",a+1u,w+1u,\n            (unsigned long)sa_ok[0][a][w],(unsigned long)sa_started[0][a][w],\n            (unsigned long)sa_ok[1][a][w],(unsigned long)sa_started[1][a][w]);\n}\n\nstatic bool is_test_command(const mil1553_transaction_t *t)'), ('return t->rt_address == 20u && t->subaddress == 18u &&', 'return t->rt_address == 20u && t->subaddress >= 1u && t->subaddress <= 30u &&'), ('Solo para el ensayo RT20/SA18; otros mensajes mantienen', 'Solo para el ensayo RT20/SA1..30; otros mensajes mantienen'), ('        if (complete && t->valid && t->data_count == wc && t->status_seen)\n            test_wc_ok[tr][wc - 1u]++;', '        bool test_valid = complete && t->valid && t->data_count == wc && t->status_seen;\n        if (test_valid)\n            test_wc_ok[tr][wc - 1u]++;\n        sa_record(tr,t->subaddress,wc,test_valid);'), ('        "WC                   : 1..32\\n"', '        "SA                   : 1..30\\n"\n        "WC                   : 1..32\\n"'), ('                signature_print();', '                sa_print();\n                signature_print();')]
def blob(b):
    return hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()
def transform(s):
    for old,new in PATCHES:
        if s.count(old)!=1: raise ValueError("Ancla no coincide: "+repr(old))
        s=s.replace(old,new,1)
    return s

def main():
    p=argparse.ArgumentParser();p.add_argument("root",nargs="?",default=".");p.add_argument("--check",action="store_true");a=p.parse_args()
    f=Path(a.root).resolve()/"main.c"
    try:
        raw=f.read_bytes();normal=raw.replace(b"\r\n",b"\n")
        if EXPECTED not in (blob(raw),blob(normal)):raise ValueError("main.c no coincide con FLASK-MIL validado; no se modifico nada")
        s=transform(normal.decode("utf-8"))
        if a.check:print("OK: version y anclas verificadas. Sin cambios.");return
        backup=f.with_name("main_DATA_variable_validado.c.bak")
        if backup.exists():raise ValueError("Backup existente: "+str(backup))
        shutil.copy2(f,backup)
        out=s.encode("utf-8")
        if b"\r\n" in raw:out=out.replace(b"\n",b"\r\n")
        tmp=f.with_name("main_sa.tmp");tmp.write_bytes(out);tmp.replace(f)
        print("BM actualizado; backup:",backup)
    except (ValueError,OSError) as e:raise SystemExit("ABORTADO: "+str(e))
if __name__=="__main__":main()
