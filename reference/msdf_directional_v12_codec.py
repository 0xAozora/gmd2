#!/usr/bin/env python3
"""Experimental GMD2 lossless MSDF RGB8 atlas codec.

Encoder/decoder use a MessagePack font layout (glyph bounds) and Zstd. The
atlas is strictly RGB8 (not RGBA). The decoder does NOT read the original RGB.
Compressed archive size excludes the external MessagePack layout.

Pixel encoding modes (fixed, one symbol per pixel):
 - mk: 0=R=G=B, 1=R exceptional, 2=G exceptional, 3=B exceptional,
       4=all distinct escape (three raw bytes)
 - shared: 0/1=two palette bytes, 2=Paeth exact, 3=left exact,
           4=above exact, 5=Paeth + unsigned modulo-256 residual
 - exceptional: 0/1=two palette bytes, 2=gradient exact, 3=left exact,
           4=vertical linear (2*above - above2) exact,
           5=gradient + unsigned modulo-256 residual

Symbol byte = mk + 5*shared_mode + 30*exception_mode.
Body = 254,016 symbol bytes, exceptional residuals, shared residuals,
       three-byte escapes. One Zstd frame compresses the body.
Header: <4sIIBB> magic=GMD2, atlas width,height, shared-channel-map,
         flags=0; followed by two shared and two exceptional palette bytes.

Standalone reference (apart from numpy, msgpack, zstd CLI).
Examples:
 python msdf_directional_v12_codec.py encode font.rgb font.msgp font.gmd2
 python msdf_directional_v12_codec.py decode font.gmd2 font.msgp restored.rgb
"""
from pathlib import Path
import argparse,struct,subprocess
import numpy as np,msgpack

H=struct.Struct('<4sIIBB')
MAGIC=b'GMD2'
CHMAP=18

def zstd(data,decompress=False,level=22):
 cmd=['zstd','-q','-d','-c'] if decompress else ['zstd','-q','--ultra',f'-{level}','-c']
 return subprocess.run(cmd,input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True).stdout

def paeth(a,b,c):
 p=a+b-c
 return a if abs(p-a)<=abs(p-b) and abs(p-a)<=abs(p-c) else b if abs(p-b)<=abs(p-c) else c

def segments(layout):
 meta=msgpack.unpackb(layout,raw=False)
 w=int(meta['a']['w']);h=int(meta['a']['h']);assert w>0 and h>0 and w*h<50_000_000
 idx=np.arange(w*h,dtype=np.int32).reshape(h,w)
 covered=np.zeros((h,w),bool);glyphs=[]
 for g in meta['g']:
  b=g['b'];gw=int(round(b['r']-b['l']));gh=int(round(b['t']-b['b']))
  if gw<=0 or gh<=0:continue
  x=int(np.floor(b['l']));y=h-int(np.ceil(b['t']))
  if x<0 or y<0 or x+gw>w or y+gh>h or covered[y:y+gh,x:x+gw].any():raise ValueError('bad glyph rectangle')
  covered[y:y+gh,x:x+gw]=True
  glyphs.append((int(g['i']),x,y,gw,gh))
 glyphs.sort(key=lambda g:(-g[3],g[4],g[0]))
 parts=[];segs=[];st=0
 for _,x,y,gw,gh in glyphs:
  parts.append(idx[y:y+gh,x:x+gw][::-1,:].ravel());segs.append((st,gw,gh));st+=gw*gh
 if st<w*h:
  parts.append(idx[~covered].ravel());segs.append((st,w*h-st,1))
 order=np.concatenate(parts)
 return w,h,order,segs

