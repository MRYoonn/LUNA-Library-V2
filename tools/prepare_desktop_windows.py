"""Select a supported CI display mode and verify the actual desktop dimensions."""
import ctypes
import json
from pathlib import Path
import struct
import sys


def prepare(output):
    user = ctypes.windll.user32
    user.SetProcessDPIAware()
    modes = []
    index = 0
    while True:
        mode = ctypes.create_string_buffer(220)  # DEVMODEW
        struct.pack_into('<H', mode, 68, 220)
        if not user.EnumDisplaySettingsW(None, index, mode):
            break
        width, height = struct.unpack_from('<II', mode, 172)
        modes.append((width, height, bytes(mode)))
        index += 1
    attempts = []
    for width, height, data in sorted(modes, key=lambda m: m[0] * m[1], reverse=True):
        mode = ctypes.create_string_buffer(data, 220)
        code = user.ChangeDisplaySettingsW(mode, 0)
        actual = [user.GetSystemMetrics(0), user.GetSystemMetrics(1)]
        attempts.append({'requested': [width, height], 'code': code, 'actual': actual})
        if code == 0 and actual == [width, height]:
            break
    result = {'modes': sorted(set((m[0], m[1]) for m in modes)), 'attempts': attempts,
              'actual': [user.GetSystemMetrics(0), user.GetSystemMetrics(1)]}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    if result['actual'][0] < 1280 or result['actual'][1] < 900:
        raise RuntimeError('No supported desktop large enough for the full 150% UI acceptance')


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
