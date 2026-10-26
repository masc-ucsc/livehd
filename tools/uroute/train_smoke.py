# SPDX-License-Identifier: BSD-3-Clause
"""Offline-only CPU trainer validation. Requires the user's torch environment.

python -m tools.uroute.train_smoke
All training data and weights are synthetic and temporary.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import time

from .export_weights import decode_tensors
from .features import Maps
from .fixture import inputs
from .pool import publish_sample, read_manifest, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, help="also exercise native calibrated held-out-design evaluation")
    args = parser.parse_args()
    # A negative final preactivation has no ReLU gradient. The opt-in linear
    # loss must recover that gradient without changing exported inference.
    import torch
    from .model import URoute
    from .train import normalized_label_tensor
    torch.set_num_threads(1)
    # Non-binary ranges expose a real input discrepancy: native normalization
    # uses double subtraction/division before its f32 cast, not f32 division.
    f32 = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]
    raw = [0., f32(.1), f32(.3)]
    precision = Maps(3, 1, 3, False, raw + [2.] * 3 + [0.] * 3,
                     [(0., .3), (2., 2.), (0., 0.)])
    expected = torch.tensor([0., f32(raw[1] / .3), 1.], dtype=torch.float32).reshape(1, 1, 3)
    actual = normalized_label_tensor(precision, 0)
    assert torch.equal(actual, expected)
    assert not torch.equal(torch.tensor(raw, dtype=torch.float32).reshape(1, 1, 3) / .3, expected)
    assert torch.equal(normalized_label_tensor(precision, 1), torch.zeros(1, 1, 3))
    probe = URoute()
    with torch.no_grad():
        probe.output.weight.zero_()
        probe.output.bias.fill_(-1.)
    q, s = torch.zeros(1, 4, 8, 8), torch.zeros(1, 1, 5, 8, 8)
    clamped = probe(q, s)
    assert bool(torch.all(clamped == 0))
    (clamped - 1).square().mean().backward()
    assert bool(torch.all(probe.output.bias.grad == 0))
    probe.zero_grad(set_to_none=True)
    linear = probe(q, s, linear_head=True)
    (linear - 1).square().mean().backward()
    assert bool(torch.all(probe.output.bias.grad < 0))
    del probe, q, s, linear, clamped
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="uroute_train_smoke_") as temporary:
        root = Path(temporary)
        area = 256 * 256
        raw = [2 * (i % 256) / 255 for i in range(area)]
        labels = Maps(3, 256, 256, False, raw + [0.] * area + [1.] * area, [(0., 2.), (0., 0.), (1., 1.)])
        for design in ("A", "B", "C"):
            for version in range(2):
                features, _ = inputs(256, 256, 1)
                features.values = [min(1., v * (.7 + .1 * version)) for v in features.values]
                publish_sample(root, {"top": design, "technology": "synthetic-test-only", "created_at": version,
                                      "source": "external", "status": "routed", "seed": version,
                                      "floorplan": {"die_width": 16., "die_height": 16., "macro_signature": "", "utilization": .5}},
                               features, labels)
        command = [sys.executable, "-m", "tools.uroute.train", "--pool", str(root), "--holdout", "C",
                   "--from-scratch", "--epochs", "1", "--episodes", "1", "--shots", "1", "--device", "cpu"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stderr
        output = json.loads(result.stdout)
        manifest = read_manifest(root)
        assert len(manifest["samples"]) == 6
        assert output["provenance"]["training_designs"] == ["A", "B"]
        assert output["provenance"]["holdout_design"] == "C" and output["provenance"]["device"] == "cpu"
        assert output["provenance"]["initial_weights_sha256"] is None
        assert math.isfinite(output["provenance"]["best_validation_loss"])
        weight_data = (root / manifest["weights"]["file"]).read_bytes()
        assert sha(weight_data) == manifest["weights"]["sha256"]
        decode_tensors(weight_data)
        log = [json.loads(line) for line in (root / "train/log.jsonl").read_text().splitlines()]
        assert len(log) == 1 and len(log[0]["importance"]) == 4
        assert len(log[0]["validation_tasks"]) == 2
        assert abs(sum(task["probability"] for task in log[0]["importance"]) - 1.) < 1e-12
        assert output["provenance"]["seen_designs"] == ["A", "B", "C"]
        assert output["provenance"]["lineage_complete"] is True
        first_log = root / output['provenance']['training_log']
        first_log_bytes = first_log.read_bytes()
        assert sha(first_log_bytes) == output['provenance']['training_log_sha256']
        first_manifest = root / output['provenance']['input_manifest']
        assert sha(first_manifest.read_bytes()) == output['provenance']['input_manifest_sha256']
        assert json.loads(first_manifest.read_bytes())['weights'] is None
        implementation = output['provenance']['implementation']
        source_directory = root / implementation['directory']
        assert all(sha((source_directory / name).read_bytes()) == digest
                   for name, digest in implementation['files'].items())
        # Identical recipe reruns must retain the first published attempt's
        # log, even though timestamps and execution times necessarily differ.
        repeated = subprocess.run(command, capture_output=True, text=True, timeout=20)
        assert repeated.returncode == 0, repeated.stderr
        replay = json.loads(repeated.stdout)
        assert replay['provenance']['run_identity'] == output['provenance']['run_identity']
        assert replay['provenance']['attempt_id'] != output['provenance']['attempt_id']
        assert replay['provenance']['training_log'] != output['provenance']['training_log']
        assert first_log.read_bytes() == first_log_bytes
        assert replay['provenance']['implementation'] == implementation
        initial_hash = replay["weights"]["sha256"]
        fine_command = [part for part in command if part != "--from-scratch"]
        fine_result = subprocess.run(fine_command, capture_output=True, text=True, timeout=20)
        assert fine_result.returncode == 0, fine_result.stderr
        fine = json.loads(fine_result.stdout)
        assert fine["provenance"]["initial_weights_sha256"] == initial_hash
        fine_input = json.loads((root / fine['provenance']['input_manifest']).read_bytes())
        assert fine_input['weights']['sha256'] == initial_hash
        assert fine_input['provenance']['training_log'] == replay['provenance']['training_log']
        assert first_log.read_bytes() == first_log_bytes
        assert len(read_manifest(root)["samples"]) == 6
        assert len(list((root / "train").glob("log_*.jsonl"))) == 3
        # A model selected using C must not initialize a fold advertised as
        # excluding C. A fresh fold holds out one other task for early stopping.
        fold_command = [sys.executable, "-m", "tools.uroute.train", "--pool", str(root),
                        "--validation-task", '["A","synthetic-test-only",0]', "--exclude-design", "C",
                        "--epochs", "1", "--episodes", "1", "--shots", "1", "--device", "cpu"]
        contaminated = subprocess.run(fold_command, capture_output=True, text=True, timeout=20)
        assert contaminated.returncode != 0 and "already seen an excluded" in contaminated.stderr, contaminated.stderr
        fold_result = subprocess.run(fold_command + ["--from-scratch"], capture_output=True, text=True, timeout=20)
        assert fold_result.returncode == 0, fold_result.stderr
        fold = json.loads(fold_result.stdout)
        assert fold["provenance"]["seen_designs"] == ["A", "B"]
        assert fold["provenance"]["excluded_designs"] == ["C"]
        assert fold["provenance"]["validation_task"] == ["A", "synthetic-test-only", 0]
        current = read_manifest(root)
        ids = {r["id"] for r in current["samples"] if r["top"] == "C"}
        assert ids.isdisjoint(fold["provenance"]["sample_ids"])
        evaluation = {}
        if args.native:
            from .evaluate import evaluate
            from .export_weights import load_model
            from .maps import read
            from .train import Dataset
            import torch
            torch.set_num_threads(1)
            model = load_model(root / current["weights"]["file"]).eval()
            reference_data = Dataset(root, "C")
            query = max((r for r in current["samples"] if r["top"] == "C"), key=lambda r: r["created_at"])
            for mode in ("earlier", "zero-shot"):
                options = argparse.Namespace(pool=root, output=root / ("evaluation_" + mode), query=[query["id"]],
                                             query_top=None, require_unseen_design=True, mode=mode, shots=1, min_shots=1,
                                             fallback="nearest", support=[], native=args.native, timeout=20, rudy_metrics=None)
                report = evaluate(options)
                assert report["coverage"] == {"requested": 1, "scored": 1, "skipped": 0}
                scored = report["queries"][0]
                assert scored["unseen_design"]["verified"] and query["id"] not in scored["support_ids"]
                if mode == "earlier":
                    assert set(scored["support_ids"]).issubset(ids)
                else:
                    assert ids.isdisjoint(scored["support_ids"])
                if mode == "earlier":
                    query_pair = reference_data.sample(query["id"], 0)
                    support_pairs = torch.stack([reference_data.sample(key, 0) for key in scored["support_ids"]])
                    with torch.inference_mode():
                        expected = 2 * model(query_pair[:4].unsqueeze(0), support_pairs.unsqueeze(0)).reshape(-1)
                    actual = torch.tensor(read(options.output / scored["prediction"]).values[:area])
                    error = (expected - actual).abs()
                    assert bool(torch.all(error <= 1e-7 + 1e-4 * expected.abs())), float(error.max())
                    evaluation["native_pytorch_max_abs"] = float(error.max())
                evaluation[mode] = {"coverage": report["coverage"], "native_seconds": scored["native_seconds"],
                                    "support_ids": scored["support_ids"]}
        print(json.dumps({"status": "passed", "training_seconds": output["seconds"],
                          "fine_tune_seconds": fine["seconds"], "excluded_fold_seconds": fold["seconds"],
                          "evaluation": evaluation,
                          "total_seconds": time.monotonic() - started, "weights_bytes": len(weight_data),
                          "heldout_loss": output["provenance"]["best_validation_loss"]}, indent=2))


if __name__ == "__main__":
    main()
