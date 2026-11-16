"""Inspect a locally owned NEF8/YBN fixture without modifying it."""
from pathlib import Path
import json
import struct
import sys
import zlib

file = Path(sys.argv[1]) if len(sys.argv) > 1 else next(
    Path("Projects/KaylaNightCity/migration/source-assets/models/world/nightcity_unified").glob("*.ybn"))
data = file.read_bytes()
size, expanded = struct.unpack_from("<QQ", data, 16)
start = len(data) - size
body = zlib.decompress(data[start:], -15)
json_at = data.find(b'{"authored_by"')
print("PATH", file)
print("SIZE", len(data), "NEF8", data[:12].hex(" "), "KIND", struct.unpack_from("<H", data, 6)[0])
print("COMPRESSED", size, "EXPANDED", expanded, "ACTUAL", len(body), "BODY_START", start)
print("HEADER_128", data[:128].hex(" "))
print("BODY_128", body[:128].hex(" "))
print("NEF16_AT", data.find(b"NEF16"), "JSON_AT", json_at, "NVRC_BODY_AT", body.find(b"NVRC"))
if json_at >= 0:
    try:
        metadata = json.loads(data[json_at:start].decode("utf-8"))
        print("METADATA", json.dumps(metadata, ensure_ascii=False)[:2400])
    except Exception as e:
        print("METADATA_ERROR", str(e))
print("BODY_FIRST", repr(body[:128]))

meta_len = struct.unpack_from("<I", body, 8)[0]
print("NVRC_META", body[20:20+meta_len].decode("utf-8","replace"))
try:
    from collections import Counter
    m = json.loads(body[20:20+meta_len])
    packed = body[20+meta_len:]
    part = m["materials"]
    count = m["triangle_count"]
    material_ids = struct.unpack_from("<" + str(count) + "I", packed, part["offset"])
    print("SOURCE_MATERIAL_FREQUENCY", Counter(material_ids).most_common(24))
except (ValueError,KeyError,struct.error) as error:
    print("MATERIAL_INSPECT_ERROR", error)
