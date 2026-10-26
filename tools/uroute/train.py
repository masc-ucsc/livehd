# SPDX-License-Identifier: BSD-3-Clause
"""Offline episodic meta-training; LiveHD never imports this module.

python -m tools.uroute.train --pool project-data --holdout design_name
Use --from-scratch only when intentionally training a new universal model.
"""
import argparse
from collections import defaultdict
from contextlib import contextmanager
import fcntl
from functools import lru_cache
import json
import math
import os
from pathlib import Path
import platform
import random
import sys
import tempfile
import time

from .episodes import ImportanceSampler
from .export_weights import LIMIT
from .features import NETWORK_BINS
from .pool import VERSIONS, _atomic, canonical, inside, load_sample, publish_weights, read_manifest, sha


def normalized_label_tensor(labels, metric):
    """Match UrouteContext's double normalization followed by an f32 cast."""
    import torch
    area = labels.bins_x * labels.bins_y
    lo, hi = labels.ranges[metric]
    raw = torch.tensor(labels.values[metric * area:(metric + 1) * area], dtype=torch.float64)
    y = torch.zeros_like(raw) if hi == lo else ((raw - lo) / (hi - lo)).clamp(0, 1)
    return y.to(torch.float32).reshape(1, labels.bins_x, labels.bins_y)


class Dataset:
    def __init__(self, root, holdout, excluded_designs=(), validation_task=None):
        self.root = Path(root)
        excluded_designs = set(excluded_designs)
        if (holdout is None) == (validation_task is None):
            raise ValueError("choose one validation design or one validation task")
        if holdout in excluded_designs or (validation_task and validation_task[0] in excluded_designs):
            raise ValueError("validation data cannot be an excluded evaluation design")
        self.manifest = read_manifest(root)
        self.records = {}
        self.train, self.validation = defaultdict(list), defaultdict(list)
        for record in self.manifest["samples"]:
            if record.get("status") != "routed" or any(type(record.get(k)) is not int or record[k] != v for k, v in VERSIONS.items()):
                continue
            if not isinstance(record.get("top"), str) or not isinstance(record.get("technology"), str):
                raise ValueError("sample is missing task identity")
            if record["top"] in excluded_designs:
                continue
            self.records[record["id"]] = record
            for metric in (0, 1):
                task = (record["top"], record["technology"], metric)
                (self.validation if record["top"] == holdout or task == validation_task else self.train)[task].append(record["id"])
        for groups in (self.train, self.validation):
            for task in groups:
                groups[task].sort()
            if any(len(ids) < 2 for ids in groups.values()):
                raise ValueError("each routed task requires at least two distinct query/support samples")
        if not self.validation:
            raise ValueError("validation design/task has no compatible routed samples")
        if len({task[0] for task in self.train}) < 2:
            raise ValueError("universal training requires at least two gradient-training designs and a held-out validation task")
        self.identity = sha(canonical({"versions": VERSIONS, "holdout": holdout,
                                      "validation_task": validation_task, "excluded_designs": sorted(excluded_designs),
                                      "samples": [{k: record[k] for k in ("id", "features_sha256", "labels_sha256")}
                                                  for record in sorted(self.records.values(), key=lambda r: r["id"])]}))

    @lru_cache(maxsize=16)
    def sample(self, sample_id, metric):
        import torch
        # Train only on supports the placer can load (all label channels).
        features, labels, _ = load_sample(self.root, self.records[sample_id])
        x = torch.tensor(features.values, dtype=torch.float32).reshape(4, NETWORK_BINS, NETWORK_BINS)
        y = normalized_label_tensor(labels, metric)
        return torch.cat((x, y), dim=0)


