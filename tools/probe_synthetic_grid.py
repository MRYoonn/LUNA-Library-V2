"""Public synthetic two-page PDF for ICC Gray vs DeviceGray vector-stroke checks.

Created independently with a synthetic LittleCMS Gray gamma 2.2 profile; no
user PDF or private contents are copied into this test.
"""
from __future__ import annotations

import base64
from pathlib import Path

ICC_GRAY_PROFILE = base64.b64decode(
    "AAABPGxjbXMEQAAAbW50ckdSQVlYWVogB+oACwAKAAsAJgAoYWNzcEFQUEwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPbWAAEAAAAA0y1sY21zAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADZGVzYwAAAKgAAAA2Y3BydAAAAOAAAABMa1RSQwAAASwAAAAQbWx1YwAAAAAAAAABAAAADGVuVVMAAAAaAAAAHABnAHIAYQB5ACAAYgB1AGkAbAB0AC0AaQBuAABtbHVjAAAAAAAAAAEAAAAMZW5VUwAAADAAAAAcAE4AbwAgAGMAbwBwAHkAcgBpAGcAaAB0ACwAIAB1AHMAZQAgAGYAcgBlAGUAbAB5cGFyYQAAAAAAAAAAAAIzMw=="
)


def build() -> bytes:
    assert len(ICC_GRAY_PROFILE) == 316 and ICC_GRAY_PROFILE[16:20] == b"GRAY"
    commands = [b"q 1 1 1 rg 0 0 300 220 re f Q\n"]
    for operation, start in ((b"/Cs1 CS 0.651 SC", 45), (b"0.651 G", 165)):
        commands.append(b"q " + operation + b" 0.36 w\n")
        for x in range(start, start + 81, 20):
            commands.append(f"{x} 35 m {x} 115 l S\n".encode())
        for y in (35, 55, 75, 95, 115):
            commands.append(f"{start} {y} m {start+80} {y} l S\n".encode())
        commands.append(b"1.079 w\n")
        commands.append(f"{start} 75 m {start+80} 75 l S\n".encode())
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
        b"<< /N 1 /Alternate /DeviceGray /Length " + str(len(ICC_GRAY_PROFILE)).encode() + b" >>\nstream\n" + ICC_GRAY_PROFILE + b"\nendstream",
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
