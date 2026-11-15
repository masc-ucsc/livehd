#!/usr/bin/env python3
"""Prepare a LiveHD placement run from an unpacked PlaceReasoner-Bench design."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys


def clusters(path):
    groups = {}
    group = None
    for line in path.read_text().splitlines():
        header = re.match(r'^\[\s*(\d+)\]\s+', line)
        if header:
            group = 'cluster_' + header[1]
            groups[group] = []
        member = re.match(r'^\s+(\S+)\s+type=', line)
        if member and group:
            # A flat escaped instance may contain a literal hierarchy separator.
            groups[group].append([member[1]])
    return {name: members for name, members in groups.items() if members}


def prepare(args):
    task, work = args.design.resolve(), args.workdir.resolve()
    floorplan = task / 'def' / {'1:1': '1_1_u60_floorplan.def', '2:1': '2_1_floorplan.def'}[args.aspect]
    with floorplan.open() as lines:
        top = next((m[1] for line in lines if (m := re.match(r'^DESIGN\s+(\S+)\s*;', line))), None)
    if not top:
        raise ValueError(f'no DEF DESIGN in {floorplan} (check whether Git LFS objects have been downloaded)')
    metadata = args.metadata.resolve() if args.metadata else None
    if metadata and not metadata.is_file():
        raise ValueError(f'missing design metadata: {metadata}')
    work.mkdir(parents=True, exist_ok=True)
    if metadata is None:
        groups = clusters(task / f'{task.name}_macro_clusters.txt')
        if not groups:
            raise ValueError(f'no macro cluster members in {task}')
        metadata = work / 'clusters.json'
        metadata.write_text(json.dumps([{'class': 'livehd.placement.hints', 'design': top, 'id': 'rtlmp',
                                         'schema_version': 1, 'templates': [{'module': top, 'groups': groups,
                                         'hints': [{'id': name, 'kind': 'cluster', 'group': name} for name in groups]}]}], indent=2) + '\n')
    tech = task / 'lef/NangateOpenCellLibrary.tech.lef'
    lefs = [tech, *sorted(p for p in tech.parent.glob('*.lef') if p != tech)]
    stdlib = task / 'lib/NangateOpenCellLibrary_typical.lib'
    macro_libs = sorted(p for p in stdlib.parent.glob('*.lib') if p != stdlib)
    flags = {'synth.input_mapped_def': floorplan, 'synth.place.policy.fast': 'true', 'compile.slang.liberty_cells': 'true',
             'synth.place': 'true', 'synth.place.io_resynth': '0', 'synth.place.resize': 'false',
             'synth.place.resynth': 'false', 'synth.sdc': task / 'constraint.sdc',
             'synth.lef': ','.join(map(str, lefs)), 'synth.liberty': stdlib,
             'synth.macro_liberty': ','.join(map(str, macro_libs)), 'synth.place.floorplan': floorplan}
    # The reference detailed-placement template reserves a five-micron halo
    # and builds its own PDN. LiveHD's automatic PDN is a different grid.
    # The scorer reopens the original port locations; new IO buffers need access to those same ports.
    flags['synth.place.preserve_imported_io'] = 'true'
    flags['synth.place.halo'] = '5,5'
    flags['synth.place.macro.pdn'] = 'off'
    if args.rc:
        flags['synth.place.tech_rc'] = args.rc.resolve()
    command = [args.lhd, 'synth', str(task / f'{task.name}.v'), str(metadata), '--top', top,
               '--workdir', str(work), '--result-json', str(work/'result.json'), '--emit-dir', 'def:'+str(work/'def')]
    if not args.batch:
        command.extend(['--gui', '--gui-no-open'])
        flags['synth.place.gui.pause'] = 'true'
        if args.flow:
            flags['synth.place.explore.scorer'] = Path(__file__).resolve().with_name('placereason_score.py')
            flags['synth.place.explore.flow'] = args.flow.resolve()
            flags['synth.place.explore.openroad'] = args.openroad
    for key, value in flags.items():
        command.extend(['--set', f'{key}={value}'])
    command.extend(['--', '--std', '1364-2005'])
    print(shlex.join(command), flush=True)
    return subprocess.call(command) if args.run else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('design', type=Path)
    parser.add_argument('--aspect', choices=['1:1', '2:1'], required=True)
    parser.add_argument('--workdir', type=Path, required=True)
    parser.add_argument('--lhd', default='lhd')
    parser.add_argument('--metadata', type=Path, metavar='DESIGN.json',
                        help='use this complete LiveHD design bundle in place of generated benchmark cluster hints')
    parser.add_argument('--flow', type=Path, help='enable GUI T4 validation with this PlaceReasoner-Beta checkout')
    parser.add_argument('--openroad', default='openroad', help='OpenROAD executable for GUI validation')
    parser.add_argument('--rc', type=Path, help='characterized technology RC script')
    parser.add_argument('--batch', action='store_true', help='run the same placement without the GUI')
    parser.add_argument('--run', action='store_true', help='execute the printed command')
    return prepare(parser.parse_args())


if __name__ == '__main__':
    sys.exit(main())
