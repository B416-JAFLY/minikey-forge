"""Verify the touch-gated software ISP round trip; never erase or flash."""
from pathlib import Path
import ctypes
import json
import os
import re
import subprocess
import time
from fido2.hid import CtapHidDevice
from fido2.ctap2 import Ctap2

root = Path(__file__).resolve().parent
if os.name == 'nt' and not ctypes.windll.shell32.IsUserAnAdmin():
    raise SystemExit('Run in elevated PowerShell 7. No commands sent.')
tool = root / 'tools/wchisp-win-x64/wchisp.exe'
def run(*args):
    r = subprocess.run([str(tool), *args], capture_output=True, text=True,
                       encoding='utf-8', errors='replace', timeout=15)
    return r, r.stdout + r.stderr
devices = [d for d in CtapHidDevice.list_devices()
           if d.descriptor.vid == 0x1209 and d.descriptor.pid == 1]
if len(devices) != 1:
    raise SystemExit(f'Expected exactly one MINI, found {len(devices)}.')
r, inventory = run('probe')
if r.returncode or re.findall(r'Device\s*#\s*(\d+)\s*:', inventory, re.I):
    raise SystemExit('Another ISP device is present or probe failed; stopped.')
device = devices[0]
print('Touch MINI once to enter factory ISP. No erase/flash will be performed.', flush=True)
reply = device.call(0x40, b'MINI-ISP-v1')
device.close()
if reply != b'OK':
    raise SystemExit('Unexpected ISP acknowledgement; stopped.')
deadline = time.monotonic() + 15
while True:
    r, info = run('info')
    if r.returncode == 0:
        break
    if time.monotonic() > deadline:
        raise SystemExit('ISP did not appear; no erase or flash performed.')
    time.sleep(0.25)
(root / 'build/software-isp-info.log').write_text(info, encoding='utf-8')
r, inventory = run('probe')
if r.returncode or re.findall(r'Device\s*#\s*(\d+)\s*:', inventory, re.I) != ['0']:
    raise SystemExit('Ambiguous ISP inventory; stopped without reset.')
if 'CH32X033F8P6' not in info:
    raise SystemExit('Unexpected chip; stopped without reset.')
previous = (root / 'build/isp-chip-info.log').read_text(encoding='utf-8')
uid_pattern = r'Chip UID:\s*([0-9A-Fa-f-]+)'
old_uid = re.search(uid_pattern, previous)
new_uid = re.search(uid_pattern, info)
if not old_uid or not new_uid or old_uid.group(1) != new_uid.group(1):
    raise SystemExit('Chip UID mismatch; stopped without reset.')
print('PASS: same MINI entered factory ISP through the touch command. Restarting app...', flush=True)
r, output = run('reset')
if r.returncode:
    raise SystemExit('ISP reset failed: ' + output)
deadline = time.monotonic() + 15
while True:
    devices = [d for d in CtapHidDevice.list_devices()
               if d.descriptor.vid == 0x1209 and d.descriptor.pid == 1]
    if len(devices) == 1:
        break
    if time.monotonic() > deadline:
        raise SystemExit('App did not reappear after ISP reset.')
    time.sleep(0.25)
ctap = Ctap2(devices[0])
(root / 'build/software-isp-acceptance.json').write_text(json.dumps({
    'software_isp_verified': True, 'same_chip_uid': True,
    'app_getinfo_after_reset': ctap.info.versions,
    'erased_or_flashed': False
}, indent=2), encoding='utf-8')
devices[0].close()
print('PASS: app returned and GetInfo succeeded. No firmware/data was erased or flashed.', flush=True)
