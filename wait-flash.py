"""Wait armed before the user's physical action. No driver/option-byte changes."""
from pathlib import Path
import argparse, subprocess, json, hashlib, time, re
p=argparse.ArgumentParser();p.add_argument('--execute',action='store_true');p.add_argument('--wait',type=int,default=3600);a=p.parse_args()
root=Path(__file__).resolve().parent
tool=root/'tools/wchisp-win-x64/wchisp.exe'
image=root/'build/mini-fido2.hex'
manifest=json.loads((root/'build/build-manifest.json').read_text())
assert hashlib.sha256(image.read_bytes()).hexdigest()==manifest['artifacts']['mini-fido2.hex'],'Firmware checksum mismatch'
if not a.execute:
    print('Image verified. --execute arms chip-specific wait/flash/verify; nothing written.')
    raise SystemExit(0)
print('ARMED: waiting for WCH factory ISP. Target must identify as CH32X033F8P6. Image SHA256 verified.',flush=True)
deadline=time.monotonic()+a.wait
while time.monotonic()<deadline:
    r=subprocess.run([str(tool),'info'],capture_output=True,text=True,encoding='utf-8',errors='replace')
    if r.returncode==0:
        info=r.stdout+r.stderr
        print(info,flush=True)
        (root/'build/isp-chip-info.log').write_text(info,encoding='utf-8')
        if 'CH32X033F8P6' not in info:raise SystemExit('Wrong/ambiguous chip; stopped without flash.')
        # Avoid ambiguous multi-device selection. Probe reports all bootloader devices.
        probe=subprocess.run([str(tool),'probe'],capture_output=True,text=True,encoding='utf-8',errors='replace')
        text=probe.stdout+probe.stderr
        (root/'build/isp-probe.log').write_text(text,encoding='utf-8')
        device_numbers=re.findall(r'Device\s*#\s*(\d+)\s*:',text,re.I)
        if probe.returncode or device_numbers!=['0']:raise SystemExit('ISP device inventory failed or is ambiguous; stopped without flash.')
        print('Chip matched. Flashing and verifying, KEEPING ISP active until explicit reset.',flush=True)
        f=subprocess.run([str(tool),'flash','--no-reset',str(image)],capture_output=True,text=True,encoding='utf-8',errors='replace')
        log=f.stdout+f.stderr;print(log,flush=True);(root/'build/flash.log').write_text(log,encoding='utf-8')
        if f.returncode:raise SystemExit('Flash/verify failed; no automatic reset. See build/flash.log.')
        print('FLASH VERIFIED. Device remains in ISP. Explicit reset will start the new firmware.',flush=True)
        raise SystemExit(0)
    if 'No WCH ISP USB device found' not in r.stderr+r.stdout:
        raise SystemExit('ISP probe failed unexpectedly; stopped. '+r.stderr+r.stdout)
    time.sleep(0.25)
raise SystemExit('Wait timeout; no device was flashed.')
