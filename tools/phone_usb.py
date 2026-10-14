"""Read-only USB file export from /hackerphone on the phone's SD card."""
import argparse,pathlib,re,time
import serial

class PhoneLink:
    def __init__(self,port):
        self.serial=serial.Serial();self.serial.port=port;self.serial.baudrate=115200
        self.serial.timeout=.2;self.serial.dtr=False;self.serial.rts=False;self.serial.open()
    def close(self): self.serial.close()
    def command(self,text,prefix,timeout=5):
        self.serial.reset_input_buffer();self.serial.write(text.encode('ascii')+b'\n')
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            line=self.serial.readline().strip()
            if line.startswith(prefix):return line
            if line.startswith((b'HP_ERROR',b'HP_FILE_ERROR')):raise RuntimeError(line.decode())
        raise TimeoutError(text)
    def exact(self,n,timeout=5):
        end=time.monotonic()+timeout;data=bytearray()
        while len(data)<n and time.monotonic()<end:data.extend(self.serial.read(n-len(data)))
        if len(data)!=n:raise TimeoutError('Incomplete USB binary transfer')
        return bytes(data)
    def file_bytes(self,name):
        if not re.fullmatch(r'[A-Za-z0-9_.\-/]{1,120}',name) or any(p in ('','.','..') for p in name.split('/')):
            raise ValueError('Use a relative path below /hackerphone')
        header=self.command('FILEINFO '+name,b'HP_FILEINFO ');size=int(header.split()[1]);result=bytearray()
        while len(result)<size:
            header=self.command(f'FILECHUNK {name} {len(result)}',b'HP_CHUNK ')
            _,offset,length=header.split();offset,length=int(offset),int(length)
            if offset!=len(result) or not 0<length<=min(1024,size-offset):raise ValueError('Invalid chunk header')
            result.extend(self.exact(length))
            if self.exact(len(b'\nHP_CHUNK_END\r\n'))!=b'\nHP_CHUNK_END\r\n':raise ValueError('Invalid chunk trailer')
        return bytes(result)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True)
    p.add_argument('file',help='Relative SD path, e.g. capture-00001.pcap');p.add_argument('output',type=pathlib.Path)
    a=p.parse_args()
    if a.output.exists():p.error('Output already exists; choose a new filename')
    phone=PhoneLink(a.port)
    try:data=phone.file_bytes(a.file)
    finally:phone.close()
    with a.output.open('xb') as out:out.write(data)
    print(f'Saved {len(data)} bytes to {a.output}')
