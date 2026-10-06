"""Fetch exact upstream revisions into ignored vendor/. Does not modify global tools."""
from pathlib import Path
import json,subprocess
root=Path(__file__).resolve().parent
for name,meta in json.loads((root/'dependencies.json').read_text()).items():
    path=root/'vendor'/name
    if not path.exists():
        path.parent.mkdir(exist_ok=True)
        subprocess.run(['git','clone','--no-checkout',meta['repository'],str(path)],check=True)
    actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
    if actual!=meta['commit']:
        subprocess.run(['git','-C',str(path),'fetch','origin',meta['commit']],check=True)
        subprocess.run(['git','-C',str(path),'checkout','--detach',meta['commit']],check=True)
    print(name,meta['commit'])
