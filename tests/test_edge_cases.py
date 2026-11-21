#!/usr/bin/env python3
"""Small randomized/malformed-archive checks for the independent C example."""
from pathlib import Path
import subprocess, tempfile, random, hashlib
import msgpack
root=Path(__file__).resolve().parents[1]
tool=root/'gmd2tool'
rng=random.Random(0x6d736466)

def run(*args, succeed=True):
    p=subprocess.run([str(tool),*map(str,args)],capture_output=True)
    if (p.returncode==0)!=succeed:
        raise AssertionError(f'command {args} returned {p.returncode}: {p.stderr.decode()}')

def layout(w,h,rects):
    glyphs=[]
    for i,(x,y,gw,gh) in enumerate(rects):
        # Atlas metadata uses bottom y-origin.
        b={'l':float(x)+.5,'r':float(x+gw)+.5,
           'b':float(h-y-gh)-.5,'t':float(h-y)-.5}
        glyphs.append({'i':rng.randrange(1,10000),'a':0.,
                       'p':{'l':0.,'r':0.,'b':0.,'t':0.},'b':b})
    return msgpack.packb({'a':{'w':w,'h':h,'y':'bottom'},'g':glyphs,
                          'm':{},'n':'Test','k':[]},use_bin_type=True)

with tempfile.TemporaryDirectory() as temp:
    d=Path(temp)
    trials=[(7,5,[]),(32,16,[(2,3,9,7)]),(20,25,[(1,1,6,8),(9,9,9,6)])]
    for z in range(20):
        w=rng.randrange(4,50);h=rng.randrange(4,50)
        rects=[] if z%2==0 else [(0,0,w//2,h//2)]
        trials.append((w,h,rects))
    for z,(w,h,rects) in enumerate(trials):
        pixels=bytes(rng.randrange(256) for _ in range(w*h*3))
        if z==1:pixels=b'\0'*(w*h*3)
        if z==2:pixels=(bytes([0,255,127])*(w*h))
        (d/'src.rgb').write_bytes(pixels);(d/'layout.msgp').write_bytes(layout(w,h,rects))
        run('encode',d/'src.rgb',d/'layout.msgp',d/'archive.gmd2','3')
        run('decode',d/'archive.gmd2',d/'layout.msgp',d/'dst.rgb')
        assert (d/'dst.rgb').read_bytes()==pixels,(w,h,rects)
    # Tamper with the GMD2 header and ensure the decoder rejects it.
    b=bytearray((d/'archive.gmd2').read_bytes());b[13]=1
    (d/'bad.gmd2').write_bytes(b)
    run('decode',d/'bad.gmd2',d/'layout.msgp',d/'dst.rgb',succeed=False)
print(f'OK: {len(trials)} varied lossless C roundtrips and malformed header rejection')