def augment(pair, rng):
    """One spatial transform shared by a sample's features and label."""
    import torch
    pair = torch.rot90(pair, rng.randrange(4), dims=(-2, -1))
    if rng.randrange(2):
        pair = pair.flip(-1)
    if rng.randrange(2):
        pair = pair.flip(-2)
    maximum = max(1, min(pair.shape[-2:]) // 16)
    dx, dy = rng.randint(-maximum, maximum), rng.randint(-maximum, maximum)
    shifted = torch.zeros_like(pair)
    nx, ny = pair.shape[-2:]
    shifted[..., max(dx, 0):min(nx + dx, nx), max(dy, 0):min(ny + dy, ny)] = \
        pair[..., max(-dx, 0):min(nx - dx, nx), max(-dy, 0):min(ny - dy, ny)]
    return shifted


def episode(dataset, task, ids, shots, rng, device, augmented):
    import torch
    query_id = rng.choice(ids)
    supports = rng.sample([sample for sample in ids if sample != query_id], min(shots, len(ids) - 1))
    query = dataset.sample(query_id, task[2])
    support = [dataset.sample(sample, task[2]) for sample in supports]
    if augmented:
        query = augment(query, rng)
        support = [augment(pair, rng) for pair in support]
    return query[:4].unsqueeze(0).to(device), query[4:].unsqueeze(0).to(device), torch.stack(support).unsqueeze(0).to(device)


def validate(model, dataset, shots, device):
    import torch
    losses = {}
    model.eval()
    with torch.no_grad():
        for task, ids in sorted(dataset.validation.items()):
            values = []
            # Every held-out placement is a query once. It cannot support itself.
            for query_id in ids:
                query = dataset.sample(query_id, task[2])
                others = [sample for sample in ids if sample != query_id]
                support = torch.stack([dataset.sample(sample, task[2]) for sample in others[:shots]])
                output = model(query[:4].unsqueeze(0).to(device), support.unsqueeze(0).to(device))
                values.append(float(torch.nn.functional.mse_loss(output, query[4:].unsqueeze(0).to(device))))
            losses[task] = sum(values) / len(values)
    return sum(losses.values()) / len(losses), losses


@contextmanager
def training_lock(root):
    directory = Path(root) / "train"
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / ".train.lock").open("a+b") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise ValueError("another trainer owns this project data directory") from exc
        yield directory


def training_implementation(log_dir):
    """Retain exact local training sources, independently of git publication."""
    names = ('train.py', 'episodes.py', 'model.py', 'export_weights.py',
             'features.py', 'maps.py', 'pool.py')
    sources = {name: (Path(__file__).parent / name).read_bytes() for name in names}
    hashes = {name: sha(data) for name, data in sources.items()}
    identity = sha(canonical(hashes))
    directory = Path(log_dir) / ('source_' + identity)
    directory.mkdir(parents=True, exist_ok=True)
    for name, data in sources.items():
        path = directory / name
        if path.exists():
            if path.read_bytes() != data:
                raise ValueError('immutable training source snapshot changed: ' + name)
        else:
            _atomic(path, data)
    manifest_path = directory / 'manifest.json'
    manifest_data = canonical(hashes)
    if manifest_path.exists():
        if manifest_path.read_bytes() != manifest_data:
            raise ValueError('immutable training source manifest changed')
    else:
        _atomic(manifest_path, manifest_data)
    return {'sha256': identity, 'files': hashes,
            'directory': str(directory.relative_to(Path(log_dir).parent))}


def configure_device(torch, requested, mps_memory_limit_gib=None):
    """Resolve the existing auto policy and bound explicitly requested Metal use."""
    if requested not in ('auto', 'cpu', 'cuda', 'mps'):
        raise ValueError('unknown training device')
    device = 'cuda' if requested == 'auto' and torch.cuda.is_available() else 'cpu' if requested == 'auto' else requested
    if device != 'mps' and mps_memory_limit_gib is not None:
        raise ValueError('--mps-memory-limit-gib requires --device mps')
    if device == 'cuda' and not torch.cuda.is_available():
        raise ValueError('CUDA was requested but is unavailable')
    if device != 'mps':
        return device, {'device_name': torch.cuda.get_device_name()} if device == 'cuda' else {}
    if mps_memory_limit_gib is None or not math.isfinite(mps_memory_limit_gib) or mps_memory_limit_gib <= 0:
        raise ValueError('--device mps requires a finite positive --mps-memory-limit-gib; zero disables the Metal allocator limit')
    if not torch.backends.mps.is_available():
        raise ValueError('Metal was requested but is unavailable')
    recommended = torch.mps.recommended_max_memory()
    if mps_memory_limit_gib > recommended / 1024 ** 3:
        raise ValueError('Metal memory limit exceeds the recommended maximum')
    limit = int(mps_memory_limit_gib * 1024 ** 3)
    if not 0 < limit <= recommended:
        raise ValueError('Metal memory limit must be at least one byte and no larger than the recommended maximum')
    fraction = limit / recommended
    torch.mps.set_per_process_memory_fraction(fraction)
    return device, {'mps_allocator_limit_bytes': limit, 'mps_recommended_memory_bytes': recommended,
                    'mps_allocator_fraction': fraction}


