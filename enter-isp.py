import argparse
from fido2.hid import CtapHidDevice
p=argparse.ArgumentParser();p.add_argument('--execute',action='store_true',help='Request ISP, requiring fresh touch; does not erase or flash');a=p.parse_args()
devices=[d for d in CtapHidDevice.list_devices() if d.descriptor.vid==0x1209 and d.descriptor.pid==1]
if len(devices)!=1:raise SystemExit(f'Expected one MINI, found {len(devices)}')
if not a.execute:raise SystemExit('Ready. Use --execute to request ISP with touch confirmation.')
print('Touch and release MINI to enter factory ISP. No firmware is flashed by this script.',flush=True)
print(devices[0].call(0x40,b'MINI-ISP-v1'))
