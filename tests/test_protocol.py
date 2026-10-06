import ctypes as C
import hashlib, os, random
from pathlib import Path
import pytest
from fido2 import cbor
from fido2.webauthn import AuthenticatorData, AttestedCredentialData
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes
from cryptography.exceptions import InvalidSignature

ROOT=Path(__file__).resolve().parent.parent
lib=C.CDLL(str(ROOT/'build/mini-test.dll'))
# Test exports exercise exactly the C protocol/crypto/storage sources used in firmware.
lib.host_ctap.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t]
lib.host_ctap.restype=C.c_size_t
def call(command,body=None):
    request=bytes([command])+(cbor.encode(body) if body is not None else b'')
    return raw(request)
def raw(request):
    response=C.create_string_buffer(1200)
    n=lib.host_ctap(request,len(request),response,1200)
    result=response.raw[:n]
    assert 1<=n<=1200
    return result[0],cbor.decode(result[1:]) if n>1 else None
def make(rp='example.com',extras=None):
    return call(1,{1:hashlib.sha256(b'register').digest(),2:{'id':rp,'name':'Test'},3:{'id':b'user','name':'user'},4:[{'type':'public-key','alg':-7}],**(extras or {})})
def assertion(cid,rp='example.com',extras=None):
    challenge=hashlib.sha256(b'login').digest()
    status,result=call(2,{1:rp,2:challenge,3:[{'type':'public-key','id':cid}],**(extras or {})})
    return status,result,challenge
def register():
    status,result=make();assert status==0
    auth=AuthenticatorData(result[2]);assert auth.is_user_present();assert not auth.is_user_verified()
    return auth.credential_data
@pytest.fixture(autouse=True)
def reset():lib.host_init()

def test_info_truthfully_advertises_capabilities():
    status,info=call(4);assert status==0
    assert info[1]==['FIDO_2_0'];assert info[5]==1200
    assert info[4]=={'rk':True,'up':True,'plat':False,'clientPin':False}
    assert info[6]==[1]

def test_registration_assertion_and_counter_verified_by_independent_crypto():
    cred=register()
    for expected in (1,2,3):
        status,result,challenge=assertion(cred.credential_id);assert status==0
        auth=AuthenticatorData(result[2]);assert auth.counter==expected;assert auth.rp_id_hash==hashlib.sha256(b'example.com').digest()
        cred.public_key.verify(bytes(auth)+challenge,result[3])

def test_credentials_persist_across_reboot():
    cred=register();lib.host_reboot()
    status,result,challenge=assertion(cred.credential_id);assert status==0
    cred.public_key.verify(result[2]+challenge,result[3])

def test_rp_binding():
    cred=register();assert assertion(cred.credential_id,'evil.example')[0]==0x2e

def test_touch_required_and_uv_not_faked():
    lib.host_presence(0);assert make()[0]==0x2f
    lib.host_presence(1);cred=register()
    lib.host_presence(0);assert assertion(cred.credential_id)[0]==0x2f
    assert make(extras={7:{'uv':True}})[0]==0x2c
    lib.host_presence(1);assert make(extras={7:{'rk':True}})[0]==0
    assert make(extras={8:b''})[0]==0x35

def test_preflight_no_touch_sets_up_false():
    cred=register();lib.host_presence(0)
    status,result,challenge=assertion(cred.credential_id,extras={5:{'up':False}});assert status==0
    auth=AuthenticatorData(result[2]);assert not auth.is_user_present()
    cred.public_key.verify(result[2]+challenge,result[3])

def test_exclude_list_and_algorithm_rejection():
    cred=register();assert make(extras={5:[{'type':'public-key','id':cred.credential_id}]})[0]==0x19
    assert make(extras={4:[{'type':'public-key','alg':-8}]})[0]==0x26

def test_reset_invalidates_old_and_never_reuses_credential_id():
    old=register();assert call(7)[0]==0
    assert assertion(old.credential_id)[0]==0x2e
    new=register();assert old.credential_id!=new.credential_id
    assert assertion(old.credential_id)[0]==0x2e

def test_reset_limited_to_first_ten_seconds_and_touch():
    lib.host_time(10001);assert call(7)[0]==0x30
    lib.host_time(0);lib.host_presence(0);assert call(7)[0]==0x2f

def flash(a,n):
    b=C.create_string_buffer(n);assert lib.host_flash(a,b,n);return b.raw

