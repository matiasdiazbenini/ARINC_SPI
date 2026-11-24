"""Aplicar cambios al BM de la rama FLASK-MIL validada RT->BC.
Uso: python preparar_bm.py RUTA_RAIZ_PROYECTO_BM
No modifica PIO, CMake ni archivos de otros nodos.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil

EXPECTED = {
    'main.c': 'bbeca433367a5b14d340481c5b5c6d705c21320f',
    'bus/mil1553_rx.c': '10a217969692f74392603c9a40cbd036890c10b3',
    'bus/mil1553_rx.h': 'e76fd032afbb558697a66da2f900d7d161be7917',
    'bus/mil1553_rx.pio': 'cb2483a147232d127a85acfd93fd9981a7ee8dcf',
}

def blob_sha(data):
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def once(s, old, new):
    if s.count(old) != 1:
        raise ValueError(f'No se encontro exactamente una ocurrencia de {old!r}')
    return s.replace(old,new,1)

def sub_exact(s, pattern, replacement, count):
    s,n=re.subn(pattern,replacement,s)
    if n!=count:
        raise ValueError(f'Esperaba {count} coincidencias, encontre {n}: {pattern}')
    return s

HELPER = r'''

/* Seleccionar RT -> BC tras validar el COMMAND.
 * SM0 sigue recibiendo STATUS sin interrupcion.
 * SM2 espera una sola IRQ (la del STATUS); el COMMAND ya se recibio.
 */
void mil1553_rx_select_rt_to_bc(void)
{
    if (!rx_initialized) return;
    const uint32_t data_mask = (1u << rx_sm_data_bc_to_rt) |
                               (1u << rx_sm_data_rt_to_bc);
    pio_set_sm_mask_enabled(rx_pio, data_mask, false);
    pio_sm_clear_fifos(rx_pio, rx_sm_data_bc_to_rt);
    pio_sm_clear_fifos(rx_pio, rx_sm_data_rt_to_bc);
    pio_sm_restart(rx_pio, rx_sm_data_rt_to_bc);
    pio_sm_exec(rx_pio, rx_sm_data_rt_to_bc,
                pio_encode_jmp(data_bc_to_rt_entry_pc()));
    pio_interrupt_clear(rx_pio, MIL1553_RX_DATA_GATE_IRQ);
    pio_sm_set_enabled(rx_pio, rx_sm_data_rt_to_bc, true);
}
'''

def transform(src):
    c=src['bus/mil1553_rx.c']
    # Solo mascaras de HABILITACION. No modificar mascaras de parada o clkdiv.
    c=sub_exact(c,r'(pio_enable_sm_mask_in_sync\(\s*rx_pio,\s*\(1u << rx_sm_cmd\) \|\s*\(1u << )rx_sm_data_rt_to_bc(\)\s*\);)',r'\g<1>rx_sm_data_bc_to_rt\2',2)
    c=sub_exact(c,r'(pio_enable_sm_mask_in_sync\(\s*rx_pio,\s*\(1u << )rx_sm_data_rt_to_bc(\)\s*\);)',r'\g<1>rx_sm_data_bc_to_rt\2',1)
    c=c.rstrip()+HELPER
    h=once(src['bus/mil1553_rx.h'],'void mil1553_rx_data_rearm(void);','void mil1553_rx_data_rearm(void);\nvoid mil1553_rx_select_rt_to_bc(void);')
    m=sub_exact(src['main.c'],r'(#define TEST_TRANSACTIONS\s+)1000u',r'\g<1>20u',1)
    m=once(m,'"Test                 : RT -> BC\\n"','"Test                 : BIDIRECCIONAL\\n"')
    m=once(m,'/* Iniciar transaccion. */','''/* Seleccionar DATA antes de preparar el diagnostico.
                 * BC -> RT: SM1 ya esta activa, no tocarla.
                 * RT -> BC: SM2 espera IRQ de STATUS.
                 */
                if (tr == BUS_1553_TR_RT_TO_BC)
                {
                    mil1553_rx_select_rt_to_bc();
                }

                /* Iniciar transaccion. */''')
    return {'main.c':m,'bus/mil1553_rx.c':c,'bus/mil1553_rx.h':h}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root',nargs='?',default='.',help='Raiz del proyecto BM (FLASK-MIL)')
    ap.add_argument('--check',action='store_true',help='Verificar sin modificar')
    args=ap.parse_args();root=Path(args.root).resolve();src={};original={}
    for name,expected in EXPECTED.items():
        data=(root/name).read_bytes();normalized=data.replace(b'\r\n',b'\n')
        if expected not in (blob_sha(data),blob_sha(normalized)):
            raise SystemExit(f'ABORTADO: {name} no coincide con la version GitHub revisada. No se escribio nada.')
        src[name]=normalized.decode('utf-8');original[name]=data
    result=transform(src)
    if args.check:
        print('Version y cambios verificados. No se modifico nada.');return
    backup=root/'backup_RT_to_BC_WC32'
    if backup.exists():raise SystemExit('ABORTADO: la carpeta de backup ya existe.')
    for name in result:
        target=backup/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/name,target)
    for name,content in result.items():
        data=content.encode('utf-8')
        if b'\r\n' in original[name]:data=data.replace(b'\n',b'\r\n')
        (root/name).write_bytes(data)
    print('BM bidireccional preparado. Backup:',backup)
    print('Sin cambios en bus/mil1553_rx.pio. Compilar ahora en VS Code.')
if __name__=='__main__':main()
