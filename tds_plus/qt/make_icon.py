# language: Python 3, file: make_icon.py, runtime: Python 3.10+, target: native Windows icon asset
from pathlib import Path
import struct
import zlib


def png(size):
    pixels = bytearray()
    for y in range(size):
        pixels.append(0)
        for x in range(size):
            u, v = x / size, y / size
            radius = 0.19
            dx = max(radius - u, u - (1 - radius), 0)
            dy = max(radius - v, v - (1 - radius), 0)
            alpha = 255 if dx * dx + dy * dy < radius * radius else 0
            ix, iy = int((u - 0.20) / 0.12), int((v - 0.20) / 0.12)
            lit = 0.2 <= u < 0.8 and 0.2 <= v < 0.8 and (ix == 2 or iy == 2)
            gap = ((u - 0.20) % 0.12 > 0.093) or ((v - 0.20) % 0.12 > 0.093)
            color = (230, 230, 235) if lit and not gap else (23, 23, 26)
            pixels.extend((*color, alpha))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


images = [(size, png(size)) for size in (32, 48, 64, 256)]
offset = 6 + 16 * len(images)
header = bytearray(struct.pack('<HHH', 0, 1, len(images)))
for size, data in images:
    header.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
    offset += len(data)
Path(__file__).with_name('tds_plus.ico').write_bytes(header + b''.join(data for _, data in images))
