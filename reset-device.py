"""Erase MINI credentials/PIN only; never create a credential or flash firmware."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import secrets
from threading import Event
import time

from fido2.ctap import CtapError
from fido2.ctap2 import Ctap2
from fido2.hid import list_descriptors, open_device
from fido2.webauthn import AttestedCredentialData

ROOT = Path(__file__).resolve().parent
AAGUID = bytes.fromhex('6d696e69663240018000000000000001')
RESIDENT_RPS = ('localhost', 'minikey-forge.test', 'mini.test.invalid')


def load_known_credentials(root):
    known = []
    for name, rp in [('browser-credential.json', 'localhost'),
                     ('browser-passkey.json', 'localhost'),
                     ('pin-resident-hardware-acceptance.json', 'minikey-forge.test')]:
        path = root / 'build' / name
        if path.exists():
            record = json.loads(path.read_text(encoding='utf-8'))
            encoded = record.get('credential_data') or record.get('public_credential_data')
            if encoded:
                known.append((rp, AttestedCredentialData(bytes.fromhex(encoded)).credential_id))
    path = root / 'build/hardware-acceptance.json'
    if path.exists():
        record = json.loads(path.read_text(encoding='utf-8'))
        if record.get('credential_id'):
            known.append(('mini.test.invalid', bytes.fromhex(record['credential_id'])))
    return list(dict.fromkeys(known))


def assert_no_credentials(ctap, rp, credential_id=None):
    event = Event()
    # An unexpectedly remaining resident credential must not trigger a signature.
    def keepalive(status):
        if int(status) == 2:
            event.set()
    allow = ([{'type': 'public-key', 'id': credential_id}]
             if credential_id is not None else None)
    try:
        ctap.get_assertion(rp, secrets.token_bytes(32), allow,
                           options={'up': credential_id is None},
                           event=event, on_keepalive=keepalive)
    except CtapError as error:
        if error.code == CtapError.ERR.NO_CREDENTIALS:
            return
        raise RuntimeError('Post-reset credential check failed; do not register yet') from error
    raise RuntimeError('A credential still responds after reset')


def reset_and_verify(ctap, known):
    ctap.reset()
    info = ctap.get_info()
    if info.options.get('clientPin') is not False:
        raise RuntimeError('PIN remains configured after reset')
    retries = ctap.client_pin(1, 1).get(3)
    if retries != 8:
        raise RuntimeError('PIN retry budget did not return to 8')
    for rp, credential_id in known:
        assert_no_credentials(ctap, rp, credential_id)
    for rp in RESIDENT_RPS:
        assert_no_credentials(ctap, rp)
    return {'reset_completed': True, 'pin_unset': True, 'pin_retries': retries,
            'known_credentials_rejected': len(known),
            'resident_test_rps_empty': len(RESIDENT_RPS),
            'new_credentials_created': 0, 'firmware_flashed': False}


def matching_descriptors():
    matches = [d for d in list_descriptors() if d.vid == 0x1209 and d.pid == 1]
    if len(matches) > 1:
        raise RuntimeError('More than one MINI connected; no device reset')
    return matches


def wait_for_fresh_connection(timeout=180):
    deadline = time.monotonic() + timeout
    present = bool(matching_descriptors())
    print('READY: unplug MINI, then reconnect normally WITHOUT shorting. '
          'Keep your hand off during startup.', flush=True)
    if present:
        while matching_descriptors():
            if time.monotonic() >= deadline:
                raise TimeoutError('Disconnect wait expired; no reset sent')
            time.sleep(0.1)
    print('Waiting for MINI to connect...', flush=True)
    while True:
        matches = matching_descriptors()
        if matches:
            return matches[0]
        if time.monotonic() >= deadline:
            raise TimeoutError('Reconnect wait expired; no reset sent')
        time.sleep(0.1)


def archive_browser_state(root, stamp):
    destination = root / 'build/reset-history' / stamp
    destination.mkdir(parents=True, exist_ok=True)
    moved = []
    for name in ('browser-credential.json', 'browser-passkey.json'):
        source = root / 'build' / name
        if source.exists():
            source.rename(destination / name)
            moved.append(name)
    return moved


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true', help='Erase all MINI credentials and PIN')
    parser.add_argument('--wait-replug', action='store_true', help='Arm before normal power cycling')
    args = parser.parse_args(argv)
    if not args.execute:
        print('Preview only: --execute resets credentials/PIN; no credential is created. '
              'Use --wait-replug to meet the firmware startup window.')
        return 0
    if os.name == 'nt' and not ctypes.windll.shell32.IsUserAnAdmin():
        raise RuntimeError('Use your foreground administrator PowerShell 7; no reset sent')
    known = load_known_credentials(ROOT)
    if args.wait_replug:
        descriptor = wait_for_fresh_connection()
    else:
        matches = matching_descriptors()
        if len(matches) != 1:
            raise RuntimeError('Exactly one MINI required; no reset sent')
        descriptor = matches[0]
    device = open_device(descriptor.path)
    try:
        ctap = Ctap2(device)
        if bytes(ctap.info.aaguid) != AAGUID or 'clientPin' not in ctap.info.options:
            raise RuntimeError('Unexpected authenticator firmware; no reset sent')
        print('MINI ready. ALL credentials and PIN will be cleared. '
              'Touch MINI once now.', flush=True)
        result = reset_and_verify(ctap, known)
    finally:
        device.close()
    stamp = time.strftime('%Y%m%d-%H%M%S')
    result['local_time'] = time.strftime('%Y-%m-%dT%H:%M:%S%z')
    result['browser_test_state_archived'] = archive_browser_state(ROOT, stamp)
    (ROOT / 'build').mkdir(exist_ok=True)
    (ROOT / 'build/reset-acceptance.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('PASS: all credentials reset; PIN unset; retries=8; old test credentials rejected. '
          'No credential created. Firmware/master key retained. '
          'Refresh localhost pages to register again.', flush=True)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f'ERROR: {error}', flush=True)
        raise SystemExit(1)
