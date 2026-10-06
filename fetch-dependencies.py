"""Fetch exact upstream revisions into ignored vendor/. No global tool changes."""
from pathlib import Path
import json,subprocess

def fetch_dependencies(root):
    root=Path(root)
    for name,meta in json.loads((root/'dependencies.json').read_text()).items():
        path=root/'vendor'/name
        cloned=not path.exists()
        if cloned:
            path.parent.mkdir(exist_ok=True)
            subprocess.run(['git','clone','--no-checkout',meta['repository'],str(path)],check=True)
        actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
        if cloned or actual!=meta['commit']:
            if not cloned:
                dirty=subprocess.check_output(['git','-C',str(path),'status','--porcelain'],text=True)
                if dirty:raise RuntimeError('Refusing to replace local dependency changes: '+name)
            subprocess.run(['git','-C',str(path),'fetch','origin',meta['commit']],check=True)
            subprocess.run(['git','-C',str(path),'checkout','--detach',meta['commit']],check=True)
        print(name,meta['commit'])

if __name__=='__main__':fetch_dependencies(Path(__file__).resolve().parent)
