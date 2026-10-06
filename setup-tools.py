"""Download pinned build tools into workspace tools/; verify SHA-256 before extraction."""
from pathlib import Path
import json,hashlib,urllib.request,zipfile
root=Path(__file__).resolve().parent;tools=root/'tools';tools.mkdir(exist_ok=True)
for name,meta in json.loads((root/'toolchain.json').read_text()).items():
    if (tools/meta['directory']).exists():
        print(name,'already present; not replaced');continue
    archive=tools/(name+'.zip')
    if not archive.exists():
        print('Downloading',name,flush=True);urllib.request.urlretrieve(meta['url'],archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=meta['sha256']:raise SystemExit('SHA-256 mismatch: '+name)
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            target=(tools/entry.filename).resolve()
            if not target.is_relative_to(tools.resolve()):raise SystemExit('Unsafe archive path')
        z.extractall(tools)
    print(name,'verified and extracted locally')
