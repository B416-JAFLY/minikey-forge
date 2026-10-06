"""Check the actual HTTP creation options, including browser-required fields."""
import base64
import json
from pathlib import Path
import runpy
import threading
import urllib.request
from http.server import ThreadingHTTPServer

def test_registration_http_options_have_all_required_members(tmp_path):
    app=runpy.run_path(str(Path(__file__).resolve().parents[1]/'web-test.py'))
    app['load'].__globals__['record_path']=tmp_path/'browser-test.json'
    server=ThreadingHTTPServer(('127.0.0.1',0),app['Handler'])
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    try:
        request=urllib.request.Request(
            f'http://127.0.0.1:{server.server_port}/api/register/begin',
            data=b'{}',headers={'Host':'localhost:8765','Origin':app['ORIGIN'],
                               'X-MINI-Token':app['TOKEN'],'Content-Type':'application/json'})
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open(request,timeout=3) as response:
            options=json.load(response)['publicKey']
        assert options['rp']['name'] and options['rp']['id']=='localhost'
        assert options['user']['name'] and options['user']['displayName']
        assert base64.urlsafe_b64decode(options['user']['id']+'==')==b'mini-browser-test'
        assert len(base64.urlsafe_b64decode(options['challenge']+'=='))>=16
        assert options['pubKeyCredParams']==[{'type':'public-key','alg':-7}]
        assert options['authenticatorSelection']['userVerification']=='discouraged'
        assert options['authenticatorSelection']['residentKey']=='discouraged'
        assert options['authenticatorSelection']['authenticatorAttachment']=='cross-platform'
    finally:
        server.shutdown();server.server_close();thread.join(timeout=3)


def test_passkey_mode_requires_resident_and_uv_and_omits_allowlist(tmp_path,monkeypatch):
    monkeypatch.setattr('sys.argv',['web-test.py','--passkey'])
    app=runpy.run_path(str(Path(__file__).resolve().parents[1]/'web-test.py'))
    state=app['load'].__globals__;state['record_path']=tmp_path/'resident.json'
    server=ThreadingHTTPServer(('127.0.0.1',0),app['Handler']);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    def request(path):
        req=urllib.request.Request(f'http://127.0.0.1:{server.server_port}'+path,data=b'{}',headers={'Host':'localhost:8766','Origin':app['ORIGIN'],'X-MINI-Token':app['TOKEN'],'Content-Type':'application/json'})
        with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req,timeout=3) as r:return json.load(r)['publicKey']
    try:
        options=request('/api/register/begin')
        assert options['authenticatorSelection']['userVerification']=='required'
        assert options['authenticatorSelection']['residentKey']=='required'
        # Only public test key, no actual device state.
        from fido2.webauthn import AttestedCredentialData
        from fido2.cose import ES256
        from cryptography.hazmat.primitives.asymmetric import ec
        cred=AttestedCredentialData.create(b'\0'*16,b'test-id',ES256.from_cryptography_key(ec.generate_private_key(ec.SECP256R1()).public_key()))
        state['record_path'].write_text(json.dumps({'credential_data':bytes(cred).hex(),'counter':0}))
        options=request('/api/login/begin');assert options['userVerification']=='required'
        assert not options.get('allowCredentials')
    finally:server.shutdown();server.server_close();thread.join(timeout=3)
