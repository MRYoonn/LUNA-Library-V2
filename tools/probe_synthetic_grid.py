"""Public synthetic two-page PDF for ICC Gray vs DeviceGray vector-stroke checks.

Created independently with a synthetic LittleCMS Gray gamma 2.2 profile; no
user PDF or private contents are copied into this test.
"""
from __future__ import annotations

import base64
import struct
from pathlib import Path

ICC_GRAY_PROFILE = base64.b64decode(
    "AAABPGxjbXMEQAAAbW50ckdSQVlYWVogB+oACwAKAAsAJgAoYWNzcEFQUEwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPbWAAEAAAAA0y1sY21zAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADZGVzYwAAAKgAAAA2Y3BydAAAAOAAAABMa1RSQwAAASwAAAAQbWx1YwAAAAAAAAABAAAADGVuVVMAAAAaAAAAHABnAHIAYQB5ACAAYgB1AGkAbAB0AC0AaQBuAABtbHVjAAAAAAAAAAEAAAAMZW5VUwAAADAAAAAcAE4AbwAgAGMAbwBwAHkAcgBpAGcAaAB0ACwAIAB1AHMAZQAgAGYAcgBlAGUAbAB5cGFyYQAAAAAAAAAAAAIzMw=="
)


def long_synthetic_gray_profile() -> bytes:
    """1024-sample gamma curve created from formula; no Apple/user data."""
    data = bytearray(ICC_GRAY_PROFILE)
    trc = b"curv" + bytes(4) + struct.pack(">I", 1024)
    trc += struct.pack(">1024H", *[
        round(65535.0 * (i / 1023.0) ** 2.2) for i in range(1024)
    ])
    tags = struct.unpack_from(">I", data, 128)[0]
    for n in range(tags):
        pos = 132 + n * 12
        if data[pos:pos + 4] == b"kTRC":
            struct.pack_into(">II", data, pos + 4, len(data), len(trc))
            break
    else:
        raise ValueError("Missing synthetic Gray profile TRC")
    data.extend(trc)
    struct.pack_into(">I", data, 0, len(data))
    return bytes(data)


def build(*, long_icc=False, nested_clip=False) -> bytes:
    profile = long_synthetic_gray_profile() if long_icc else ICC_GRAY_PROFILE
    assert len(profile) >= 316 and profile[16:20] == b"GRAY"
    commands = [b"q 1 1 1 rg 0 0 300 220 re f Q\n"]
    for operation, start in ((b"/Cs1 CS 0.651 SC", 45), (b"0.651 G", 165)):
        if nested_clip:
            commands.append(b"q -0.08537946 0 300 220 re W n\n")
        commands.append(b"q " + operation + b" 0.36 w\n")
        for x in range(start, start + 81, 20):
            if nested_clip:
                commands.append((
                    f"q 1.028571 0 0 1.028571 -0.08537946 0 cm "
                    f"{x/1.028571:.4f} {35/1.028571:.4f} m "
                    f"{x/1.028571:.4f} {115/1.028571:.4f} l S Q\n"
                ).encode())
            else:
                commands.append(f"{x} 35 m {x} 115 l S\n".encode())
        for y in (35, 55, 75, 95, 115):
            if nested_clip:
                commands.append((
                    f"q 1.028571 0 0 1.028571 -0.08537946 0 cm "
                    f"{start/1.028571:.4f} {y/1.028571:.4f} m "
                    f"{(start+80)/1.028571:.4f} {y/1.028571:.4f} l S Q\n"
                ).encode())
            else:
                commands.append(f"{start} {y} m {start+80} {y} l S\n".encode())
        commands.append(b"1.079 w\n")
        commands.append(f"{start} 75 m {start+80} 75 l S\n".encode())
        commands.append(b"Q\n")
        if nested_clip:
            commands.append(b"Q\n")
    commands.extend([
        b"0 g BT /F1 14 Tf 40 165 Td (LUNA synthetic smoke test) Tj ET\n",
        b"0 g BT /F1 9 Tf 45 125 Td (ICC Gray) Tj ET\n",
        b"0 g BT /F1 9 Tf 165 125 Td (Device Gray) Tj ET\n",
        b"0 g BT /F1 8 Tf 45 20 Td (SYNTHETIC - NO USER DATA) Tj ET\n",
    ])
    first = b"".join(commands)
    second = b"BT /F1 14 Tf 40 165 Td (Second synthetic page) Tj ET\n"

    def stream(data: bytes) -> bytes:
        return b"<< /Length " + str(len(data)).encode() + b" >>\nstream\n" + data + b"\nendstream"

    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] /Resources << /Font << /F1 4 0 R >> /ColorSpace << /Cs1 [ /ICCBased 8 0 R ] >> >> /Contents 5 0 R >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        stream(first),
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] /Resources << /Font << /F1 4 0 R >> >> /Contents 7 0 R >>",
        stream(second),
        b"<< /N 1 /Alternate /DeviceGray /Length " + str(len(profile)).encode() + b" >>\nstream\n" + profile + b"\nendstream",
    ]
    pdf = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for n, obj in enumerate(objects, 1):
        offsets.append(len(pdf))
        pdf.extend(f"{n} 0 obj\n".encode() + obj + b"\nendobj\n")
    startxref = len(pdf)
    pdf.extend(f"xref\n0 {len(objects)+1}\n0000000000 65535 f \n".encode())
    for pos in offsets:
        pdf.extend(f"{pos:010d} 00000 n \n".encode())
    pdf.extend(f"trailer\n<< /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{startxref}\n%%EOF\n".encode())
    return bytes(pdf)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    args.path.write_bytes(build())
