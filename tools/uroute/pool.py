# SPDX-License-Identifier: BSD-3-Clause
"""Atomic offline ownership of a project pool. LiveHD only reads this directory.

Samples and weights are immutable, content-named objects. Writers serialize the
manifest update, install new objects first, and replace manifest.json last.
"""
from contextlib import contextmanager
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import tempfile
import time

from .export_weights import decode_tensors
from .features import ARCH_VERSION, FEATURE_VERSION, LABEL_VERSION
from .maps import encode

VERSIONS = {"feature_version": FEATURE_VERSION, "label_version": LABEL_VERSION, "arch_version": ARCH_VERSION}


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    """Hash physical checkpoints without materializing a multi-GB byte string."""
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate manifest field: {key}")
        result[key] = value
    return result


def read_manifest(root):
    path = Path(root) / "manifest.json"
    if not path.exists():
        return dict(VERSIONS, weights=None, trained_on="", samples=[])
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("oversized URoute manifest")
    doc = json.loads(path.read_bytes(), object_pairs_hook=_object)
    if any(type(doc.get(key)) is not int or doc[key] != value for key, value in VERSIONS.items()):
        raise ValueError("unsupported URoute feature/label/architecture version")
    if not isinstance(doc.get("samples"), list) or len(doc["samples"]) > 100000:
        raise ValueError("invalid URoute sample pool")
    ids = [entry["id"] for entry in doc["samples"]]
    if len(set(ids)) != len(ids):
        raise ValueError("duplicate URoute sample id")
    return doc


@contextmanager
def _writer(root):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    with (root / ".update.lock").open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        yield root


def _atomic(path, data):
    descriptor, name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def _identity(metadata):
    for name in ("top", "technology"):
        if not isinstance(metadata.get(name), str) or not metadata[name] or "\0" in metadata[name]:
            raise ValueError(f"invalid URoute sample {name}")
    fp = metadata["floorplan"]
    for name in ("die_width", "die_height"):
        if not isinstance(fp.get(name), (int, float)) or not math.isfinite(fp[name]) or fp[name] <= 0:
            raise ValueError(f"invalid floorplan {name}")
    if not isinstance(fp.get("macro_signature"), str) or "\0" in fp["macro_signature"]:
        raise ValueError("invalid floorplan macro_signature")
    if not isinstance(fp.get("utilization"), (int, float)) or not math.isfinite(fp["utilization"]) or not 0 <= fp["utilization"] <= 1:
        raise ValueError("invalid floorplan utilization")
    if type(metadata.get("created_at")) is not int or metadata["created_at"] < 0:
        raise ValueError("created_at must be a nonnegative integer timestamp")
    if metadata.get("status") not in ("routed", "unrouted"):
        raise ValueError("status must be routed or unrouted")
    if metadata.get("source") not in ("livehd", "external"):
        raise ValueError("source must be livehd or external")


def _shape(maps, channels, normalized):
    if (maps.channels, maps.bins_x, maps.bins_y, maps.normalized) != (channels, 256, 256, normalized):
        raise ValueError(f"expected {channels}x256x256 maps with normalized={normalized}")


