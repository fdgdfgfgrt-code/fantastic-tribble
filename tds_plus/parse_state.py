# tolerant TDS serializer parser: walk a window, decode len-prefixed strings,
# 0x0c doubles, 09 06 doubles, JSON blobs, and show raw bytes otherwise
import re, struct, sys

def parse(data, start, length):
    out = []
    i = start
    end = min(len(data), start + length)
    while i < end:
        b = data[i]
        # 0x0c + 8-byte double
        if b == 0x0C and i + 9 <= end:
            v = struct.unpack("<d", data[i+1:i+9])[0]
            out.append(f"DUB({v:.4g})")
            i += 9
            continue
        # 0x09 0x06 + 8-byte double
        if b == 0x09 and i + 10 <= end and data[i+1] == 0x06:
            v = struct.unpack("<d", data[i+2:i+10])[0]
            out.append(f"NUM({v:.4g})")
            i += 10
            continue
        # 0x02 + len + string
        if b == 0x02 and i + 2 <= end:
            ln = data[i+1]
            s = data[i+2:i+2+ln]
            if ln and all(0x20 <= c < 0x7F for c in s):
                out.append(f'STR"{s.decode()}"')
                i += 2 + ln
                continue
        # 0x04 + len + string (key)
        if b == 0x04 and i + 2 <= end:
            ln = data[i+1]
            s = data[i+2:i+2+ln]
            if ln and all(0x20 <= c < 0x7F for c in s):
                out.append(f'KEY"{s.decode()}"')
                i += 2 + ln
                continue
        # JSON blob
        if b == 0x7B:  # {
            j = data.find(b"}", i)
            if 0 < j - i < 300:
                out.append("JSON" + data[i:j+1].decode(errors="replace"))
                i = j + 1
                continue
        # table markers seen so far
        if b == 0x1E and i + 2 <= end and data[i+1] == 0x01:
            out.append("TABLE")
            i += 2
            continue
        if b == 0x1F:
            out.append("T_END?")
            i += 1
            continue
        out.append(f"{b:02x}")
        i += 1
    return " ".join(out)

data = open("scan_tr/hitregion_0000025624700000.bin", "rb").read()
base = 0x25624700000
for m in re.finditer(rb"Troops", data):
    off = m.start()
    print(f"=== record around {base+off:x} (from -700) ===")
    print(parse(data, max(0, off - 700), 1400))
    print()
