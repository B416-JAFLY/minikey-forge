import ctypes as C
import hashlib, hmac
from types import SimpleNamespace
import pytest
from fido2.ctap2.pin import ClientPin, PinProtocolV1
from fido2.webauthn import AuthenticatorData
from fido2 import cbor
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.asymmetric import ec
from test_protocol import lib, call, make, assertion, register, flash

@pytest.fixture(autouse=True)
def reset(): lib.host_init()

class Adapter:
    @property
    def info(self):
        _, info=call(4)
        return SimpleNamespace(options=info[4],pin_uv_protocols=info[6])
    def client_pin(self,proto,cmd,**kw):
        keys={'key_agreement':3,'pin_uv_param':4,'new_pin_enc':5,'pin_hash_enc':6}
        status,value=call(6,{1:proto,2:int(cmd),**{keys[k]:v for k,v in kw.items() if v is not None}})
        if status: raise PinError(status)
        return value
class PinError(Exception):
    def __init__(self,status): self.status=status

def client():return ClientPin(Adapter(), PinProtocolV1())
def param(token,hash):return PinProtocolV1().authenticate(token,hash)
def resident(user=b'one',pin_token=None):
    challenge=hashlib.sha256(b'register').digest()
    extras={3:{'id':user,'name':'测试用户','displayName':'Alice'},7:{'rk':True}}
    if pin_token:extras.update({8:param(pin_token,challenge),9:1})
    status,result=make(extras=extras)
    assert status==0
    return AuthenticatorData(result[2]).credential_data

def discover(token=None,rp='example.com'):
    challenge=hashlib.sha256(b'discover').digest()
    body={1:rp,2:challenge}
    if token:body.update({6:param(token,challenge),7:1})
    status,result=call(2,body)
    return status,result,challenge

def test_pin_setup_change_uv_and_independent_verification():
    cp=client();cp.set_pin('123456');assert call(4)[1][4]['clientPin']
    assert make()[0]==0x36
    token=cp.get_pin_token('123456')
    cred=resident(pin_token=token)
    status,result,challenge=discover(token);assert status==0
    auth=AuthenticatorData(result[2]);assert auth.is_user_verified() and auth.is_user_present()
    assert result[4]['id']==b'one' and result[4]['name']=='测试用户'
    cred.public_key.verify(result[2]+challenge,result[3])
    cp.change_pin('123456','abcdef');assert cp.get_pin_token('abcdef')
    with pytest.raises(PinError) as ex:cp.get_pin_token('123456')
    assert ex.value.status==0x31

def test_pin_retries_persist_and_power_cycle_gate():
    cp=client();cp.set_pin('1234')
    for expected in (0x31,0x31,0x34):
        with pytest.raises(PinError) as ex:cp.get_pin_token('badpin')
        assert ex.value.status==expected
    assert cp.get_pin_retries()[0]==5
    with pytest.raises(PinError) as ex:cp.get_pin_token('1234')
    assert ex.value.status==0x34
    lib.host_reboot();assert cp.get_pin_retries()[0]==5
    cp.get_pin_token('1234');assert cp.get_pin_retries()[0]==8
    for cycle in range(3):
        lib.host_reboot()
        for _ in range(min(3,cp.get_pin_retries()[0])):
            with pytest.raises(PinError):cp.get_pin_token('badpin')
    lib.host_reboot();assert cp.get_pin_retries()[0]==0
    with pytest.raises(PinError) as ex:cp.get_pin_token('1234')
    assert ex.value.status==0x32

def test_pin_reset_and_reboot_invalidates_old_token():
    cp=client();cp.set_pin('1234');token=cp.get_pin_token('1234')
    cred=resident(pin_token=token);lib.host_reboot()
    status,_,_=discover(token);assert status==0x33
    fresh=cp.get_pin_token('1234');assert fresh!=token
    assert discover(fresh)[0]==0
    assert call(7)[0]==0;assert not call(4)[1][4]['clientPin']
    assert assertion(cred.credential_id)[0]==0x2e

def test_pin_invalid_auth_and_ecdh_point_and_short_unicode_policy():
    cp=client()
    cp.set_pin('🙂🙂🙂🙂');assert cp.get_pin_token('🙂🙂🙂🙂')
    proto=PinProtocolV1();peer,secret=proto.encapsulate(call(6,{1:1,2:2})[1][1])
    encrypted=proto.encrypt(secret,b'abcd'.ljust(64,b'\0'))
    assert call(6,{1:1,2:4,3:peer,4:b'bad-auth-param!!',5:encrypted,6:b'x'*16})[0]==0x33
    lib.host_init();peer[-2]=b'\0'*32;peer[-3]=b'\0'*32
    assert call(6,{1:1,2:3,3:peer,4:b'x'*16,5:encrypted})[0]==2
    lib.host_init();peer,secret=proto.encapsulate(call(6,{1:1,2:2})[1][1])
    encrypted=proto.encrypt(secret,'🙂🙂🙂'.encode().ljust(64,b'\0'))
    assert call(6,{1:1,2:3,3:peer,4:proto.authenticate(secret,encrypted),5:encrypted})[0]==0x37


