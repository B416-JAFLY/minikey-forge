"""Reset-only maintenance must never create credentials or remove state on failure."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace

import pytest
from fido2.ctap import CtapError

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('reset_device', ROOT / 'reset-device.py')
reset = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reset)


class Authenticator:
    def __init__(self, pin=False, reset_error=None):
        self.pin = pin
        self.reset_error = reset_error
        self.calls = []

    def reset(self):
        self.calls.append('reset')
        if self.reset_error:
            raise self.reset_error

    def get_info(self):
        self.calls.append('info')
        return SimpleNamespace(options={'clientPin': self.pin})

    def client_pin(self, proto, command):
        assert (proto, command) == (1, 1)
        self.calls.append('retries')
        return {3: 8}

    def get_assertion(self, rp, challenge, allow, **kwargs):
        self.calls.append(('assertion', rp, allow, kwargs['options']))
        assert len(challenge) == 32
        raise CtapError(CtapError.ERR.NO_CREDENTIALS)


def test_reset_only_verifies_pin_and_old_keys_without_registration():
    ctap = Authenticator()
    result = reset.reset_and_verify(ctap, [('localhost', b'test-id')])
    assert result['pin_unset'] and result['new_credentials_created'] == 0
    assert result['known_credentials_rejected'] == 1
    assert ctap.calls[:3] == ['reset', 'info', 'retries']
    assert ctap.calls[3][3] == {'up': False}
    assert len(ctap.calls) == 7


def test_reset_failure_stops_verification_and_preserves_host_state():
    ctap = Authenticator(reset_error=CtapError(CtapError.ERR.NOT_ALLOWED))
    with pytest.raises(CtapError):
        reset.reset_and_verify(ctap, [])
    assert ctap.calls == ['reset']


def test_remaining_pin_fails_acceptance():
    ctap = Authenticator(pin=True)
    with pytest.raises(RuntimeError, match='PIN remains'):
        reset.reset_and_verify(ctap, [])
    assert ctap.calls == ['reset', 'info']


def test_preview_never_opens_a_device(monkeypatch):
    def unexpected():
        raise AssertionError('Device enumeration forbidden in preview')
    monkeypatch.setattr(reset, 'matching_descriptors', unexpected)
    assert reset.main([]) == 0


def test_browser_archive_keeps_public_evidence_and_never_touches_env(tmp_path):
    build = tmp_path / 'build'
    build.mkdir()
    for name in ('browser-credential.json', 'browser-passkey.json'):
        (build / name).write_text('{"public":"test"}')
    (tmp_path / '.env').write_text('retained-secret-placeholder')
    assert len(reset.archive_browser_state(tmp_path, 'test-run')) == 2
    assert not (build / 'browser-passkey.json').exists()
    assert (build / 'reset-history/test-run/browser-passkey.json').exists()
    assert (tmp_path / '.env').read_text() == 'retained-secret-placeholder'
