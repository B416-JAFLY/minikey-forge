"""Authenticate the original lab credential after the verified ISP reboot."""
from pathlib import Path
import ctypes, hashlib, json, os
from fido2.hid import CtapHidDevice
from fido2.ctap2 import Ctap2
from fido2.cose import CoseKey
root=Path(__file__).resolve().parent
if os.name=='nt' and not ctypes.windll.shell32.IsUserAnAdmin():
    raise SystemExit('Run in elevated PowerShell 7. No commands sent.')
record=json.loads((root/'build/hardware-acceptance.json').read_text())
isp=json.loads((root/'build/software-isp-acceptance.json').read_text())
assert isp['software_isp_verified'] and isp['app_getinfo_after_reset']==['FIDO_2_0']
devices=[d for d in CtapHidDevice.list_devices() if d.descriptor.vid==0x1209 and d.descriptor.pid==1]
if len(devices)!=1:raise SystemExit('Expected one MINI; stopped.')
ctap=Ctap2(devices[0]);challenge=os.urandom(32)
print('Touch MINI once to authenticate the ORIGINAL credential after reboot. No reset or new registration.',flush=True)
result=ctap.get_assertion('mini.test.invalid',challenge,[{'type':'public-key','id':bytes.fromhex(record['credential_id'])}])
key=CoseKey.parse({int(k):bytes.fromhex(v) if int(k) in (-2,-3) else v for k,v in record['cose_public_key'].items()})
key.verify(bytes(result.auth_data)+challenge,result.signature)
assert result.auth_data.rp_id_hash==hashlib.sha256(b'mini.test.invalid').digest()
assert result.auth_data.is_user_present() and result.auth_data.counter>record['counter']
(root/'build/persistence-acceptance.json').write_text(json.dumps({'original_credential_verified_after_reboot':True,'counter_before':record['counter'],'counter_after':result.auth_data.counter},indent=2))
print(f'PASS: original credential survived reboot; signature verified; counter {record["counter"]} -> {result.auth_data.counter}.',flush=True)
devices[0].close()
