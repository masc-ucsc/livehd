#!/usr/bin/env python3
"""Elaborate tiny legacy exports and compare sampled outputs/state with DesignCert.

Arguments: LEGACY_EXPORT_ROOT VERIFIED_EXPORT_ROOT RUNTIME_DIR ENV_JSON.
Roots are compare_exports.py outputs. ENV_JSON supplies PATH and LEAN_PATH.
Only use tiny fixtures: state fields follow their one-memory/one-flop ordinals.
"""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import sys

legacy, verified, runtime, env_file = map(Path, sys.argv[1:])
runtime.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env.update(json.loads(env_file.read_text()))
rows = json.loads((legacy / 'results.json').read_text())


def check(row):
    top = row['top']
    if 'sha256' not in row:
        return {'top': top, 'status': 'refused'}
    file = legacy / row['group'] / top / 'export' / (top + '_Lgraph.lean')
    text = file.read_text()
    vtext = (verified / row['group'] / top / 'export' / (top + '_Lgraph.lean')).read_text()
    compiler_refusal = any(op in vtext for op in ('LGraphOp.Op_UDiv', 'LGraphOp.Op_SetMask', 'LGraphOp.Op_Const'))
    if compiler_refusal:
        # These are intentional CompileOp refusals on the baseline branch.
        # Retain its DesignCert and use interpretDesign as the independent oracle.
        vtext = vtext[:vtext.index('/-- Compile-and-run.')]
        vtext += f'\ndef {top}_step := interpretDesign {top}_designCert\n'
        vtext += f'example : compilesOk {top}_designCert = false := by native_decide\n'
    imports = ['import LeanSemanticPrimitives.Translation.OpBridge',
               'import LeanSemanticPrimitives.Compiler.CompileDesign']
    imports += re.findall(r'^import .*$', text, re.M)
    imports = list(dict.fromkeys(imports))
    text = re.sub(r'^import .*\n', '', text, flags=re.M)
    vtext = re.sub(r'^import .*\n', '', vtext, flags=re.M)

    def fields(role):
        match = re.search(r'structure ' + top + '_' + role + r' where\n(.*?)deriving', text, re.S)
        return re.findall(r'^  (\w+) : (.*)$', match[1], re.M) if match else []

    inp, state, output = fields('in'), fields('state'), fields('out')
    word_state = [(f, t) for f, t in state if '->' not in t]
    mem_state = [(f, t) for f, t in state if '->' in t]
    in_values = [f'{f} := BitVec.ofNat {re.search(r"\d+", t)[0]} (seed + {i})' for i, (f, t) in enumerate(inp)]
    state_values = []
    for i, (f, t) in enumerate(state):
        nums = re.findall(r'\d+', t)
        value = f'BitVec.ofNat {nums[-1]} (seed + {i})'
        if '->' in t:
            value = f'fun a => BitVec.ofNat {nums[-1]} (seed + {i} + a.toNat)'
        state_values.append(f'{f} := {value}')
    oracle = ['open OpBridge Compiler', f'namespace {top}_Lgraph',
              f'def oracle_in (seed : Nat) : {top}_in := {{ ' + ', '.join(in_values) + ' }']
    if state:
        oracle += [f'def oracle_state (seed : Nat) : {top}_state := {{ ' + ', '.join(state_values) + ' }']
    runtime_inputs = ', '.join(f'bvenc (oracle_in seed).{f}' for f, _ in inp if f != 'in_dummy')
    runtime_flops = ', '.join(f'bvenc (oracle_state seed).{f}' for f, _ in word_state)
    runtime_mems = ', '.join(f'memenc (oracle_state seed).{f}' for f, _ in mem_state)
    oracle += [f'def oracle_result (seed : Nat) := _root_.{top}_step #[{runtime_inputs}] ⟨#[{runtime_flops}], #[{runtime_mems}]⟩']
    args = '(oracle_in seed)' + (' (oracle_state seed)' if state else '')
    checks = []
    for i, (f, _) in enumerate(output):
        if f == 'out_dummy':
            continue
        fast = f'({top}_comb {args}).{f}'
        cert = f'({top}_comb_cert {args}).{f}'
        compiled = f'(oracle_result seed).outputs[{i}]!'
        checks += [f'({fast} == {cert})', f'(bv_uint (bvenc {fast}) == bv_uint {compiled})']
    for i, (f, _) in enumerate(word_state):
        fast = f'({top}_next {args}).{f}'
        cert = f'({top}_next_cert {args}).{f}'
        compiled = f'(oracle_result seed).nextState.flops[{i}]!'
        checks += [f'({fast} == {cert})', f'(bv_uint (bvenc {fast}) == bv_uint {compiled})']
    for i, (f, t) in enumerate(mem_state):
        aw = re.findall(r'\d+', t)[0]
        fast = f'({top}_next {args}).{f} (BitVec.ofNat {aw} addr)'
        cert = f'({top}_next_cert {args}).{f} (BitVec.ofNat {aw} addr)'
        compiled = f'((oracle_result seed).nextState.mems[{i}]! (Int.ofNat addr))'
        checks += [f'((List.range (2^{aw})).all fun addr => ({fast} == {cert}) && (bv_uint (bvenc ({fast})) == bv_uint {compiled}))']
    # Values above the tiny 8-bit sign boundary and zero as well as low values.
    seeds = '[0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 63, 127, 128, 129, 171, 255]'
    oracle += [f'example : ({seeds} : List Nat).all (fun seed => ' + ' && '.join(checks or ['true']) + ') = true := by native_decide',
               f'end {top}_Lgraph']
    path = runtime / (top + '_oracle.lean')
    path.write_text('\n'.join(imports) + '\n' + text + '\n' + vtext + '\n' + '\n'.join(oracle) + '\n')
    try:
        proc = subprocess.run(['lean', str(path)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        log = proc.stdout.decode(errors='replace')
        status = proc.returncode
        if 'sorryAx' in log:
            status = 'sorryAx'
    except subprocess.TimeoutExpired:
        log, status = 'timeout', 'timeout'
    (runtime / (top + '.log')).write_text(log)
    print(top, status, flush=True)
    return {'top': top, 'status': status, 'compiler_refusal': compiler_refusal}


with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    results = list(pool.map(check, rows))
(runtime / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
if any(r['status'] not in (0, 'refused') for r in results):
    sys.exit(1)
