"""Exercise flash selection and failure guards without touching USB hardware."""
from pathlib import Path
import runpy
import subprocess
import sys
import pytest

SCRIPT = Path(__file__).resolve().parents[1] / 'wait-flash.py'

def invoke(monkeypatch, responses, execute=True):
    calls = []
    def run(command, **kwargs):
        calls.append(command)
        code, output = responses.pop(0)
        return subprocess.CompletedProcess(command, code, output, '')
    monkeypatch.setattr(subprocess, 'run', run)
    monkeypatch.setattr(Path, 'write_text', lambda *args, **kwargs: None)
    monkeypatch.setattr(sys, 'argv', [str(SCRIPT)] + (['--execute'] if execute else []))
    with pytest.raises(SystemExit) as result:
        runpy.run_path(str(SCRIPT), run_name='__main__')
    return result.value.code, calls

def test_dry_run_does_not_access_usb(monkeypatch):
    code, calls = invoke(monkeypatch, [], False)
    assert code == 0 and calls == []

def test_wrong_chip_never_flashes(monkeypatch):
    code, calls = invoke(monkeypatch, [(0, 'CH32V003F4P6')])
    assert 'Wrong' in code and len(calls) == 1

def test_opening_log_is_not_counted_as_second_device(monkeypatch):
    probe = 'Found 1 USB device\nOpening USB device #0\nDevice #0: CH32X033F8P6[0x5a23]'
    code, calls = invoke(monkeypatch, [(0, 'CH32X033F8P6'), (0, probe), (0, 'verified')])
    assert code == 0 and len(calls) == 3

@pytest.mark.parametrize('probe', [(1, 'Device #0: CH32X033F8P6'),
                                  (0, ''),
                                  (0, 'Device #0: CH32X033F8P6\nDevice #1: CH32X033F8P6')])
def test_unreliable_inventory_never_flashes(monkeypatch, probe):
    code, calls = invoke(monkeypatch, [(0, 'CH32X033F8P6'), probe])
    assert 'ambiguous' in code and len(calls) == 2

@pytest.mark.parametrize('flash_code', [0, 1])
def test_flash_always_disables_reset_and_propagates_failure(monkeypatch, flash_code):
    code, calls = invoke(monkeypatch, [(0, 'CH32X033F8P6'),
                                       (0, 'Device #0: CH32X033F8P6'),
                                       (flash_code, 'flash output')])
    assert len(calls) == 3 and calls[-1][1:3] == ['flash', '--no-reset']
    assert (code == 0) == (flash_code == 0)
