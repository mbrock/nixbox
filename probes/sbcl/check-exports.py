"""Reject recursive export aliases in the linked x64 SBCL host.

LLD's fuzzy .def matching can select C++ math wrappers whose C tail calls
resolve back to themselves. Check the executable, not just the export names.
"""

import struct
import sys
from pathlib import Path

image = Path(sys.argv[1]).read_bytes()
pe = struct.unpack_from("<I", image, 0x3C)[0]
section_count = struct.unpack_from("<H", image, pe + 6)[0]
optional = pe + 24
sections = optional + struct.unpack_from("<H", image, pe + 20)[0]


def offset(rva):
    for index in range(section_count):
        virtual_size, address, size, start = struct.unpack_from(
            "<IIII", image, sections + index * 40 + 8
        )
        if address <= rva < address + max(virtual_size, size):
            return start + rva - address
    raise ValueError(f"Unmapped RVA: {rva:#x}")


directory = offset(struct.unpack_from("<I", image, optional + 112)[0])
count = struct.unpack_from("<I", image, directory + 24)[0]
functions, names, ordinals = map(
    offset, struct.unpack_from("<III", image, directory + 28)
)
recursive = []
for index in range(count):
    name_start = offset(struct.unpack_from("<I", image, names + index * 4)[0])
    name = image[name_start : image.index(0, name_start)].decode("ascii")
    ordinal = struct.unpack_from("<H", image, ordinals + index * 2)[0]
    body = offset(struct.unpack_from("<I", image, functions + ordinal * 4)[0])
    if image[body : body + 5] == b"\xe9\xfb\xff\xff\xff" or image[
        body : body + 2
    ] == b"\xeb\xfe":
        recursive.append(name)

if recursive:
    sys.exit(f"Recursive SBCL export aliases: {', '.join(recursive)}")
print(f"SBCL export check: {count} exports, no unconditional self-jumps")
