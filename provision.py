"""Local provisioning only. Never prints the device key; no implicit rotation."""
from pathlib import Path
import os,re,secrets,sys

def load_master_key(root):
    value=os.environ.get('MINI_MASTER_KEY_HEX')
    if value is None:
        path=Path(root)/'.env'
        if path.exists():
            entries={}
            for line in path.read_text(encoding='utf-8-sig').splitlines():
                line=line.strip()
                if not line or line.startswith('#'):continue
                name,sep,val=line.partition('=')
                if not sep or name in entries:raise ValueError('Malformed/duplicate .env entry')
                entries[name.strip()]=val.strip()
            value=entries.get('MINI_MASTER_KEY_HEX')
    if value is None or not re.fullmatch(r'[0-9a-fA-F]{64}',value):
        raise ValueError('Provide 64 hex digits in MINI_MASTER_KEY_HEX or .env; no key generated automatically')
    return bytes.fromhex(value)

def write_provision(root,key):
    path=Path(root)/'private/provision.c'
    if path.exists():
        m=re.search(r'mini_master_key\[32\]\s*=\s*\{([^}]+)\}',path.read_text())
        if not m:raise ValueError('Existing provision source is malformed')
        old=bytes(int(v.strip(),0) for v in m.group(1).split(',') if v.strip())
        if old!=key:raise ValueError('Refusing key rotation: existing provision differs from configured key')
        return
    path.parent.mkdir(exist_ok=True)
    path.write_text('#include <stdint.h>\nconst uint8_t mini_master_key[32]={'+','.join(f'0x{v:02x}' for v in key)+'};\n',encoding='ascii')

if __name__=='__main__':
    root=Path(__file__).resolve().parent
    if sys.argv[1:]==['--generate']:
        path=root/'.env'
        if path.exists() or (root/'private/provision.c').exists():raise SystemExit('Refusing to replace existing provisioning')
        with path.open('x',encoding='ascii') as f:f.write('MINI_MASTER_KEY_HEX='+secrets.token_hex(32)+'\n')
        print('Created local .env. Keep a backup; key not displayed.')
    else:
        write_provision(root,load_master_key(root));print('Provisioning validated; key not displayed.')
