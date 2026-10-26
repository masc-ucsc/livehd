# SPDX-License-Identifier: BSD-3-Clause
"""Explicit external OpenROAD ODB export; never used by native inference."""
import gzip
import math
import os
from pathlib import Path
import shutil
import subprocess

from .features import grid
from .maps import raw, write
from .pool import canonical, file_sha


def overflow_map(path):
    if Path(path).stat().st_size > 64 * 1024 * 1024:
        raise ValueError('oversized ODB overflow export')
    with Path(path).open() as stream:
        if stream.readline().strip() != 'UROUTE_GRT1':
            raise ValueError('unsupported ODB congestion export')
        dimensions = stream.readline().split()
        summary = stream.readline().split()
        if len(dimensions) != 7 or len(summary) != 7:
            raise ValueError('invalid ODB congestion header')
        nx, ny = map(int, dimensions[:2])
        grid(nx, ny)
        dbu, lx, ly, hx, hy = map(float, dimensions[2:])
        gx, gy, layers = map(int, summary[:3])
        capacity, usage, overflow = map(float, summary[3:6])
        positive = int(summary[6])
        if (not all(map(math.isfinite, (dbu, lx, ly, hx, hy, capacity, usage, overflow))) or dbu <= 0
                or hx <= lx or hy <= ly or min(gx, gy, layers) < 1 or capacity <= 0
                or min(usage, overflow, positive) < 0 or positive > gx * gy * layers
                or (positive == 0) != (overflow == 0) or overflow > usage + 1e-6 * max(1., usage)):
            raise ValueError('missing/invalid ODB global-route resource census')
        values = []
        for x in range(nx):
            for y in range(ny):
                row = stream.readline().split()
                if len(row) != 3 or (int(row[0]), int(row[1])) != (x, y):
                    raise ValueError('incomplete/unordered ODB overflow grid')
                value = float(row[2])
                if not math.isfinite(value) or value < 0:
                    raise ValueError('invalid ODB overflow value')
                values.append(value)
        if overflow == 0 and any(values):
            raise ValueError('zero resource overflow disagrees with exported bins')
        if stream.read().strip():
            raise ValueError('trailing ODB overflow data')
    maps = raw([values], nx, ny)
    metadata = {'source': 'OpenDB dbGCellGrid usage/capacity', 'dbu_per_micron': dbu,
                'core': [lx, ly, hx, hy], 'source_grid': [gx, gy], 'layers': layers,
                'capacity_sum': capacity, 'usage_sum': usage, 'overflow_tracks_sum': overflow,
                'positive_layer_gcells': positive,
                'units': 'sum across layers of positive usage-minus-capacity tracks per source GCell, area-averaged onto the core grid',
                'purpose': 'stored global-route resource diagnostics for an unrouted sample; not a detailed-wire training label',
                'limitations': 'OpenDB per-layer resources may not capture transient 2D routing demand or the failing iteration; zero stored overflow does not imply successful routing'}
    return maps, metadata


def export_odb(source, executable, output, *, routed=True, overflow=False, timeout=300):
    source, executable, output = Path(source).resolve(), Path(executable).resolve(), Path(output).resolve()
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('ODB export timeout must be finite and positive')
    output.mkdir(parents=True, exist_ok=True)
    input_path = source
    if source.suffix == '.gz':
        input_path = output / 'input.odb'
        with gzip.open(source, 'rb') as src, input_path.open('wb') as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
    definition, wirelength, raw_overflow = output / 'physical.def', output / 'wirelength.txt', output / 'overflow.tsv'
    environment = dict(os.environ, LHD_UROUTE_INPUT_ODB=str(input_path), LHD_UROUTE_OUTPUT_DEF=str(definition),
                       LHD_UROUTE_OUTPUT_WIRELENGTH=str(wirelength) if routed else '',
                       LHD_UROUTE_OUTPUT_OVERFLOW=str(raw_overflow) if overflow else '')
    log = output / 'openroad.log'
    with log.open('wb') as stream:
        result = subprocess.run([str(executable), '-no_init', '-exit', '-threads', '1', str(Path(__file__).with_name('odb.tcl').resolve())],
                                env=environment, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    # Tool logs are not guaranteed UTF-8; a stray byte must not fail a good run.
    text = log.read_text(errors='replace')
    if result.returncode != 0 or not definition.is_file() or 'UROUTE_ODB_EXPORT_OK' not in text:
        detail = '\n'.join(text.splitlines()[-16:])
        raise ValueError(f'OpenROAD ODB export failed: {detail}')
    metadata = {'source_odb': source.name, 'source_odb_sha256': file_sha(source),
                'openroad_binary_sha256': file_sha(executable), 'adapter_version': 1,
                'adapter_tcl_sha256': file_sha(Path(__file__).with_name('odb.tcl')),
                'routed_wirelength_report': routed, 'global_overflow_exported': overflow}
    if overflow:
        maps, detail = overflow_map(raw_overflow)
        write(output / 'last_global_overflow.bin', maps)
        metadata['global_overflow'] = detail
    (output / 'odb_export.json').write_bytes(canonical(metadata))
    return definition, wirelength if routed else None, output / 'last_global_overflow.bin' if overflow else None, metadata
