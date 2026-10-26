# SPDX-License-Identifier: BSD-3-Clause
"""Deterministic synthetic golden fixtures. Never use as pretrained weights."""
from array import array
import math
import struct
import sys
from .features import Maps


def shapes():
    result = {}

    def conv(name, inputs, outputs, kernel):
        result[name + ".weight"] = (outputs, inputs, kernel, kernel)
        result[name + ".bias"] = (outputs,)

    conv("q_embed", 4, 64, 3)
    conv("s_embed", 5, 64, 3)
    for level in range(4):
        for prefix in ("query", "support", "decoder"):
            inputs = 128 if prefix == "decoder" else 64
            conv(f"{prefix}.{level}.conv1", inputs, 64, 3)
            conv(f"{prefix}.{level}.conv2", 64, 64, 3)
            if inputs != 64:
                conv(f"{prefix}.{level}.skip", inputs, 64, 1)
        conv(f"matches.{level}.z", 128, 64, 3)
        conv(f"matches.{level}.gate", 1, 1, 7)
        conv(f"matches.{level}.out", 64, 64, 1)
    conv("output", 64, 1, 1)
    return result


def tensors():
    state, output = 19, {}
    for name, shape in sorted(shapes().items()):
        scale = 1 / math.sqrt(math.prod(shape[1:])) if len(shape) > 1 else .02
        values = array("f")
        for _ in range(math.prod(shape)):
            state = (1664525 * state + 1013904223) & 0xffffffff
            values.append(((state >> 8) / 16777216. * 2 - 1) * scale)
        if name == "output.bias":
            values[0] = .2
        if sys.byteorder != "little":
            values.byteswap()
        output[name] = (shape, values.tobytes())
    return output


def cancellation_tensors():
    """Amplify the decoder signal and put an odd-grid output near zero.

    This exercises interpolation/reduction rounding that a positive head bias
    hides. The constants are synthetic; no routed sample or trained weights
    contribute to this fixture.
    """
    result = tensors()
    shape, data = result["output.weight"]
    values = struct.unpack("<" + "f" * (len(data) // 4), data)
    result["output.weight"] = (shape, struct.pack("<" + "f" * len(values), *(32 * v for v in values)))
    result["output.bias"] = ((1,), struct.pack("<f", .48317378759384155))
    return result


def support_cancellation_tensors():
    """Four-shot cancellation exposes a fused support multiply/add.

    The attention product is rounded before adding each shot in PyTorch.
    This synthetic head keeps that rounding difference visible near zero.
    """
    result = cancellation_tensors()
    result["output.bias"] = ((1,), struct.pack("<f", 2.718822956085205))
    return result


def attention_cancellation_tensors():
    """Expose the system-expf versus CPU Torch attention rounding difference."""
    result = support_cancellation_tensors()
    result["output.bias"] = ((1,), struct.pack("<f", 2.7178239822387695))
    return result


def inputs(nx, ny, shots):
    def data(channels, seed):
        state, values = seed, []
        for _ in range(channels * nx * ny):
            state = (1664525 * state + 1013904223) & 0xffffffff
            values.append((state >> 8) / 16777216.)
        return Maps(channels, nx, ny, True, values, [(0., 1.)] * channels)
    return data(4, 71), [data(5, 80 + i) for i in range(shots)]
