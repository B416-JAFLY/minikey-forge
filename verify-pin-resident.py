"""Hardware acceptance for PIN, UV, discoverable credentials. Secrets never logged."""
import ctypes,getpass,hashlib,sys
from pathlib import Path
import json
from fido2.ctap2 import Ctap2
from fido2.ctap2.pin import ClientPin,PinProtocolV1
from fido2.hid import CtapHidDevice
from fido2.webauthn import AuthenticatorData

if not ctypes.windll.shell32.IsUserAnAdmin():raise SystemExit('Run in your foreground administrator PowerShell 7; no background elevation.')
devices=[d for d in CtapHidDevice.list_devices() if d.descriptor.vid==0x1209 and d.descriptor.pid==1]
if len(devices)!=1:raise SystemExit('Expected exactly one MINI')
ctap=Ctap2(devices[0]);cp=ClientPin(ctap,PinProtocolV1());root=Path(__file__).resolve().parent
info=ctap.get_info()
if not info.options.get('rk') or 'clientPin' not in info.options:raise SystemExit('New firmware required: PIN/rk not advertised')
if not info.options['clientPin']:
    pin=getpass.getpass('Choose MINI PIN (4+ characters, keep offline): ')
    if getpass.getpass('Repeat PIN: ')!=pin:raise SystemExit('PIN confirmation differs; no PIN written')
    cp.set_pin(pin);print('PIN configured. No reset or old credential erase performed.')
else:pin=getpass.getpass('MINI PIN: ')
token=cp.get_pin_token(pin);pin=None
rp='minikey-forge.test';challenge=hashlib.sha256(b'MiniKey Forge PIN/RK acceptance register').digest()
print('Registering resident LOCAL TEST credential. Touch MINI now.',flush=True)
result=ctap.make_credential(challenge,{'id':rp,'name':'MiniKey Forge local acceptance'},{'id':b'forge-acceptance','name':'forge-test','displayName':'Forge local test'},[{'type':'public-key','alg':-7}],options={'rk':True},pin_uv_param=cp.protocol.authenticate(token,challenge),pin_uv_protocol=1)
auth=result.auth_data;cred=auth.credential_data
assert auth.is_user_verified() and auth.is_user_present()
challenge=hashlib.sha256(b'MiniKey Forge discoverable login').digest();token=cp.get_pin_token(getpass.getpass('Confirm PIN for discovery test: '))
print('Discovering account WITHOUT credential ID. Touch MINI now.',flush=True)
result=ctap.get_assertion(rp,challenge,pin_uv_param=cp.protocol.authenticate(token,challenge),pin_uv_protocol=1)
assert result.credential['id']==cred.credential_id and result.user['id']==b'forge-acceptance'
assert result.auth_data.is_user_verified() and result.auth_data.is_user_present()
assert result.auth_data.rp_id_hash==hashlib.sha256(rp.encode()).digest()
cred.public_key.verify(bytes(result.auth_data)+challenge,result.signature)
record={'pin_setup':True,'resident_discovery_without_allowlist':True,'uv_and_up':True,'independent_es256_verification':True,'counter':result.auth_data.counter,'public_credential_data':bytes(cred).hex(),'firmware_sha256':json.loads((root/'build/build-manifest.json').read_text())['artifacts']['mini-fido2.hex']}
(root/'build/pin-resident-hardware-acceptance.json').write_text(json.dumps(record,indent=2))
print('PASS: PIN, UV, resident discovery, RP binding and ES256 signature. LCD intentionally disabled.')
