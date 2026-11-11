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


def build(*, long_icc=False, nested_clip=False, clip_only=False, ctm_only=False) -> bytes:
    profile = long_synthetic_gray_profile() if long_icc else ICC_GRAY_PROFILE
    assert len(profile) >= 316 and profile[16:20] == b"GRAY"
    use_clip = nested_clip or clip_only
    use_ctm = nested_clip or ctm_only
    commands = [b"q 1 1 1 rg 0 0 300 220 re f Q\n"]
    for operation, start in ((b"/Cs1 CS 0.651 SC", 45), (b"0.651 G", 165)):
        if use_clip:
            commands.append(b"q -0.08537946 0 300 220 re W n\n")
        commands.append(b"q " + operation + b" 0.36 w\n")
        for x in range(start, start + 81, 20):
            if use_ctm:
                commands.append((
                    f"q 1.028571 0 0 1.028571 -0.08537946 0 cm "
                    f"{x/1.028571:.4f} {35/1.028571:.4f} m "
                    f"{x/1.028571:.4f} {115/1.028571:.4f} l S Q\n"
                ).encode())
            else:
                commands.append(f"{x} 35 m {x} 115 l S\n".encode())
        for y in (35, 55, 75, 95, 115):
            if use_ctm:
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
        if use_clip:
            commands.append(b"Q\n")
    commands.extend([
        b"0 g BT /F1 14 Tf 40 165 Td (LUNA synthetic smoke test) Tj ET\n",
        b"0 g BT /F1 9 Tf 45 125 Td (ICC Gray) Tj ET\n",
        b"0 g BT /F1 9 Tf 165 125 Td (Device Gray) Tj ET\n",
        b"0 g BT /F1 8 Tf 45 20 Td (SYNTHETIC - NO USER DATA) Tj ET\n",
    ])
    first = b"".join(commands)
    return assemble(first, profile)


def assemble(first: bytes, profile: bytes, resources: bytes = b'') -> bytes:
    second = b"BT /F1 14 Tf 40 165 Td (Second synthetic page) Tj ET\n"

    def stream(data: bytes) -> bytes:
        return b"<< /Length " + str(len(data)).encode() + b" >>\nstream\n" + data + b"\nendstream"

    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] /Resources << /Font << /F1 4 0 R >> /ColorSpace << /Cs1 [ /ICCBased 8 0 R ] >> " + resources + b">> /Contents 5 0 R >>" if resources else b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 220] /Resources << /Font << /F1 4 0 R >> /ColorSpace << /Cs1 [ /ICCBased 8 0 R ] >> >> /Contents 5 0 R >>",
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


EXTENDED_CASES = ('nested-tight', 'hole-evenodd', 'triangle', 'disjoint-clips',
                  'saved-restore', 'alpha-strokes')


def extended(case):
    """Real intersections, nonrectangular clips, empty clips and state restore.

    These extend the original six fixtures without changing any of their bytes.
    All geometry and colors are independently generated; no private PDF data.
    """
    import re
    baseline = build(nested_clip=case == 'nested-tight')
    # Extract only our own generated, uncompressed content stream.
    body = re.search(rb'5 0 obj\n<< /Length \d+ >>\nstream\n(.*?)\nendstream', baseline, re.S).group(1)
    resources = b''
    if case == 'nested-tight':
        # The original case's q/clip around each grid now intersects two more
        # clips in the same graphics-state ancestry, including its CTM.
        clip = b'35 25 230 110 re W n 55 45 180 60 re W n\n'
    elif case == 'hole-evenodd':
        clip = b'0 0 300 220 re 80 55 140 40 re W* n\n'
    elif case == 'triangle':
        clip = b'35 25 m 270 25 l 150 135 l h W n\n'
    elif case == 'disjoint-clips':
        clip = b'0 0 140 220 re W n 160 0 140 220 re W n\n'
    elif case == 'saved-restore':
        clip = b'55 45 180 60 re W n\n'
    elif case == 'alpha-strokes':
        clip = b'0 0 300 220 re 290 210 m 295 210 l 295 215 l h W* n /GS1 gs\n'
        resources = b'/ExtGState << /GS1 << /Type /ExtGState /CA 0.6 /ca 0.6 >> >> '
    else:
        raise ValueError('Unknown synthetic clip case')
    body = b'q\n' + clip + body + b'Q\n'
    if case == 'saved-restore':
        body += b'0 0 1 rg 275 20 10 10 re f\n'
    return assemble(body, ICC_GRAY_PROFILE, resources)


def visible(case, x, y):
    """Known PDF-space visibility; None marks boundary samples to exclude."""
    if case in ('nested-tight', 'saved-restore'):
        if min(abs(x-55), abs(x-235), abs(y-45), abs(y-105)) < 3:
            return None
        return 55 < x < 235 and 45 < y < 105
    if case == 'hole-evenodd':
        if min(abs(x-80), abs(x-220), abs(y-55), abs(y-95)) < 3:
            return None
        return not (80 < x < 220 and 55 < y < 95)
    if case == 'triangle':
        # Signed cross-products for a counter-clockwise triangle; reject
        # samples within three PDF points of an edge.
        vertices = ((35,25),(270,25),(150,135))
        distances=[]
        for a,b in zip(vertices,vertices[1:]+vertices[:1]):
            dx,dy=b[0]-a[0],b[1]-a[1]
            distances.append((dx*(y-a[1])-dy*(x-a[0]))/(dx*dx+dy*dy)**.5)
        if min(abs(v) for v in distances)<3:return None
        return all(v>0 for v in distances)
    return case != 'disjoint-clips'


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    args.path.write_bytes(build())
