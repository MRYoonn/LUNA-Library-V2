#!/usr/bin/env python3
"""Windows-only resource probe: app icon 1 stays blue; PDF icon group 2 exists.

Checks the actual packaged LUNA.exe in a data-only resource mapping. No user
registry or PDF is opened. An ICO sidecar must NOT be distributed because the
already-published LUNA v256 updater will reject that file extension.
"""
from __future__ import annotations
import ctypes
import hashlib
from pathlib import Path
import struct
import sys


def probe(exe: Path):
    if sys.platform != "win32":
        raise RuntimeError("Windows executable-resource probe requires Windows")
    root=exe.parent
    if exe.name!="LUNA.exe" or not exe.is_file():
        raise ValueError("Expected exact packaged LUNA.exe")
    if (root/"LUNA-PDF.ico").exists():
        raise ValueError("Standalone icon would break existing v256 updater")
    kernel=ctypes.WinDLL("kernel32",use_last_error=True)
    kernel.LoadLibraryExW.argtypes=[ctypes.c_wchar_p,ctypes.c_void_p,ctypes.c_ulong]
    kernel.LoadLibraryExW.restype=ctypes.c_void_p
    kernel.FindResourceW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p]
    kernel.FindResourceW.restype=ctypes.c_void_p
    kernel.SizeofResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
    kernel.SizeofResource.restype=ctypes.c_ulong
    kernel.LoadResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
    kernel.LoadResource.restype=ctypes.c_void_p
    kernel.LockResource.argtypes=[ctypes.c_void_p]
    kernel.LockResource.restype=ctypes.c_void_p
    kernel.FreeLibrary.argtypes=[ctypes.c_void_p]
    kernel.FreeLibrary.restype=ctypes.c_int
    mapped=kernel.LoadLibraryExW(str(exe),None,0x00000002)  # DATAFILE, do not run code
    if not mapped:
        raise OSError(ctypes.get_last_error(),"Cannot load LUNA icon resources")
    try:
        def get_resource(kind, identifier):
            handle=kernel.FindResourceW(mapped,ctypes.c_void_p(identifier),
                                         ctypes.c_void_p(kind))
            if not handle:
                raise AssertionError(f"Missing resource kind {kind} id {identifier}")
            length=kernel.SizeofResource(mapped,handle)
            block=kernel.LoadResource(mapped,handle)
            pointer=kernel.LockResource(block) if block else None
            if not pointer or not length:
                raise AssertionError(f"Empty resource kind {kind} id {identifier}")
            return ctypes.string_at(pointer,length)

        result={}
        for resource_id in (1,2):
            data=get_resource(14,resource_id)  # RT_GROUP_ICON
            if len(data)<6:
                raise AssertionError(f"Invalid ICO resource ID {resource_id}")
            reserved,kind,n=struct.unpack_from("<HHH",data)
            if reserved or kind!=1 or n<1 or len(data)<6+14*n:
                raise AssertionError(f"Corrupt icon group {resource_id}")
            result[resource_id]=n
            if resource_id==2:
                # The Windows resource compiler may normalize GROUP_ICON
                # planes/offsets; these are metadata, not original artwork.
                # Require every actual RT_ICON image byte to match the ICO's
                # original PNG image for the same size, losslessly.
                approved=(Path(__file__).resolve().parents[1]/
                          "assets/icons/LUNA-PDF-approved.ico").read_bytes()
                if len(approved)!=105070 or hashlib.sha256(approved).hexdigest()!=(
                    "7f89df11a934690f3a6bae8a9b014efdd9f73a3607f3b699e60b44b185349f0f"):
                    raise AssertionError("Approved PDF icon input hash changed")
                _,source_kind,source_n=struct.unpack_from("<HHH",approved)
                if source_kind!=1 or source_n!=n:
                    raise AssertionError("Original ICO directory size changed")
                source_images={}
                for index in range(n):
                    w,h,_,_,_,_,length,offset=struct.unpack_from(
                        "<BBBBHHII",approved,6+index*16)
                    image=approved[offset:offset+length]
                    if len(image)!=length:
                        raise AssertionError("Original ICO PNG image is truncated")
                    source_images[(w or 256,h or 256)]=image
                dimensions=set()
                for index in range(n):
                    w,h,_,_,_,_,size,icon_id=struct.unpack_from(
                        "<BBBBHHIH",data,6+index*14)
                    image=get_resource(3,icon_id)  # actual compiled RT_ICON
                    dim=(w or 256,h or 256)
                    if len(image)!=size or source_images.get(dim)!=image:
                        raise AssertionError(
                            "Approved red PDF icon differs from embedded Windows resource ID 2")
                    dimensions.add(dim[0])
                if dimensions!={16,24,32,48,64,128,256}:
                    raise AssertionError("Compiled PDF icon resolution set is incomplete")
        if result[2]<7:
            raise AssertionError("PDF icon missing 16/24/32/48/64/128/256 sizes")
        print(f"[PASS] LUNA.exe icon resource 1=blue app ({result[1]} sizes), "
              f"2=exact approved red PDF document ({result[2]} sizes); no external ICO")
        return result
    finally:
        kernel.FreeLibrary(mapped)


if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: python probe_pdf_icon_windows.py path/to/LUNA.exe")
    probe(Path(sys.argv[1]).resolve())
