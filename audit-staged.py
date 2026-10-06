"""Audit staged paths/content without printing secrets. Run before every commit/push."""
from pathlib import Path
import subprocess,re,sys
from provision import load_master_key
root=Path(__file__).resolve().parent
paths=subprocess.check_output(['git','diff','--cached','--name-only','-z'],cwd=root).decode().split('\0')
forbidden={'build','private','tools','vendor','archive','.venv'}
key=None
try:key=load_master_key(root)
except ValueError:pass
problems=[]
for name in filter(None,paths):
    p=Path(name)
    if p.parts[0] in forbidden or (p.name.startswith('.env') and p.name!='.env.example') or p.suffix.lower() in {'.bin','.hex','.elf','.dll','.o','.zip'}:problems.append(name+': forbidden path');continue
    content=subprocess.check_output(['git','show',':'+name],cwd=root)
    if len(content)>2_000_000:problems.append(name+': unexpectedly large');continue
    if key:
        if key.hex().encode() in content.lower() or key in content:problems.append(name+': device key')
        for match in re.finditer(rb'mini_master_key\[32\]\s*=\s*\{([^}]+)\}',content):
            try:values=bytes(int(v.strip(),0) for v in match.group(1).split(b',') if v.strip())
            except ValueError:continue
            if values==key:problems.append(name+': device key initializer')
    if re.search(rb'(gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9]{32,})',content):problems.append(name+': possible access token')
if problems:
    print('\n'.join(problems));sys.exit(1)
print('PASS: staged paths/content exclude device key, firmware binaries, local data and access-token patterns.')