def _routed_pair_identity(metadata):
    # DEF and ODB ingestion may describe the same physical routed result while
    # adding different auxiliary reports. Those are not independent support
    # shots. Keep the first immutable sample and its original provenance.
    if not isinstance(metadata.get("routed_def_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", metadata["routed_def_sha256"]):
        return None
    # The control file includes provenance (for example the source QoR hash),
    # not just clock/reset roles. A provenance-only change is an observation
    # when the routed DEF and both effective input/label maps are identical.
    # Keep the full controls digest in sample/observation metadata for audit.
    names = ("top", "technology", "source", "status", "floorplan", "routed_def_sha256", "dbu_per_micron",
             "core", "route_grid", "network_grid", "liberty_sha256", "features_sha256",
             "labels_sha256", *VERSIONS)
    result = {name: metadata.get(name) for name in names}
    if metadata["status"] == "unrouted":
        result["last_global_overflow_sha256"] = metadata["files"].get("last_global_overflow.bin")
    return result


def _verify_immutable_sample(root, record):
    directory = Path(record["directory"])
    if directory.is_absolute() or ".." in directory.parts:
        raise ValueError("immutable sample directory escapes the pool")
    directory = (root / directory).resolve()
    if not directory.is_relative_to(root.resolve()):
        raise ValueError("immutable sample directory escapes the pool")
    saved = json.loads((directory / "sample.json").read_bytes(), object_pairs_hook=_object)
    if saved != record:
        raise ValueError("immutable URoute sample metadata was modified")
    for name, digest in record["files"].items():
        relative = Path(name)
        path = (directory / relative).resolve()
        if relative.is_absolute() or ".." in relative.parts or not path.is_relative_to(directory):
            raise ValueError("immutable sample file escapes the pool")
        if file_sha(path) != digest:
            raise ValueError("immutable URoute sample was modified")
    return directory


def _publish_observation(root, manifest, previous, metadata, files):
    current = {key: value for key, value in metadata.items() if key != "created_at"}
    original = {key: value for key, value in previous.items() if key not in ("id", "directory", "created_at")}
    if canonical(current) == canonical(original):
        return previous
    changed = {name: data for name, data in files.items()
               if name not in ("features.bin", "labels.bin", "last_global_overflow.bin", "sample.json")
               and previous["files"].get(name) != sha(data)}
    payload = {"observation_version": 1, "sample_id": previous["id"], "sample_directory": previous["directory"],
               "metadata": current, "files": {name: sha(data) for name, data in sorted(changed.items())}}
    digest = sha(canonical(payload))
    parent = root / "observations"
    parent.mkdir(exist_ok=True)
    if parent.resolve().parent != root.resolve():
        raise ValueError("observation directory escapes the pool")
    destination = parent / digest
    objects = dict(changed, **{"observation.json": canonical(payload)})
    if destination.exists():
        if any((destination / name).read_bytes() != data for name, data in objects.items()):
            raise ValueError("immutable URoute observation was modified")
    else:
        temporary = Path(tempfile.mkdtemp(prefix=".observation_", dir=parent))
        try:
            for name, data in objects.items():
                path = temporary / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            os.rename(temporary, destination)
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    observations = manifest.setdefault("observations", [])
    if not isinstance(observations, list):
        raise ValueError("invalid URoute observations list")
    entry = next((entry for entry in observations if entry["id"] == digest), None)
    if entry is not None and (entry.get("sample_id") != previous["id"] or entry.get("directory") != "observations/" + digest):
        raise ValueError("immutable URoute observation metadata was modified")
    if entry is None:
        entry = {"id": digest, "sample_id": previous["id"], "directory": "observations/" + digest,
                 "created_at": metadata["created_at"]}
        observations.append(entry)
        _atomic(root / "manifest.json", canonical(manifest))
    return dict(previous, observation=entry)


def publish_sample(root, metadata, features, labels=None, *, last_global_overflow=None, overflow_unavailable=None, extra_files=None):
    """Install one sample without losing a concurrent writer's pool updates."""
    metadata = dict(metadata)
    metadata.setdefault("created_at", time.time_ns())
    if any(key in metadata and (type(metadata[key]) is not int or metadata[key] != value) for key, value in VERSIONS.items()):
        raise ValueError("unsupported sample feature/label/architecture version")
    metadata.update(VERSIONS)
    _identity(metadata)
    if overflow_unavailable is not None:
        if (metadata["status"] != "unrouted" or last_global_overflow is not None or not isinstance(overflow_unavailable, str)
                or not overflow_unavailable.strip() or len(overflow_unavailable) > 4096 or "\0" in overflow_unavailable):
            raise ValueError("overflow unavailability requires an explicit nonempty reason on an unrouted sample without a map")
        metadata["last_global_overflow_status"] = "unavailable"
        metadata["last_global_overflow_reason"] = overflow_unavailable
    _shape(features, 4, True)
    files = {"features.bin": encode(features)}
    if metadata["status"] == "routed":
        if labels is None:
            raise ValueError("routed samples require labels")
        _shape(labels, 3, False)
        files["labels.bin"] = encode(labels)
    else:
        if last_global_overflow is None and overflow_unavailable is None:
            raise ValueError("failed routes require the last global-route overflow map or an explicit unavailability reason")
        if last_global_overflow is not None:
            metadata["last_global_overflow_status"] = "stored"
            files["last_global_overflow.bin"] = encode(last_global_overflow)
    for name, data in (extra_files or {}).items():
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or name in files or name in ("sample.json", "observation.json", "features.bin", "labels.bin", "last_global_overflow.bin"):
            raise ValueError("invalid extra sample file path")
        files[name] = data
    metadata["features_sha256"] = sha(files["features.bin"])
    metadata["labels_sha256"] = sha(files["labels.bin"]) if "labels.bin" in files else ""
    metadata["files"] = {name: sha(data) for name, data in sorted(files.items())}
    # Re-ingesting the same routed result must not create extra support shots
    # merely because the update script ran at another time.
    identity = {key: value for key, value in metadata.items() if key != "created_at"}
    content_hash = sha(canonical(identity))
    prefix = re.sub(r"[^A-Za-z0-9_.-]", "_", metadata["top"] + "__" + metadata["technology"])[:100]
    sample_id = prefix + "__" + content_hash
    entry = dict(metadata, id=sample_id, directory="pool/" + sample_id)
    files["sample.json"] = canonical(entry)
    with _writer(root) as root:
        manifest = read_manifest(root)
        pair = _routed_pair_identity(metadata)
        if pair is not None:
            matches = [record for record in manifest["samples"] if _routed_pair_identity(record) == pair]
            if matches:
                previous = min(matches, key=lambda record: (record["created_at"], record["id"]))
                directory = _verify_immutable_sample(root, previous)
                primary = "labels.bin" if metadata["status"] == "routed" else "last_global_overflow.bin"
                for name in ["features.bin"] + ([primary] if primary in files else []):
                    if (directory / name).read_bytes() != files[name]:
                        raise ValueError("routed-pair digest collision or modified sample")
                return _publish_observation(root, manifest, previous, metadata, files)
        pool = root / "pool"
        pool.mkdir(exist_ok=True)
        destination = pool / sample_id
        if destination.exists():
            previous = json.loads((destination / "sample.json").read_bytes(), object_pairs_hook=_object)
            repeated = dict(entry, created_at=previous["created_at"])
            if repeated != previous:
                raise ValueError("immutable URoute sample metadata was modified")
            entry = previous
            files["sample.json"] = canonical(entry)
            for name, data in files.items():
                if (destination / name).read_bytes() != data:
                    raise ValueError("immutable URoute sample was modified")
        else:
            temporary = Path(tempfile.mkdtemp(prefix=".sample_", dir=pool))
            try:
                for name, data in files.items():
                    path = temporary / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                os.rename(temporary, destination)
            finally:
                if temporary.exists():
                    shutil.rmtree(temporary)
        if not any(old["id"] == sample_id for old in manifest["samples"]):
            manifest["samples"].append(entry)
        _atomic(root / "manifest.json", canonical(manifest))
    return entry


def publish_weights(root, data, *, trained_on, provenance):
    """Publish a validated flat export and retain the exact training provenance."""
    decode_tensors(data)
    if not isinstance(trained_on, str) or not trained_on or not isinstance(provenance, dict):
        raise ValueError("weights require a training identity and provenance")
    digest = sha(data)
    name = f"weights_{digest}.bin"
    with _writer(root) as root:
        manifest = read_manifest(root)
        path = root / name
        if path.exists():
            if path.read_bytes() != data:
                raise ValueError("immutable URoute weights were modified")
        else:
            _atomic(path, data)
        manifest.update(weights={"file": name, "sha256": digest}, trained_on=trained_on, provenance=provenance)
        _atomic(root / "manifest.json", canonical(manifest))
    return manifest