def test_external_flash_encrypts_private_keys_and_detects_tampering():
    cred=register();record=flash(0xf1000,512)
    assert hashlib.sha256(b'example.com').digest() not in record
    lib.host_corrupt(0xf1000+50,1)
    assert assertion(cred.credential_id)[0]==0x2e

def test_torn_write_does_not_destroy_existing_key_or_publish_partial_key():
    cred=register();lib.host_fail_write(80);assert make('new.example')[0]==0x28
    lib.host_fail_write(-1);lib.host_reboot()
    assert assertion(cred.credential_id)[0]==0
    assert flash(0xf1200,4)==b'\xff'*4

def test_gc_recycles_signature_records_without_exhaustion():
    cred=register()
    for _ in range(300):assert assertion(cred.credential_id)[0]==0
    lib.host_reboot()
    status,result,challenge=assertion(cred.credential_id);assert status==0
    assert AuthenticatorData(result[2]).counter==301
    cred.public_key.verify(result[2]+challenge,result[3])

def test_cbor_rejects_truncation_and_deep_nesting():
    valid=bytes([1])+cbor.encode({1:b'x'*32,2:{'id':'example.com'},3:{'id':b'user'},4:[{'type':'public-key','alg':-7}]})
    for end in range(1,len(valid)):assert raw(valid[:end])[0]!=0
    assert raw(b'\x01'+b'\x81'*12+b'\x00')[0]==0x12
    assert raw(b'\x01\xa0\x00')[0]==0x12

def test_random_malformed_input_is_bounded():
    rng=random.Random(241)
    for _ in range(2000):
        p=bytes([rng.choice([1,2])])+rng.randbytes(rng.randrange(0,300))
        assert raw(p)[0]!=0

def packets():
    out=[];b=C.create_string_buffer(64)
    while lib.host_pop(b):out.append(b.raw)
    return out
def send(cid,cmd,payload=b''):
    first=cid.to_bytes(4,'big')+bytes([cmd|128])+len(payload).to_bytes(2,'big')+payload[:57]
    lib.host_packet(first.ljust(64,b'\0'))
    for seq,start in enumerate(range(57,len(payload),59)):
        lib.host_packet((cid.to_bytes(4,'big')+bytes([seq])+payload[start:start+59]).ljust(64,b'\0'))
    return packets()
def channel():
    reply=send(0xffffffff,6,b'12345678');assert reply[0][7:15]==b'12345678'
    return int.from_bytes(reply[0][15:19],'big')
def assemble(ps):
    n=int.from_bytes(ps[0][5:7],'big');return (ps[0][7:]+b''.join(p[5:] for p in ps[1:]))[:n]

def test_ctaphid_init_ping_and_fragmented_registration():
    cid=channel();payload=bytes(range(256))*3
    assert assemble(send(cid,1,payload))==payload
    request=b'\x01'+cbor.encode({1:b'x'*32,2:{'id':'example.com'},3:{'id':b'user'},4:[{'type':'public-key','alg':-7}]})
    response=assemble(send(cid,0x10,request));assert response[0]==0
    assert cbor.decode(response[1:])[1]=='none'

def test_ctaphid_invalid_sequence_and_oversized_frame():
    cid=channel();prefix=cid.to_bytes(4,'big')
    lib.host_packet((prefix+b'\x90\x00\x64'+b'x'*57))
    lib.host_packet((prefix+b'\x01'+b'x'*59));assert packets()[0][7]==4
    lib.host_packet((prefix+b'\x90\xff\xff').ljust(64,b'\0'));assert packets()[0][7]==3

def test_ctaphid_partial_packet_timeout_and_cancel():
    cid=channel();prefix=cid.to_bytes(4,'big')
    lib.host_packet(prefix+b'\x90\x00\x64'+b'x'*57);lib.host_time(3001);lib.host_poll();assert packets()[0][7]==5
    lib.host_time(0);lib.host_packet(prefix+b'\x90\x00\x64'+b'x'*57);assert send(cid,0x11)==[]
    assert assemble(send(cid,0x10,b'\x04'))[0]==0

def test_maintenance_command_requires_magic_and_touch():
    cid=channel();assert send(cid,0x40,b'wrong')[0][7]==2
    assert lib.host_isp_requested()==0
    lib.host_presence(0);send(cid,0x40,b'MINI-ISP-v1');assert lib.host_isp_requested()==0
    lib.host_presence(1);assert assemble(send(cid,0x40,b'MINI-ISP-v1'))==b'OK'
    assert lib.host_isp_requested()==1
