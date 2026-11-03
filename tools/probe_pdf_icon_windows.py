#!/usr/bin/env python3
"""Windows-only resource probe: app icon 1 stays blue; PDF icon group 2 exists.

Checks the actual packaged LUNA.exe in a data-only resource mapping. No user
registry or PDF is opened. An ICO sidecar must NOT be distributed because the
already-published LUNA v256 updater will reject that file extension.
"""
from __future__ import annotations
import ctypes
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
        result={}
        for resource_id in (1,2):
            group=kernel.FindResourceW(mapped,ctypes.c_void_p(resource_id),
                                       ctypes.c_void_p(14)) # RT_GROUP_ICON
            if not group:
                raise AssertionError(f"Missing icon group ID {resource_id}")
            count=kernel.SizeofResource(mapped,group)
            h=kernel.LoadResource(mapped,group)
            pointer=kernel.LockResource(h) if h else None
            if not pointer or count<6:
                raise AssertionError(f"Invalid ICO resource ID {resource_id}")
            data=ctypes.string_at(pointer,count)
            reserved,kind,n=struct.unpack_from("<HHH",data)
            if reserved or kind!=1 or n<1 or count<6+14*n:
                raise AssertionError(f"Corrupt icon group {resource_id}")
            result[resource_id]=n
        if result[2]<7:
            raise AssertionError("PDF icon missing 16/24/32/48/64/128/256 sizes")
        print(f"[PASS] LUNA.exe icon resource 1=blue app ({result[1]} sizes), "
              f"2=red PDF document ({result[2]} sizes); no external ICO")
        return result
    finally:
        kernel.FreeLibrary(mapped)


if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: python probe_pdf_icon_windows.py path/to/LUNA.exe")
    probe(Path(sys.argv[1]).resolve())
