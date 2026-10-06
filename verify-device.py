"""Real hardware check. Initialization requires explicit --initialize and touch.
Creates ONE credential for mini.test.invalid; never contacts a real account.
"""
import argparse, hashlib, json, os, time
from pathlib import Path
from fido2.hid import CtapHidDevice, list_descriptors
from fido2.ctap2 import Ctap2
from fido2.webauthn import AuthenticatorData

p=argparse.ArgumentParser()
p.add_argument('--initialize',action='store_true',help='Erase last 64KiB, only in first 10s after power-up, requires fresh touch')
p.add_argument('--info-only',action='store_true')
p.add_argument('--wait-replug',action='store_true',help='Wait for disconnect/reconnect before initialization; no shorting needed')
args=p.parse_args()
root=Path(__file__).resolve().parent

if os.name == 'nt':
    import ctypes
    if not ctypes.windll.shell32.IsUserAnAdmin():
        raise SystemExit('Windows raw FIDO HID requires an elevated PowerShell 7 session. No commands sent.')

if args.wait_replug:
    if not args.initialize:
        raise SystemExit('--wait-replug requires --initialize')
    def present():
        return any(d.vid==0x1209 and d.pid==1 for d in list_descriptors())
    print('READY: unplug MINI, then reconnect normally WITHOUT shorting. Do not touch during startup.',flush=True)
    deadline=time.monotonic()+180
    while present():
        if time.monotonic()>deadline:raise SystemExit('Disconnect wait timed out; nothing reset.')
        time.sleep(0.2)
    print('Waiting for MINI to reconnect...',flush=True)
    while not present():
        if time.monotonic()>deadline:raise SystemExit('Reconnect wait timed out; nothing reset.')
        time.sleep(0.2)
devices=[d for d in CtapHidDevice.list_devices() if d.descriptor.vid==0x1209 and d.descriptor.pid==1]
if len(devices)!=1:
    raise SystemExit(f'Expected exactly one MINI 1209:0001, found {len(devices)}. No commands sent.')
device=devices[0]
ctap=Ctap2(device)
print('MINI:',device.descriptor)
print('Versions:',ctap.info.versions,'Options:',ctap.info.options)
(root/'build/hardware-getinfo.json').write_text(json.dumps({'versions':ctap.info.versions,'options':ctap.info.options},indent=2),encoding='utf-8')
if args.info_only:
    raise SystemExit(0)
if args.initialize:
    print('Initializing credential storage: last 64KiB will be erased. Touch and release the MINI now.',flush=True)
    ctap.reset()
    print('Storage initialized.')
rp='mini.test.invalid'
print('Registering a LOCAL TEST credential. Touch and release the MINI now.',flush=True)
registration=ctap.make_credential(os.urandom(32),{'id':rp,'name':'MINI prototype local test'},{'id':b'mini-lab','name':'mini-lab'},[{'type':'public-key','alg':-7}])
cred=registration.auth_data.credential_data
challenge=os.urandom(32)
print('Verifying a LOCAL TEST signature. Touch and release the MINI now.',flush=True)
result=ctap.get_assertion(rp,challenge,[{'type':'public-key','id':cred.credential_id}])
cred.public_key.verify(bytes(result.auth_data)+challenge,result.signature)
assert result.auth_data.rp_id_hash==hashlib.sha256(rp.encode()).digest()
assert result.auth_data.is_user_present() and not result.auth_data.is_user_verified()
assert result.auth_data.counter>=1
state={'time':time.strftime('%Y-%m-%dT%H:%M:%S%z'),'usb_vid':'1209','usb_pid':'0001','versions':ctap.info.versions,'options':ctap.info.options,'registration_verified':True,'assertion_signature_verified':True,'counter':result.auth_data.counter,'credential_id':cred.credential_id.hex(),'cose_public_key':{str(k):v.hex() if isinstance(v,bytes) else v for k,v in cred.public_key.items()},'note':'local test only; display/touch visual quality requires human observation'}
(root/'build/hardware-acceptance.json').write_text(json.dumps(state,indent=2),encoding='utf-8')
print('PASS: registration, ES256 signature, RP hash, UP flag and signature counter verified.')