def channel_map(mapid):
 if mapid>=24:raise ValueError('bad map')
 return np.array([mapid//8,1+(mapid%8)//4,((mapid%4)//2)*2,mapid%2,1],np.uint8)

def neighbor_indices(w,h,order,segs):
 """Causal L/U/UL/UU neighbors. Outside region uses true atlas coordinates."""
 n=len(order);inv=np.empty(n,np.int32);inv[order]=np.arange(n,dtype=np.int32)
 result={}
 outside=segs[-1][0]
 for name,(dx,dy) in {'L':(-1,0),'U':(0,-1),'UL':(-1,-1),'UU':(0,-2)}.items():
  q=np.full(n,-1,np.int32)
  for st,gw,gh in segs[:-1]:
   a=q[st:st+gw*gh].reshape(gh,gw)
   b=np.arange(st,st+gw*gh,dtype=np.int32).reshape(gh,gw)
   x0=max(0,-dx);x1=min(gw,gw-dx);y0=max(0,-dy);y1=min(gh,gh-dy)
   if x0<x1 and y0<y1:a[y0:y1,x0:x1]=b[y0+dy:y1+dy,x0+dx:x1+dx]
  for i in range(outside,n):
   pos=int(order[i]);x=pos%w;y=pos//w;xx=x+dx;yy=y+dy
   if 0<=xx<w and 0<=yy<h:
    src=int(inv[yy*w+xx])
    if src<i:q[i]=src
  result[name]=q
 return result

def encode(raw,layout,level=22):
 w,h,order,segs=segments(layout)
 if len(raw)!=w*h*3:raise ValueError('bad raw RGB8 byte count')
 rgb=np.frombuffer(raw,np.uint8).reshape(-1,3)[order]
 r,g,b=rgb.T
 mk=np.where((r==g)&(g==b),0,np.where(g==b,1,np.where(r==b,2,np.where(r==g,3,4)))).astype(np.uint8)
 sh=np.where((mk==1)|(mk==4),g,r).astype(np.uint8)
 ex=np.where(mk==1,r,np.where(mk==2,g,b)).astype(np.uint8)
 bp=np.argsort(-np.bincount(sh[mk!=4],minlength=256))[:2].astype(np.uint8)
 ep=np.argsort(-np.bincount(ex[(mk>0)&(mk<4)],minlength=256))[:2].astype(np.uint8)
 ch=channel_map(CHMAP)[mk];ech=np.where(mk==1,0,np.where(mk==2,1,2)).astype(np.uint8)
 q=neighbor_indices(w,h,order,segs)
 def vals(k,c):
  ids=q[k];return np.where(ids>=0,rgb[np.maximum(ids,0),c],0).astype(np.int16)
 sl,su,sc=[vals(k,ch) for k in ('L','U','UL')]
 el,eu,ec,euu=[vals(k,ech) for k in ('L','U','UL','UU')]
 p=sl+su-sc
 sp=np.where((abs(p-sl)<=abs(p-su))&(abs(p-sl)<=abs(p-sc)),sl,np.where(abs(p-su)<=abs(p-sc),su,sc)).astype(np.uint8)
 eg=((el+eu-ec)&255).astype(np.uint8)
 evert=((2*eu-euu)&255).astype(np.uint8)
 bc=np.full(len(mk),255,np.uint8);exmode=np.full(len(mk),255,np.uint8)
 for i,value in enumerate(bp):bc[sh==value]=i
 for i,value in enumerate(ep):exmode[ex==value]=i
 for i,(test,name) in enumerate([(sp,'paeth'),(sl,'left'),(su,'above')]):
  bc[(bc==255)&(sh==test)]=i+2
 for i,test in enumerate((eg,el,evert)):
  exmode[(exmode==255)&(ex==test)]=i+2
 bc[bc==255]=5;exmode[exmode==255]=5
 bc[mk==4]=0;exmode[(mk==0)|(mk==4)]=0
 symbols=(mk.astype(np.uint16)+5*bc.astype(np.uint16)+30*exmode.astype(np.uint16)).astype(np.uint8)
 bm=(mk!=4)&(bc==5);em=(mk>0)&(mk<4)&(exmode==5)
 br=((sh[bm].astype(np.int16)-sp[bm].astype(np.int16))&255).astype(np.uint8)
 er=((ex[em].astype(np.int16)-eg[em].astype(np.int16))&255).astype(np.uint8)
 body=symbols.tobytes()+er.tobytes()+br.tobytes()+rgb[mk==4].tobytes()
 return H.pack(MAGIC,w,h,CHMAP,0)+bp.tobytes()+ep.tobytes()+zstd(body,level=level)

def decode(blob,layout):
 if len(blob)<H.size+4:raise ValueError('truncated archive')
 magic,w,h,mapid,flags=H.unpack_from(blob)
 if magic!=MAGIC or flags:raise ValueError('unsupported format')
 aw,ah,order,segs=segments(layout)
 if (w,h)!=(aw,ah):raise ValueError('mismatched layout')
 bp=blob[H.size:H.size+2];ep=blob[H.size+2:H.size+4]
 body=zstd(blob[H.size+4:],True)
 n=w*h
 if len(body)<n:raise ValueError('truncated symbols')
 sym=np.frombuffer(body[:n],np.uint8)
 mk=sym%5;bc=(sym//5)%6;excls=sym//30
 if np.any((mk==4)&((bc!=0)|(excls!=0))) or np.any((mk==0)&(excls!=0)) or np.any(excls>5):raise ValueError('invalid symbols')
 bm=(mk!=4)&(bc==5);em=(mk>0)&(mk<4)&(excls==5)
 nb=int(bm.sum());ne=int(em.sum());nx=int((mk==4).sum())
 if len(body)!=n+ne+nb+nx*3:raise ValueError('incorrect data length')
 er=body[n:n+ne];br=body[n+ne:n+ne+nb];esc=np.frombuffer(body[n+ne+nb:],np.uint8).reshape(-1,3)
 q=neighbor_indices(w,h,order,segs)
 chmap=channel_map(mapid)
 rgb=np.zeros((n,3),np.uint8)
 ib=ie=ix=0
 for i in range(n):
  mode=int(mk[i]);s=int(bc[i]);exmode=int(excls[i]);pos=int(order[i])
  if mode==4:
   rgb[i]=esc[ix];ix+=1;continue
  def prev(which,channel):
   j=int(q[which][i]);return int(rgb[j,channel]) if j>=0 else 0
  ch=int(chmap[mode])
  sl,su,sc=prev('L',ch),prev('U',ch),prev('UL',ch)
  p=paeth(sl,su,sc)
  if s<2:sh=int(bp[s])
  elif s==2:sh=p
  elif s==3:sh=sl
  elif s==4:sh=su
  elif s==5:sh=(p+br[ib])&255;ib+=1
  rgb[i]=sh
  if mode:
   ch=mode-1
   el,eu,ec,eu2=prev('L',ch),prev('U',ch),prev('UL',ch),prev('UU',ch)
   grad=(el+eu-ec)&255
   if exmode<2:unusual=int(ep[exmode])
   elif exmode==2:unusual=grad
   elif exmode==3:unusual=el
   elif exmode==4:unusual=(2*eu-eu2)&255
   elif exmode==5:unusual=(grad+er[ie])&255;ie+=1
   rgb[i,ch]=unusual
 if (ib,ie,ix)!=(nb,ne,nx):raise ValueError('incomplete decode')
 original=np.empty_like(rgb);original[order]=rgb
 return original.tobytes()

if __name__=='__main__':
 a=argparse.ArgumentParser()
 a.add_argument('mode',choices=('encode','decode'));a.add_argument('source');a.add_argument('layout');a.add_argument('output');args=a.parse_args()
 data=Path(args.source).read_bytes();lay=Path(args.layout).read_bytes()
 result=encode(data,lay) if args.mode=='encode' else decode(data,lay)
 Path(args.output).write_bytes(result)
 print(args.mode,len(result),'bytes')
