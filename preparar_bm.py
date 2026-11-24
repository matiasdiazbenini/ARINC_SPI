"""Actualiza main.c del BM, base FLASK-MIL WC dinamico 1024 validada."""
import argparse
import hashlib
import re
from pathlib import Path
import shutil

EXPECTED = "91555ac433fc80c13393746e8cb0bd1eefb3f960"
HELPER = '/* Patron de ensayo reproducible por direccion, WC y posicion (base 0).\n * Indices 0..1055 sin colisiones: multiplicador impar y xorshift de 16 bits.\n * No usa el contador de transacciones ni el reloj.\n */\nstatic inline uint16_t test_data_word(unsigned tr, unsigned wc, unsigned pos)\n{\n    uint32_t index = tr * 528u + (wc * (wc - 1u)) / 2u + pos;\n    uint16_t x = (uint16_t)(index * 40503u + 0xBEEFu);\n    x ^= (uint16_t)(x >> 7);\n    x ^= (uint16_t)(x << 9);\n    x ^= (uint16_t)(x >> 8);\n    return x;\n}\n'

def blob_sha(data):
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def once(s, old, new):
    if s.count(old) != 1:
        raise ValueError(f"No coincide el punto de cambio: {old!r}")
    return s.replace(old,new,1)

def transform(s):
    s=once(s,"#define TEST_TRANSACTIONS           1024u","#define TEST_TRANSACTIONS           64u")
    # Registrar toda la matriz de 64 en la prueba inicial, sin exceder capacidad.
    s=once(s,"#define PRINT_FIRST_TRANSACTIONS    6u","#define PRINT_FIRST_TRANSACTIONS    64u")
    s,n=re.subn(r"static const uint16_t diag_expected_data\[32\] =\n\{.*?\n\};",lambda m:HELPER,s,flags=re.S)
    if n!=1: raise ValueError("No coincide la tabla DATA original")
    s=once(s,"diag_expected_data[j]","test_data_word(t->tr,t->word_count,(unsigned)j)")
    if s.count("diag_expected_data[i]")!=2: raise ValueError("No coinciden las comparaciones DATA")
    s=s.replace("diag_expected_data[i]","test_data_word(t->tr,t->word_count,i)")
    s=once(s,"Patron DATA          : prefijo de longitud WC","Patron DATA          : PRNG por direccion, WC y posicion")
    s=s.replace("DIAGNOSTICO DATA / WC 1..32","DIAGNOSTICO DATA PRNG / WC 1..32")
    # Conservar 64 registros iniciales y reservar espacio para errores posteriores.
    s=once(s,"#define BM_DIAG_MAX_SAVED           64u","#define BM_DIAG_MAX_SAVED           128u")
    return s

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root",nargs="?",default=".")
    ap.add_argument("--check",action="store_true")
    args=ap.parse_args()
    p=Path(args.root).resolve()/"main.c"
    try:
        raw=p.read_bytes(); normalized=raw.replace(b"\r\n",b"\n")
        if EXPECTED not in (blob_sha(raw),blob_sha(normalized)):
            raise ValueError("main.c no coincide con la revision GitHub FLASK-MIL WC dinamico 1024. No se modifico nada.")
        result=transform(normalized.decode("utf-8"))
        if args.check:
            print("OK: version y cambios verificados. No se escribio nada."); return
        backup=p.with_name("main_WC_dinamico_validado.c.bak")
        if backup.exists(): raise ValueError(f"Ya existe el backup: {backup}")
        shutil.copy2(p,backup)
        data=result.encode("utf-8")
        if b"\r\n" in raw: data=data.replace(b"\n",b"\r\n")
        tmp=p.with_name("main_DATA_prng.c.tmp"); tmp.write_bytes(data); tmp.replace(p)
        print("BM DATA PRNG preparado. Backup:",backup)
        print("No se modificaron drivers ni PIO.")
    except (ValueError,OSError) as exc:
        raise SystemExit(f"ABORTADO: {exc}")

if __name__=="__main__": main()
