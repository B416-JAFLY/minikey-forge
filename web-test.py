"""Loopback-only WebAuthn prototype acceptance, no external services."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json, secrets, time, threading, sys
from fido2.server import Fido2Server
from fido2.webauthn import AttestedCredentialData, AuthenticationResponse

ROOT=Path(__file__).resolve().parent
PASSKEY_MODE='--passkey' in sys.argv
PORT=8766 if PASSKEY_MODE else 8765
ORIGIN=f'http://localhost:{PORT}'
UV='required' if PASSKEY_MODE else 'discouraged'
RK='required' if PASSKEY_MODE else 'discouraged'
TOKEN=secrets.token_urlsafe(32)
server=Fido2Server({'id':'localhost','name':'MINI local prototype'},verify_origin=lambda origin:origin==ORIGIN)
server.timeout=60000
server.allowed_algorithms=[p for p in server.allowed_algorithms if p.alg==-7]
pending={}
lock=threading.Lock()
record_path=ROOT/('build/browser-passkey.json' if PASSKEY_MODE else 'build/browser-credential.json')

def load():
    if not record_path.exists():return None,None
    record=json.loads(record_path.read_text())
    return AttestedCredentialData(bytes.fromhex(record['credential_data'])),record

class Handler(BaseHTTPRequestHandler):
    def reply(self,status,data,content_type='application/json'):
        raw=json.dumps(data).encode() if content_type=='application/json' else data
        self.send_response(status)
        self.send_header('Content-Type',content_type)
        self.send_header('Content-Length',str(len(raw)))
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'")
        self.end_headers();self.wfile.write(raw)
    def host_ok(self):return self.headers.get('Host')==f'localhost:{PORT}'
    def do_GET(self):
        if not self.host_ok():return self.reply(403,{'error':'Use '+ORIGIN})
        if self.path=='/':
            return self.reply(200,(ROOT/'web-test.html').read_bytes(),'text/html; charset=utf-8')
        if self.path=='/app.js':
            return self.reply(200,(ROOT/'web-test.js').read_bytes(),'text/javascript; charset=utf-8')
        if self.path=='/api/state':
            _,record=load()
            return self.reply(200,{'token':TOKEN,'registered':record is not None,'counter':record['counter'] if record else None})
        return self.reply(404,{'error':'Not found'})
    def do_POST(self):
        try:
            n=int(self.headers.get('Content-Length','0'))
            if not 0<n<=16384:return self.reply(413,{'error':'Invalid body length'})
            self.connection.settimeout(5)
            raw=self.rfile.read(n)
        except (ValueError,TimeoutError):
            return self.reply(400,{'error':'Invalid request body'})
        if not self.host_ok() or self.headers.get('Origin')!=ORIGIN or self.headers.get('X-MINI-Token')!=TOKEN:
            return self.reply(403,{'error':'Origin or request token mismatch'})
        try:
            data=json.loads(raw)
            with lock:
                cred,record=load()
                if self.path=='/api/register/begin':
                    if cred:raise ValueError('This page already has a credential; use sign-in.')
                    options,state=server.register_begin({'id':b'mini-browser-test','name':'mini-local-test','displayName':'MINI 本地测试用户'},resident_key_requirement=RK,user_verification=UV,authenticator_attachment='cross-platform')
                    pending['register']=(state,time.monotonic()+120)
                    return self.reply(200,dict(options))
                if self.path=='/api/login/begin':
                    if not cred:raise ValueError('Register first.')
                    options,state=server.authenticate_begin(None if PASSKEY_MODE else [cred],user_verification=UV)
                    pending['login']=(state,time.monotonic()+120)
                    return self.reply(200,dict(options))
                if self.path=='/api/register/complete':
                    state,expires=pending.pop('register')
                    if time.monotonic()>expires:raise ValueError('Challenge expired.')
                    auth=server.register_complete(state,data)
                    record={'credential_data':bytes(auth.credential_data).hex(),'counter':auth.counter,'browser_registration_verified':True,'origin':ORIGIN}
                    record_path.write_text(json.dumps(record,indent=2))
                    return self.reply(200,{'ok':True,'counter':auth.counter})
                if self.path=='/api/login/complete':
                    state,expires=pending.pop('login')
                    if time.monotonic()>expires:raise ValueError('Challenge expired.')
                    response=AuthenticationResponse.from_dict(data)
                    server.authenticate_complete(state,[cred],response)
                    counter=response.response.authenticator_data.counter
                    if counter<=record['counter']:raise ValueError('Counter did not increase.')
                    record['counter']=counter
                    record['browser_signature_verified']=True
                    record_path.write_text(json.dumps(record,indent=2))
                    return self.reply(200,{'ok':True,'counter':counter})
                return self.reply(404,{'error':'Not found'})
        except (ValueError,KeyError,TypeError) as error:
            return self.reply(400,{'error':str(error) or 'Invalid response'})

if __name__=='__main__':
    print('MINI WebAuthn test ready: '+ORIGIN+' (loopback only)',flush=True)
    ThreadingHTTPServer(('127.0.0.1',PORT),Handler).serve_forever()