def test_resident_multiple_accounts_get_next_and_privacy():
    first=resident(b'one');second=resident(b'two')
    status,result,challenge=discover();assert status==0 and result[5]==2
    assert result[4]=={'id':b'one'}
    first.public_key.verify(result[2]+challenge,result[3])
    status,next_=call(8);assert status==0 and next_[4]=={'id':b'two'}
    second.public_key.verify(next_[2]+challenge,next_[3]);assert call(8)[0]==0x30
    lib.host_reboot();assert discover()[0]==0
    assert discover(rp='evil.example')[0]==0x2e
    assert call(2,{1:'example.com',2:challenge,5:{'up':False}})[0]==0x36

def test_resident_replace_same_user_and_capacity_not_signature_budget():
    old=resident();new=resident();assert old.credential_id!=new.credential_id
    assert lib.host_store_count()==1;assert assertion(old.credential_id)[0]==0x2e
    lib.host_reboot();assert lib.host_store_count()==1
    for i in range(47):resident(f'user{i}'.encode())
    assert lib.host_store_count()==48
    assert make('overflow.example')[0]==0x28
    replacement=resident();assert lib.host_store_count()==48
    for _ in range(30):assert assertion(replacement.credential_id)[0]==0
    lib.host_reboot();assert assertion(replacement.credential_id)[0]==0

@pytest.mark.parametrize('cut',[0,1,20,511,512,515,516,550,600,610,612,613,620,1128])
def test_interrupted_gc_preserves_last_committed_credential(cut):
    cred=register()
    for _ in range(55):assert assertion(cred.credential_id)[0]==0
    assert lib.host_store_used()==56
    lib.host_fail_write(cut);status,_,_=assertion(cred.credential_id)
    lib.host_fail_write(-1);lib.host_reboot()
    status,result,challenge=assertion(cred.credential_id);assert status==0
    assert AuthenticatorData(result[2]).counter>=56
    cred.public_key.verify(result[2]+challenge,result[3])

MASTER=bytes(range(1,33))
def derive(label):return hmac.new(MASTER,label.encode().ljust(64,b'\0')[:60],hashlib.sha256).digest()
def encrypt(data,iv):
    ctx=Cipher(algorithms.AES(derive('storage-encryption')),modes.CTR(iv)).encryptor()
    return ctx.update(data)+ctx.finalize()

def test_legacy_storage_migration_preserves_key_id_rp_and_latest_counter():
    private=ec.generate_private_key(ec.SECP256R1());k=private.private_numbers().private_value.to_bytes(32,'big')
    cid=b'legacy-test-id!!';assert len(cid)==16
    rp=hashlib.sha256(b'example.com').digest()
    header=b'MINIF2v1'+derive('storage-header')+b'0123456789abcdef'
    header+=hmac.new(MASTER,header,hashlib.sha256).digest()[:8]
    lib.host_load_flash(0xf0000,b'\xff'*65536,65536);lib.host_load_flash(0xf0000,header,len(header))
    for slot,counter in enumerate((3,9)):
        iv=bytes([slot])*16;payload=k+rp+counter.to_bytes(4,'big')+b'\0'*16
        data=cid+iv+encrypt(payload,iv)
        record=b'MREC'+data+hmac.new(derive('storage-auth'),data,hashlib.sha256).digest()
        lib.host_load_flash(0xf1000+slot*256,record.ljust(256,b'\xff'),256)
    lib.host_reboot();status,result,challenge=assertion(cid);assert status==0
    assert AuthenticatorData(result[2]).counter==10
    from cryptography.hazmat.primitives import hashes
    private.public_key().verify(result[3],result[2]+challenge,ec.ECDSA(hashes.SHA256()))
    lib.host_reboot();assert assertion(cid)[0]==0
    assert flash(0xf8000,8)==b'MINIF2v2'

@pytest.mark.parametrize('cut',[0,1,2,7])
def test_interrupted_gc_erase_never_erases_active_bank(cut):
    cred=register()
    for _ in range(55):assert assertion(cred.credential_id)[0]==0
    lib.host_fail_erase(cut);assert assertion(cred.credential_id)[0]==0x28
    lib.host_fail_erase(-1);lib.host_reboot()
    status,result,challenge=assertion(cred.credential_id);assert status==0
    cred.public_key.verify(result[2]+challenge,result[3])

def test_get_next_expires_and_pin_configuration_write_failure_fails_closed():
    resident(b'one');resident(b'two');assert discover()[0]==0
    lib.host_time(30001);assert call(8)[0]==0x30
    lib.host_init();lib.host_fail_write(10)
    with pytest.raises(PinError):client().set_pin('1234')
    lib.host_fail_write(-1);lib.host_reboot();assert not call(4)[1][4]['clientPin']
