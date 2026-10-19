"""Native Windows serial diagnostics for the temporary Crystal firmware.

Examples: console.py --port COM3 status | console.py --port COM3 shot captures/out.png
Binary screenshot reads use the announced byte count, not newline parsing.
"""
import argparse,time,sys
from pathlib import Path
import serial

def connect(port):
    p=serial.Serial()
    p.port=port;p.baudrate=115200;p.timeout=.2;p.write_timeout=2
    p.dtr=False;p.rts=False;p.open()
    return p
def collect(p,seconds):
    end=time.monotonic()+seconds;buf=bytearray()
    while time.monotonic()<end:
        buf.extend(p.read(p.in_waiting or 1))
    return bytes(buf)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--port',required=True)
    ap.add_argument('--wait',type=float,default=1.5)
    ap.add_argument('command',nargs='+');args=ap.parse_args()
    with connect(args.port) as p:
        pre=collect(p,.3)
        if args.command[0]=='listen':
            sys.stdout.write((pre+collect(p,args.wait)).decode('utf-8',errors='replace'));return
        cmd=' '.join(args.command)
        target=None
        if args.command[0]=='shot':
            target=Path(args.command[1]);cmd='shot'
        p.write((cmd+'\r').encode())
        if target:
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                line=p.readline()
                if line.startswith(b'FRAME '):break
            else:raise RuntimeError('No screenshot header received')
            _,w,h,n=line.split();w,h,n=map(int,(w,h,n));data=bytearray()
            while len(data)<n and time.monotonic()<deadline:
                data.extend(p.read(n-len(data)))
            if len(data)!=n:raise RuntimeError(f'Incomplete screenshot {len(data)}/{n}')
            from PIL import Image
            rgb=bytearray()
            for i in range(0,n,2):
                c=data[i]|data[i+1]<<8
                rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
            target.parent.mkdir(parents=True,exist_ok=True)
            Image.frombytes('RGB',(w,h),bytes(rgb)).save(target)
            print(f'Saved {w}x{h} screenshot to {target}')
        else:sys.stdout.write(collect(p,args.wait).decode('utf-8',errors='replace'))
if __name__=='__main__':main()
