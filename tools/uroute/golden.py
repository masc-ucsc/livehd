# SPDX-License-Identifier: BSD-3-Clause
"""Mandatory offline PyTorch/native golden check; run in the training venv.

python -m tools.uroute.golden bazel-bin/tools/uroute/native
Random weights are test fixtures only. They must never be shipped as pretrained.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

import numpy as np
import torch

from .export_weights import encode_tensors, export_model, load_model
from .fixture import attention_cancellation_tensors, cancellation_tensors, inputs, support_cancellation_tensors, tensors
from .features import Maps
from .maps import read, write
from .model import URoute


def map_of(tensor):
    a = tensor.detach().cpu().numpy()
    return Maps(a.shape[0], a.shape[1], a.shape[2], True, a.flatten().tolist(), [(0., 1.)] * a.shape[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("--production-grid", action="store_true", help="also compare a full 256x256 query")
    parser.add_argument("--benchmark-reuse", action="store_true", help="compare and time reusable support encodings, including 256x256 one/five-shot queries")
    parser.add_argument("--refresh", type=Path, help="write the hermetic test reference using PyTorch")
    args = parser.parse_args()
    binary = str(args.native.resolve())
    torch.set_num_threads(1)
    torch.manual_seed(19)
    model = URoute().eval()
    # Keep a nonzero output so a broken all-zero predictor cannot pass after
    # the output ReLU. All other weights remain seeded random values.
    with torch.no_grad():
        model.output.bias.fill_(.2)
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        weights = root / "weights.bin"
        export_model(model, weights)
        loaded = load_model(weights).eval()
        if args.refresh:
            fixture_weights = root / "fixture_weights.bin"
            fixture_weights.write_bytes(encode_tensors(tensors()))
            fixture_model = load_model(fixture_weights).eval()
            cases = []
            fixtures = {None: tensors, "cancellation": cancellation_tensors,
                        "support_cancellation": support_cancellation_tensors,
                        "attention_cancellation": attention_cancellation_tensors}
            for nx, ny, shots, fixture in [(8, 8, 1, None), (17, 19, 2, None),
                                           (17, 19, 1, "cancellation"), (17, 19, 4, "support_cancellation"),
                                           (17, 19, 4, "attention_cancellation")]:
                fixture_weights.write_bytes(encode_tensors(fixtures[fixture]()))
                fixture_model = load_model(fixture_weights).eval()
                qmap, smaps = inputs(nx, ny, shots)
                q = torch.tensor(qmap.values, dtype=torch.float32).reshape(1, 4, nx, ny)
                support = torch.tensor([m.values for m in smaps], dtype=torch.float32).reshape(1, shots, 5, nx, ny)
                with torch.no_grad():
                    out = fixture_model(q, support).numpy().flatten().tolist()
                cases.append({"shape": [nx, ny], "shots": shots, "output": out,
                              **({"fixture": fixture} if fixture else {})})
            args.refresh.write_text(json.dumps({"arch_version": 1, "generator": "tools.uroute.golden --refresh",
                                               "torch_version": torch.__version__, "cases": cases}, indent=2) + "\n")
        records = []
        cases = [(8, 8, 1), (17, 19, 2)] + ([(256, 256, 1)] if args.production_grid or args.benchmark_reuse else [])
        if args.benchmark_reuse:
            cases.append((256, 256, 5))
        for nx, ny, shots in cases:
            query = torch.rand(1, 4, nx, ny)
            support = torch.rand(1, shots, 5, nx, ny)
            with torch.no_grad():
                expected = model(query, support).numpy().ravel()
                roundtrip = loaded(query, support).numpy().ravel()
            np.testing.assert_array_equal(expected, roundtrip)
            write(root / "query.bin", map_of(query[0]))
            paths = []
            for i in range(shots):
                path = root / f"support_{i}.bin"
                write(path, map_of(support[0, i]))
                paths.append(str(path))
            command = [binary, "infer", str(weights), str(root / "query.bin"), *paths, str(root / "out.bin")]
            subprocess.run(command, check=True, timeout=20)
            actual = np.array(read(root / "out.bin").values)
            np.testing.assert_allclose(actual, expected, rtol=1e-4, atol=1e-7)
            first = (root / "out.bin").read_bytes()
            repeated = [*command]
            if args.benchmark_reuse:
                repeated[1] = "infer-reuse"
            measurement = subprocess.run(repeated, check=True, capture_output=True, text=True,
                                         timeout=60 if args.benchmark_reuse else 20)
            assert first == (root / "out.bin").read_bytes(), "native output is nondeterministic"
            records.append({"shape": [nx, ny], "shots": shots,
                            "maximum_absolute_error": float(np.max(np.abs(actual - expected))),
                            "maximum_relative_error": float(np.max(np.abs(actual - expected) / np.maximum(np.abs(expected), 1e-7))),
                            **({"support_reuse_timing": json.loads(measurement.stdout)} if args.benchmark_reuse else {})})
        # Check real adaptation, and that spatial structure survives the encoder.
        changed = support.clone()
        changed[:, :, 4] = 1 - changed[:, :, 4]
        with torch.no_grad():
            assert not torch.equal(model(query, support), model(query, changed))
        assert float(expected.max() - expected.min()) > 1e-4
        bad = root / "bad.bin"
        bad.write_bytes(weights.read_bytes()[:-1])
        rejected = subprocess.run([binary, "infer", str(bad), str(root / "query.bin"), *paths, str(root / "badout.bin")],
                                  capture_output=True, timeout=10)
        assert rejected.returncode != 0 and b"URoute" in rejected.stderr
        print(json.dumps({"arch_version": 1, "weights_bytes": weights.stat().st_size,
                          "cases": records, "seconds": time.monotonic() - start}, indent=2))


if __name__ == "__main__":
    main()
