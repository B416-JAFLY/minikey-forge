"""Inspect the browser credential's actual CTAP assertion without resetting it."""
import ctypes,json,os,time,threading,hashlib
from pathlib import Path
from fido2 import cbor
from fido2.hid import CtapHidDevice
from fido2.webauthn import AttestedCredentialData,AuthenticatorData
root=Path(__file__).resolve().parent
if os.name=='nt' and not ctypes.windll.shell32.IsUserAnAdmin():
    raise SystemExit('Run in elevated PowerShell 7. No commands sent.')
record=json.loads((root/'build/browser-credential.json').read_text())
cred=AttestedCredentialData(bytes.fromhex(record['credential_data']))
devices=[d for d in CtapHidDevice.list_devices() if d.descriptor.vid==0x1209 and d.descriptor.pid==1]
if len(devices)!=1:raise SystemExit('Expected exactly one MINI.')
device=devices[0]
results=[]
try:
    for up in (False,True):
        start=time.monotonic();events=[]
        print('Silent preflight (no touch required).' if not up else 'Fresh touch required: release, touch MINI, then release.',flush=True)
        challenge=os.urandom(32)
        request=b'\x02'+cbor.encode({1:'localhost',2:challenge,3:[{'type':'public-key','id':cred.credential_id}],5:{'up':up,'uv':False}})
        def keepalive(status):
            elapsed=round(time.monotonic()-start,3)
            events.append({'seconds':elapsed,'status':int(status)})
            print(f'{elapsed:.2f}s: '+('waiting for touch' if int(status)==2 else 'processing'),flush=True)
        cancel=threading.Event();timer=threading.Timer(70,cancel.set);timer.start()
        try:response=device.call(0x10,request,event=cancel,on_keepalive=keepalive)
        finally:timer.cancel()
        elapsed=round(time.monotonic()-start,3)
        result={'up_requested':up,'seconds':elapsed,'keepalive':events,'request_hex':request.hex(),'response_hex':response.hex()}
        results.append(result)
        if not response or response[0]!=0:raise ValueError(f'CTAP status: {response[:1].hex()}')
        value=cbor.decode(response[1:])
        assert cbor.encode(value)==response[1:],'Noncanonical CBOR'
        assert value[1]=={'id':cred.credential_id,'type':'public-key'},'Wrong credential descriptor'
        auth=AuthenticatorData(value[2])
        assert auth.rp_id_hash==hashlib.sha256(b'localhost').digest()
        assert auth.is_user_present()==up
        cred.public_key.verify(bytes(auth)+challenge,value[3])
        result['counter']=auth.counter;result['signature_verified']=True
        print(f'PASS: canonical CBOR, credential ID, RP hash, UP and ES256 signature; counter={auth.counter}, total={elapsed}s.',flush=True)
finally:
    device.close()
    (root/'build/browser-raw-login.json').write_text(json.dumps(results,indent=2))
