#!/usr/bin/env python3
"""Create LUNA-PDF.ico (document/crescent/PDF), deterministic, pure stdlib.

Windows EXE resources deliberately remain the original BLUE LUNA-Moon.ico.
This red icon is only for the PDF ProgID's DefaultIcon and travels inside
the portable release; no GUI, graphics packages or personal data needed.
"""
from __future__ import annotations
import argparse
import binascii
import hashlib
from pathlib import Path
import struct
import zlib

SIZES=(16,24,32,48,64,128,256)
# 5x7 pixel-letter forms, enlarged to remain readable in Windows Explorer.
FONT={
    "P": ("11110","10001","10001","11110","10000","10000","10000"),
    "D": ("11110","10001","10001","10001","10001","10001","11110"),
    "F": ("11111","10000","10000","11110","10000","10000","10000"),
}

def art(n: int)->bytes:
    """RGBA icon. Simple vector-like shapes with 2x supersampling at 16px."""
    k=2 if n<64 else 1
    w=n*k
    pixels=bytearray(w*w*4)
    def dot(x:int,y:int,r:int,g:int,b:int,a:int=255)->None:
        if not (0<=x<w and 0<=y<w):return
        p=(y*w+x)*4
        pixels[p:p+4]=bytes((r,g,b,a))
    def inside_round(x,y,left,top,right,bottom,radius):
        if not (left<=x<right and top<=y<bottom):return False
        cx=min(max(x,left+radius),right-radius)
        cy=min(max(y,top+radius),bottom-radius)
        return (x-cx)**2+(y-cy)**2<=radius**2
    for y in range(w):
        for x in range(w):
            u,v=(x+.5)/w,(y+.5)/w
            if not inside_round(u,v,.11,.065,.89,.93,.088):continue
            # folded white document and subtle edge
            edge=not inside_round(u,v,.121,.076,.879,.918,.077)
            dot(x,y,226 if edge else 255,129 if edge else 251,143 if edge else 251)
            # top-right folded corner
            if u>.65 and v<.24 and (u-.65)+(v-.065)*.42>.084:
                dot(x,y,231,54,72)
            # crescent moon: red disk with white shifted inner cutout
            outer=(u-.49)**2+((v-.469)*1.04)**2<.245**2
            carve=(u-.592)**2+((v-.364)*1.04)**2<.229**2
            if outer and not carve:
                dot(x,y,213 if u<.43 else 237,26 if u<.43 else 48,51 if u<.43 else 67)
    # PDF label positioned in lower paper half at variable pixel-grid scale.
    scale=max(1,int(w*.032))
    glyph_width=5*scale
    advance=6*scale
    total=advance*3-scale
    left=(w-total)//2
    top=int(w*.69)
    for i,letter in enumerate("PDF"):
        for row,pattern in enumerate(FONT[letter]):
            for col,v in enumerate(pattern):
                if v!="1":continue
                for dy in range(scale):
                    for dx in range(scale):
                        dot(left+i*advance+col*scale+dx,
                            top+row*scale+dy,174,22,42)
    if k==1:return bytes(pixels)
    # Standard 2x2 premultiplied-alpha downscale, preserving transparency.
    result=bytearray(n*n*4)
    for y in range(n):
        for x in range(n):
            samples=[pixels[((2*y+dy)*w+2*x+dx)*4:((2*y+dy)*w+2*x+dx)*4+4]
                     for dy in (0,1) for dx in (0,1)]
            a=sum(p[3] for p in samples)
            q=(y*n+x)*4
            result[q+3]=a//4
            for ch in range(3):
                result[q+ch]=round(sum(p[ch]*p[3] for p in samples)/a) if a else 0
    return bytes(result)

def png(n:int,rgba:bytes)->bytes:
    signature=b"\x89PNG\r\n\x1a\n"
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",binascii.crc32(kind+data)&0xffffffff)
    stride=n*4
    scan=b"".join(b"\x00"+rgba[y*stride:(y+1)*stride] for y in range(n))
    return(signature+
           chunk(b"IHDR",struct.pack(">IIBBBBB",n,n,8,6,0,0,0))+
           chunk(b"IDAT",zlib.compress(scan,9))+
           chunk(b"IEND",b""))

def make_ico()->bytes:
    images=[png(size,art(size)) for size in SIZES]
    header=struct.pack("<HHH",0,1,len(images))
    offset=len(header)+16*len(images)
    entries=[]
    for size,image in zip(SIZES,images):
        entries.append(struct.pack("<BBBBHHII",size if size<256 else 0,
            size if size<256 else 0,0,0,1,32,len(image),offset))
        offset+=len(image)
    return header+b"".join(entries)+b"".join(images)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    data=make_ico()
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_bytes(data)
    print(f"LUNA PDF association icon: {len(data)} bytes, SHA256={hashlib.sha256(data).hexdigest()}")

if __name__=="__main__":
    main()