def train(args):
    import torch
    from .export_weights import export_model, load_model
    from .model import URoute
    if not (1 <= args.shots <= 16 and args.epochs >= 1 and args.patience >= 1 and args.episodes >= 0
            and args.threads >= 1 and math.isfinite(args.learning_rate) and args.learning_rate > 0):
        raise ValueError("invalid training shots/epochs/patience/episodes/threads/learning-rate")
    if any(not isinstance(top, str) or not top or "\0" in top for top in args.exclude_design):
        raise ValueError("excluded evaluation design names must be nonempty strings")
    torch.set_num_threads(args.threads)
    torch.manual_seed(args.seed)
    rng = random.Random(args.seed)
    device, device_runtime = configure_device(torch, args.device, getattr(args, 'mps_memory_limit_gib', None))
    with training_lock(args.pool) as log_dir:
        dataset = Dataset(args.pool, args.holdout, args.exclude_design, args.validation_task)
        initial = dataset.manifest.get("weights")
        previous = dataset.manifest.get("provenance", {}) if initial and not args.from_scratch else {}
        inherited_designs = set(previous.get("seen_designs", []))
        if args.exclude_design and initial and not args.from_scratch:
            if previous.get("lineage_complete") is not True:
                raise ValueError("initial weights have unknown training lineage; cannot guarantee excluded evaluation designs")
            if inherited_designs.intersection(args.exclude_design):
                raise ValueError("initial weights have already seen an excluded evaluation design")
        if args.from_scratch:
            model, initial_hash = URoute(), None
        elif initial:
            path = inside(args.pool, initial["file"])
            if path.stat().st_size > LIMIT or sha(path.read_bytes()) != initial["sha256"]:
                raise ValueError("initial weight digest mismatch")
            model, initial_hash = load_model(path), initial["sha256"]
        else:
            raise ValueError("no current weights; use --from-scratch to train a new universal model")
        model = model.to(device)
        optimizer = torch.optim.AdamW(model.parameters(), lr=args.learning_rate)
        sampler = ImportanceSampler(dataset.train, getattr(args, 'sampling', 'sqrt-mean-floor'))
        best, best_state, stale = math.inf, None, 0
        config = {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}
        config['sampling'] = sampler.mode
        config['linear_head_loss'] = getattr(args, 'linear_head_loss', False)
        implementation = training_implementation(log_dir)
        runtime = {'python_version': sys.version, 'torch_version': str(torch.__version__),
                   'device': device, 'machine': platform.machine(), 'cuda_version': torch.version.cuda}
        runtime.update(device_runtime)
        run_identity = sha(canonical({"dataset": dataset.identity, "initial_weights": initial_hash, "options": config,
                                      "implementation": implementation['sha256'], "runtime": runtime}))
        started = time.monotonic()
        descriptor, filename = tempfile.mkstemp(prefix='log_' + run_identity + '_', suffix='.jsonl', dir=log_dir)
        log_path = Path(filename)
        input_snapshot = log_path.with_suffix('.input_manifest.json')
        input_manifest = canonical(dataset.manifest)
        with os.fdopen(descriptor, 'w') as log:
            _atomic(input_snapshot, input_manifest)
            for epoch in range(args.epochs):
                model.train()
                epoch_losses = defaultdict(list)
                for _ in range(args.episodes or len(sampler.tasks)):
                    task, probability = sampler.sample(rng)
                    query, target, support = episode(dataset, task, dataset.train[task], args.shots, rng, device, True)
                    optimizer.zero_grad(set_to_none=True)
                    prediction = model(query, support, linear_head=config['linear_head_loss'])
                    loss = torch.nn.functional.mse_loss(prediction, target)
                    raw_loss = float(loss.detach())
                    if not math.isfinite(raw_loss):
                        raise ValueError("nonfinite training loss; weights were not published")
                    (loss / (len(sampler.tasks) * probability)).backward()
                    optimizer.step()
                    sampler.observe(task, raw_loss)
                    epoch_losses[task].append(raw_loss)
                validation_loss, by_task = validate(model, dataset, args.shots, device)
                if not math.isfinite(validation_loss):
                    raise ValueError("nonfinite held-out loss; weights were not published")
                record = {"epoch": epoch, "validation_loss": validation_loss,
                          "sampling": sampler.mode,
                          "linear_head_loss": config['linear_head_loss'],
                          "task_losses": [{"task": list(task), "loss": sum(values) / len(values)}
                                          for task, values in sorted(epoch_losses.items())],
                          "validation_tasks": [{"task": list(task), "loss": value} for task, value in sorted(by_task.items())],
                          "importance": sampler.state(), "seconds": time.monotonic() - started}
                log.write(canonical(record).decode())
                log.flush()
                if validation_loss < best:
                    best, stale = validation_loss, 0
                    best_state = {key: value.detach().cpu().clone() for key, value in model.state_dict().items()}
                else:
                    stale += 1
                if stale >= args.patience:
                    break
        if best_state is None:
            raise ValueError("training produced no validated checkpoint")
        model.cpu().load_state_dict(best_state)
        export_path = log_path.with_name(log_path.name.replace('log_', 'weights_', 1)).with_suffix('.bin')
        export_model(model, export_path)
        provenance = {"run_identity": run_identity, "dataset_identity": dataset.identity, "options": config,
                      "initial_weights_sha256": initial_hash, "holdout_design": args.holdout,
                      "training_designs": sorted({task[0] for task in dataset.train}),
                      "validation_task": args.validation_task, "excluded_designs": sorted(set(args.exclude_design)),
                      "seen_designs": sorted(inherited_designs | {record["top"] for record in dataset.records.values()}),
                      "lineage_complete": args.from_scratch or previous.get("lineage_complete") is True,
                      "sample_ids": sorted(dataset.records), "best_validation_loss": best,
                      "epochs_completed": epoch + 1, "device": device, "torch_version": torch.__version__,
                      "source": "routed-pool", "training_log": str(log_path.relative_to(args.pool)),
                      "training_log_sha256": sha(log_path.read_bytes()),
                      "input_manifest": str(input_snapshot.relative_to(args.pool)),
                      "input_manifest_sha256": sha(input_manifest),
                      "attempt_id": log_path.stem, "implementation": implementation, "runtime": runtime}
        manifest = publish_weights(args.pool, export_path.read_bytes(), trained_on=dataset.identity, provenance=provenance)
        # Keep the specified train/log.jsonl as the latest completed run, while
        # retaining content-specific logs for every published training identity.
        _atomic(log_dir / "log.jsonl", log_path.read_bytes())
        return {"weights": manifest["weights"], "provenance": provenance, "seconds": time.monotonic() - started}


