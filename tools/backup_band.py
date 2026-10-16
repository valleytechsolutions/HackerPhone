"""Read-only, identity-checked Maker Band recovery backup (never writes flash).

Use the project Python with esptool installed. Requires the expected MAC obtained
from USB/ROM identification. Originals and diagnostics remain in ignored .work/.
The ROM reader avoids the stub read failure observed on the engineering C3.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--expected-mac',required=True)
    args=parser.parse_args()
    if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}',args.expected_mac):
        parser.error('--expected-mac must be six colon-separated hex bytes')
    root=pathlib.Path(__file__).resolve().parents[1]
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    destination=root/'.work/recovery/hackerband'/stamp
    destination.mkdir(parents=True,exist_ok=False)
    base=[sys.executable,'-m','esptool','--chip','esp32c3','--port',args.port,'--no-stub','--after','no-reset']
    def run(extra,name):
        result=subprocess.run(base+extra,capture_output=True,text=True)
        (destination/name).write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'esptool failed; private diagnostic: {destination/name}')
        return result.stdout
    identity=run(['chip-id'],'identity.log')
    if args.expected_mac.lower() not in identity.lower() or 'ESP32-C3' not in identity:
        raise RuntimeError('Connected identity does not match; no backup or flash write attempted')
    # The first command leaves the same device in its ROM loader.
    base+=['--before','no-reset']
    flash=run(['flash-id'],'flash-id.log')
    if 'Detected flash size: 4MB' not in flash:
        raise RuntimeError('Expected 4 MB flash; inspect hardware before continuing')
    print('Identity verified. Reading 4 MB twice with the ROM loader; flash is not modified.',flush=True)
    paths=[destination/'original-flash.bin',destination/'verification-read.bin']
    hashes=[]
    for i,path in enumerate(paths):
        run(['read-flash','0','0x400000',str(path)],f'read-{i+1}.log')
        data=path.read_bytes()
        if len(data)!=4194304:raise RuntimeError('Incomplete flash read; backup is not verified')
        hashes.append(hashlib.sha256(data).hexdigest())
    if hashes[0]!=hashes[1]:raise RuntimeError('Independent reads differ; backup is not verified')
    data=paths[0].read_bytes();partitions=[]
    for off in range(0x8000,0x8c00,32):
        if data[off:off+2]!=b'\xaa\x50':break
        _,kind,sub,address,size,label,flags=struct.unpack('<HBBII16sI',data[off:off+32])
        partitions.append(dict(type=kind,subtype=sub,offset=hex(address),size=hex(size),label=label.split(b'\0')[0].decode('ascii',errors='replace'),flags=flags))
    info=dict(chip='ESP32-C3',mac=args.expected_mac.lower(),bytes=len(data),sha256=hashes[0],verified_reads=2,partitions=partitions)
    (destination/'backup-info.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
    print(f'Verified recovery saved privately: {destination}')
    print('Device remains in its ROM loader. Reset/power-cycle it to boot the unchanged original firmware.')

if __name__=='__main__':
    try:main()
    except (RuntimeError,OSError) as error:
        print(f'Backup incomplete: {error}',file=sys.stderr);sys.exit(1)
