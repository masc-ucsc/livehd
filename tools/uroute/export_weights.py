# SPDX-License-Identifier: BSD-3-Clause
"""Versioned flat tensors with a bounded descriptor table and aligned payloads.

No pickle is consumed by C++. Immutable content-named weights are installed
before the owning manifest is atomically replaced by the pool writer.
"""
import argparse
import hashlib
import math
from pathlib import Path
import struct

MAGIC = b"UROUTEW1"
ARCH_VERSION = 1
LIMIT = 256 * 1024 * 1024


def encode_tensors(tensors):
    """tensors maps ASCII names to (shape, little-endian f32 bytes)."""
    entries = []
    header_size = 16
    for name, (shape, data) in sorted(tensors.items()):
        encoded = name.encode("ascii")
        if any(c < 32 or c >= 127 for c in encoded) or not 1 <= len(encoded) <= 128 or not 1 <= len(shape) <= 4 or any(not 1 <= d <= 4096 for d in shape):
            raise ValueError("invalid URoute tensor descriptor")
        if len(data) != math.prod(shape) * 4:
            raise ValueError("URoute tensor shape mismatch")
        if not all(math.isfinite(v[0]) for v in struct.iter_unpack("<f", data)):
            raise ValueError("nonfinite URoute tensor")
        entries.append((encoded, shape, data))
        header_size += 4 + ((len(encoded) + 3) // 4) * 4 + 4 + 4 * len(shape) + 8
    if not 1 <= len(entries) <= 1024:
        raise ValueError("invalid URoute tensor count")
    offset = (header_size + 63) // 64 * 64
    header = bytearray(struct.pack("<8sII", MAGIC, ARCH_VERSION, len(entries)))
    chunks = []
    for name, shape, data in entries:
        header.extend(struct.pack("<I", len(name)) + name + bytes((-len(name)) % 4))
        header.extend(struct.pack(f"<I{len(shape)}IQ", len(shape), *shape, offset))
        chunks.append((offset, data))
        offset = (offset + len(data) + 63) // 64 * 64
    if offset > LIMIT:
        raise ValueError("oversized URoute weights")
    output = bytearray(offset)
    output[:len(header)] = header
    for offset, data in chunks:
        output[offset:offset + len(data)] = data
    return bytes(output)


def decode_tensors(data):
    if len(data) < 16 or len(data) > LIMIT:
        raise ValueError("invalid URoute weights size")
    magic, version, count = struct.unpack_from("<8sII", data)
    if magic != MAGIC or version != ARCH_VERSION or not 1 <= count <= 1024:
        raise ValueError("unsupported URoute weights header")
    pos, descriptors = 16, []
    try:
        for _ in range(count):
            length, = struct.unpack_from("<I", data, pos)
            pos += 4
            if not 1 <= length <= 128:
                raise ValueError("invalid URoute tensor name")
            name = data[pos:pos + length].decode("ascii")
            if len(name) != length or any(ord(c) < 32 or ord(c) >= 127 for c in name):
                raise ValueError("truncated URoute tensor name")
            pos += (length + 3) // 4 * 4
            rank, = struct.unpack_from("<I", data, pos)
            pos += 4
            if not 1 <= rank <= 4:
                raise ValueError("invalid URoute tensor rank")
            shape = struct.unpack_from(f"<{rank}I", data, pos)
            pos += 4 * rank
            offset, = struct.unpack_from("<Q", data, pos)
            pos += 8
            if any(not 1 <= d <= 4096 for d in shape):
                raise ValueError("invalid URoute tensor dimension")
            descriptors.append((name, shape, offset))
    except (struct.error, UnicodeDecodeError) as exc:
        raise ValueError("malformed URoute descriptor table") from exc
    result, end = {}, (pos + 63) // 64 * 64
    for name, shape, offset in descriptors:
        size = math.prod(shape) * 4
        if name in result or offset != end or offset + size > len(data):
            raise ValueError("invalid URoute tensor extent/order")
        values = data[offset:offset + size]
        if not all(math.isfinite(v[0]) for v in struct.iter_unpack("<f", values)):
            raise ValueError("nonfinite URoute tensor")
        result[name] = (shape, values)
        end = (offset + size + 63) // 64 * 64
    if end != len(data):
        raise ValueError("trailing URoute weight data")
    return result


def export_model(model, path):
    tensors = {name: (tuple(t.shape), t.detach().cpu().contiguous().numpy().astype("<f4").tobytes())
               for name, t in model.state_dict().items()}
    data = encode_tensors(tensors)
    Path(path).write_bytes(data)
    return hashlib.sha256(data).hexdigest()


def load_model(path):
    import numpy as np
    import torch
    from .model import URoute
    model = URoute()
    tensors = decode_tensors(Path(path).read_bytes())
    expected = model.state_dict()
    if set(tensors) != set(expected):
        raise ValueError("URoute weight names do not match architecture v1")
    state = {}
    for name, (shape, data) in tensors.items():
        if tuple(expected[name].shape) != shape:
            raise ValueError(f"URoute weight shape mismatch: {name}")
        state[name] = torch.from_numpy(np.frombuffer(data, dtype="<f4").copy().reshape(shape))
    model.load_state_dict(state, strict=True)
    return model


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state_dict", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    import torch
    from .model import URoute
    model = URoute()
    model.load_state_dict(torch.load(args.state_dict, map_location="cpu", weights_only=True), strict=True)
    print(export_model(model, args.output))


if __name__ == "__main__":
    main()