def parse_task(value):
    try:
        task = json.loads(value)
        if (not isinstance(task, list) or len(task) != 3 or not all(isinstance(v, str) and v for v in task[:2])
                or type(task[2]) is not int or task[2] not in (0, 1)):
            raise ValueError("expected [top,technology,0|1]")
        return tuple(task)
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError("validation task must be JSON [top,technology,0|1]") from error


# Training controls shared with cross_validate. Callers that build their own
# namespaces (tests) may omit the newer opt-in controls.
TRAINING_DEFAULTS = {"sampling": "sqrt-mean-floor", "linear_head_loss": False, "mps_memory_limit_gib": None}
TRAINING_CONTROLS = ("shots", "epochs", "episodes", "patience", "learning_rate", "seed", "threads", "device", *TRAINING_DEFAULTS)


def add_training_arguments(parser):
    parser.add_argument("--shots", type=int, default=5)
    parser.add_argument("--sampling", choices=('sqrt-mean-floor', 'paper-rms'), default='sqrt-mean-floor',
                        help='task probability rule; default preserves the original plan, paper-rms follows Eq. 15 and Algorithm 1')
    parser.add_argument('--linear-head-loss', action='store_true',
                        help='opt in to MSE before the final nonnegative clamp; validation and exported inference keep the clamp')
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--episodes", type=int, default=0, help="episodes per epoch; zero uses the task count")
    parser.add_argument("--patience", type=int, default=10)
    parser.add_argument("--learning-rate", type=float, default=.0003)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda", "mps"), default="auto",
                        help='auto preserves CUDA-if-available, otherwise CPU; Metal requires explicit mps')
    parser.add_argument('--mps-memory-limit-gib', type=float,
                        help='required positive Metal allocator cap for --device mps; recorded in provenance')


def training_options(args, **fields):
    """A train() namespace: fields, then the shared training controls of args."""
    controls = {name: getattr(args, name, TRAINING_DEFAULTS[name]) if name in TRAINING_DEFAULTS else getattr(args, name)
                for name in TRAINING_CONTROLS}
    return argparse.Namespace(**fields, **controls)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pool", required=True, type=Path)
    validation = parser.add_mutually_exclusive_group(required=True)
    validation.add_argument("--holdout", help="validation design used for early stopping; this is not an unseen test design")
    validation.add_argument("--validation-task", type=parse_task, help='held-out [top,technology,metric] JSON task; metric 0=utilization, 1=DRC')
    parser.add_argument("--exclude-design", action="append", default=[], help="exclude this evaluation design from both gradients and model selection")
    parser.add_argument("--from-scratch", action="store_true")
    add_training_arguments(parser)
    args = parser.parse_args()
    try:
        output = train(args)
    except (ValueError, OSError, RuntimeError) as exc:
        parser.exit(2, f"uroute train: {exc}\n")
    print(json.dumps(output, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
