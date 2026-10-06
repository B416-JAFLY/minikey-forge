from pathlib import Path
import subprocess, sys, shutil, hashlib, json
from provision import load_master_key, write_provision
root=Path(__file__).resolve().parent
src=root/'src'; vendor=root/'vendor'; output=root/'build'; output.mkdir(exist_ok=True)
gcc=root/'tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc.exe'
sdk=vendor/'ch32x035/EVT/EXAM/SRC'
common=[src/'cbor.c',src/'crypto.c',src/'store.c',src/'ctap.c',src/'pin.c',src/'transport.c',src/'uECC-mini.c',vendor/'tiny-aes/aes.c',vendor/'crypto-algorithms/sha256.c']
defs=['-DuECC_SUPPORTS_secp160r1=0','-DuECC_SUPPORTS_secp192r1=0','-DuECC_SUPPORTS_secp224r1=0','-DuECC_SUPPORTS_secp256k1=0','-DuECC_WORD_SIZE=4','-DuECC_OPTIMIZATION_LEVEL=2','-DuECC_ENABLE_VLI_API=0','-DAES256=1','-DECB=0','-DCBC=1','-DCTR=1']
includes=[src,vendor/'micro-ecc',vendor/'tiny-aes',vendor/'crypto-algorithms']
if '--host' in sys.argv:
    zig=root/'tools/zig-x86_64-windows-0.15.2/zig.exe'
    args=[str(zig),'cc','-target','x86_64-windows-gnu','-shared','-O1','-g','-Wl,--export-all-symbols','-DuECC_PLATFORM=0',*defs,*[f'-I{p}' for p in includes],*[str(p) for p in common],str(root/'tests/host_platform.c'),'-o',str(output/'mini-test.dll')]
    subprocess.run(args,check=True,cwd=root)
    print('Host DLL built')
    sys.exit()
write_provision(root, load_master_key(root))
files=common+[src/'board.c',src/'main.c',src/'ch32x035_usbfs_device.c',src/'usb_desc.c',src/'system_ch32x035.c',src/'startup.S',root/'private/provision.c']
files += [sdk/'Peripheral/src'/f'ch32x035_{name}.c' for name in ['rcc','gpio','adc']]
includes += [sdk/'Peripheral/inc',sdk/'Core']
linker=output/'mini.ld'
linker.write_text((sdk/'Ld/Link.ld').read_text().replace('__stack_size = 2048','__stack_size = 6144').replace('ORIGIN = 0x00000000, LENGTH = 62K','ORIGIN = 0x00002000, LENGTH = 54K'))
base=['-march=rv32imac_zicsr_zifencei','-mabi=ilp32','-msmall-data-limit=8','-Os','-g','-ffunction-sections','-fdata-sections','-fno-common','-fstack-usage','-Wall','-Wextra','-Wno-misleading-indentation',*defs,*[f'-I{p}' for p in includes]]
objects=[]
for i,p in enumerate(files):
    obj=output/f'{i:02}-{p.stem}.o';objects.append(obj)
    speed_flags=['-O2'] if p.name in ('board.c','uECC-mini.c') else []
    subprocess.run([str(gcc),*base,*speed_flags,'-c',str(p),'-o',str(obj)],check=True,cwd=root)
elf=output/'mini-app.elf'
subprocess.run([str(gcc),*base,'-nostartfiles','--specs=nano.specs','--specs=nosys.specs',f'-T{linker}','-Wl,--gc-sections',f'-Wl,-Map={output / "mini-fido2.map"}',*[str(p) for p in objects],'-o',str(elf)],check=True,cwd=root)
binroot=gcc.parent
for format,ext in [('ihex','hex'),('binary','bin')]:
    subprocess.run([str(binroot/'riscv-none-elf-objcopy.exe'),'-O',format,str(elf),str(output/f'mini-app.{ext}')],check=True)
boot_linker=output/'recovery.ld'
boot_linker.write_text((sdk/'Ld/Link.ld').read_text().replace('LENGTH = 62K','LENGTH = 8K'))
boot_sources=[src/'recovery.c',src/'startup.S',src/'system_ch32x035.c',*[sdk/'Peripheral/src'/f'ch32x035_{name}.c' for name in ['rcc','gpio','adc']]]
boot_objects=[]
for i,p in enumerate(boot_sources):
    obj=output/f'boot-{i}-{p.stem}.o';boot_objects.append(obj)
    subprocess.run([str(gcc),*base,'-c',str(p),'-o',str(obj)],check=True,cwd=root)
boot_elf=output/'mini-recovery.elf'
subprocess.run([str(gcc),*base,'-nostartfiles','--specs=nano.specs','--specs=nosys.specs',f'-T{boot_linker}','-Wl,--gc-sections',*[str(p) for p in boot_objects],'-o',str(boot_elf)],check=True,cwd=root)
boot_bin=output/'mini-recovery.bin'
subprocess.run([str(binroot/'riscv-none-elf-objcopy.exe'),'-O','binary',str(boot_elf),str(boot_bin)],check=True)
boot=boot_bin.read_bytes();assert len(boot)<=8192
full=boot.ljust(8192,b'\xff')+(output/'mini-app.bin').read_bytes()
(output/'mini-fido2.bin').write_bytes(full)
records=[]
for address in range(0,len(full),16):
    data=full[address:address+16];record=bytes([len(data),address>>8,address&255,0])+data
    records.append(':'+record.hex().upper()+f'{(-sum(record))&255:02X}')
(output/'mini-fido2.hex').write_text('\n'.join(records+[':00000001FF'])+'\n',encoding='ascii')
size=subprocess.check_output([str(binroot/'riscv-none-elf-size.exe'),str(elf)],text=True)
size+=subprocess.check_output([str(binroot/'riscv-none-elf-size.exe'),str(boot_elf)],text=True)
print(size)
names=['mini-app.elf','mini-app.hex','mini-app.bin','mini-recovery.elf','mini-recovery.bin','mini-fido2.hex','mini-fido2.bin']
manifest={'verified_on_hardware':False,'size_output':size,'artifacts':{name:hashlib.sha256((output/name).read_bytes()).hexdigest() for name in names},'key_policy':'.env or MINI_MASTER_KEY_HEX -> ignored private/provision.c; retain same key for updates; never publish key-bearing images'}
(output/'build-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')

