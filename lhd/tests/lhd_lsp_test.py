#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Scripted JSON-RPC session against `lhd pyrope lsp` (Pyrope LSP over stdio,
# Content-Length framing): initialize negotiates utf-8 + pull diagnostics,
# didOpen/didChange/didSave/didClose manage the in-memory buffer, the pull
# request must report a syntax error for a broken .prp and none after the
# fix, and shutdown/exit must terminate the server with exit code 0.
# Stdlib only.

import json
import os
import subprocess
import sys
import tempfile

LHD = os.environ.get('LHD', 'lhd/lhd')

GOOD = 'comb f(a:u8) -> (z:u8) { z = a }\n'
BAD = 'comb f(a:u8 -> (z:u8) { z = a\n'  # missing ')' and '}'

# 2n Phase C: a file-scope const used inside a comb -> definition on the use
# must answer the declaration statement. The trailing tuple exercises the
# per-field hover render (`pair : tuple(px: u1(...), py: u2(...))`).
MULTI = ('const kk = 33\n'
         'comb g(a:u8) -> (z:u8) {\n'
         '  z = a + kk\n'
         '}\n'
         'const pair = (const px = 1, const py = 2)\n')

# A runtime if/else mux: `sel` takes 1 on one path and 0 on the other, so its
# inferred range must be the UNION [0, 1] — not the textually-last arm's [0, 0]
# (each arm's store REPLACES the range; the bitwidth pass range-unions across
# the arms so a non-constant never reads as bw_min==bw_max, a fold hazard).
MUX = ('comb m(a:u8, b:u8) -> (z:u8) {\n'
       '  mut sel = if a == b { 1 } else { 0 }\n'
       '  z = sel\n'
       '}\n')

# A struct wire: its fields carry declared widths (pc:u64, src:u128) that the
# root `io` never binds as a tuple bundle. Hover on a field access must report
# the field's declared width (u64/u128) — not a bare `int`. Exercises both the
# lsp-decl-hints recovery and the >62-bit width render.
FIELD = ('comb w(a:u64, b:u128) -> (z:u64) {\n'
         '  wire io:(pc:u64, src:u128) = nil\n'
         '  io.pc = a\n'
         '  io.src = b\n'
         '  z = io.pc\n'
         '}\n')


def send(proc, obj):
    body = json.dumps(obj).encode('utf-8')
    proc.stdin.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
    proc.stdin.flush()


def read_msg(proc):
    length = None
    while True:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError('server closed stdout mid-header')
        line = line.strip()
        if not line:
            break
        if line.lower().startswith(b'content-length:'):
            length = int(line.split(b':', 1)[1])
    if length is None:
        raise RuntimeError('missing Content-Length header')
    return json.loads(proc.stdout.read(length))


def read_response(proc, want_id):
    # Skip server-initiated notifications until the response for want_id.
    for _ in range(50):
        msg = read_msg(proc)
        if msg.get('id') == want_id:
            return msg
    raise RuntimeError('no response for id %d' % want_id)


def fail(why):
    print('FAIL: %s' % why, file=sys.stderr)
    sys.exit(1)


def main():
    proc = subprocess.Popen([LHD, 'pyrope', 'lsp'], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    uri = 'file:///lsp_test/broken.prp'

    send(proc, {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
                'params': {'capabilities': {
                    'general': {'positionEncodings': ['utf-8']},
                    'textDocument': {'diagnostic': {}}}}})
    init = read_response(proc, 1)
    caps = init.get('result', {}).get('capabilities', {})
    if caps.get('positionEncoding') != 'utf-8':
        fail('utf-8 position encoding not negotiated: %r' % caps)
    if 'diagnosticProvider' not in caps:
        fail('server did not advertise pull diagnostics: %r' % caps)
    if not caps.get('hoverProvider'):  # task 2n Phase B
        fail('server did not advertise hoverProvider: %r' % caps)
    if not caps.get('definitionProvider'):  # 2n Phase C
        fail('server did not advertise definitionProvider: %r' % caps)
    if not caps.get('declarationProvider'):  # 2n Phase C
        fail('server did not advertise declarationProvider: %r' % caps)
    send(proc, {'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})

    # Broken file -> at least one error diagnostic with a span.
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
                'params': {'textDocument': {'uri': uri, 'languageId': 'pyrope',
                                            'version': 1, 'text': BAD}}})
    send(proc, {'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/diagnostic',
                'params': {'textDocument': {'uri': uri}}})
    diag = read_response(proc, 2)
    items = diag.get('result', {}).get('items', [])
    if not items:
        fail('no diagnostics for a broken .prp buffer')
    first = items[0]
    if first.get('severity') != 1:
        fail('expected severity 1 (error), got %r' % first)
    if 'range' not in first or 'message' not in first:
        fail('diagnostic missing range/message: %r' % first)

    # didChange to the fixed text -> diagnostics clear.
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didChange',
                'params': {'textDocument': {'uri': uri, 'version': 2},
                           'contentChanges': [{'text': GOOD}]}})
    send(proc, {'jsonrpc': '2.0', 'id': 3, 'method': 'textDocument/diagnostic',
                'params': {'textDocument': {'uri': uri}}})
    diag = read_response(proc, 3)
    if diag.get('result', {}).get('items'):
        fail('diagnostics did not clear after didChange fix: %r' % diag)

    # Hover (task 2n Phase B) on the `z` store in GOOD
    # ('comb f(a:u8) -> (z:u8) { z = a }'): z is at line 0, character 25. The
    # server returns the variable's type + range and a covering range.
    send(proc, {'jsonrpc': '2.0', 'id': 6, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 0, 'character': 25}}})
    hov = read_response(proc, 6).get('result')
    if not hov or 'contents' not in hov or 'range' not in hov:
        fail('hover missing contents/range on a name: %r' % hov)
    value = hov.get('contents', {}).get('value', '')
    if 'z' not in value or 'u8' not in value:
        fail('hover did not report z : u8: %r' % hov)
    # z spans its full u8 range, so the render is the plain width with NO
    # bw_min/bw_max (those appear only when the value is narrower than its type;
    # the kk and mux hovers below exercise the narrowed case).
    if 'bw_min=' in value:
        fail('a full-range u8 should render as plain u8, not with bw_min/bw_max: %r' % hov)
    # Hover off any name (column 2, inside the `comb` keyword) -> null.
    send(proc, {'jsonrpc': '2.0', 'id': 7, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 0, 'character': 2}}})
    off = read_response(proc, 7).get('result', 'missing')
    if off is not None:
        fail('hover off a name should be null, got: %r' % off)

    # ── definition / declaration (2n Phase C), same buffer ───────────────────
    # MULTI: `kk` used at line 2 char 10 declares at line 0.
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didChange',
                'params': {'textDocument': {'uri': uri, 'version': 3},
                           'contentChanges': [{'text': MULTI}]}})
    send(proc, {'jsonrpc': '2.0', 'id': 8, 'method': 'textDocument/definition',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 2, 'character': 10}}})
    defs = read_response(proc, 8).get('result')
    if not defs or not isinstance(defs, list):
        fail('definition on a use returned no locations: %r' % defs)
    if defs[0].get('uri') != uri or defs[0]['range']['start']['line'] != 0:
        fail('definition of kk should be line 0 of the same buffer: %r' % defs)
    # gD path: textDocument/declaration answers through the same resolver.
    send(proc, {'jsonrpc': '2.0', 'id': 9, 'method': 'textDocument/declaration',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 2, 'character': 10}}})
    decl = read_response(proc, 9).get('result')
    if not decl or decl[0]['range']['start']['line'] != 0:
        fail('declaration of kk should be line 0: %r' % decl)
    # Hover the same use: the reaching definition's comptime range.
    send(proc, {'jsonrpc': '2.0', 'id': 10, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 2, 'character': 10}}})
    hov = read_response(proc, 10).get('result')
    if not hov or 'bw_min=33' not in hov.get('contents', {}).get('value', ''):
        fail('hover on kk should report bw_min=33: %r' % hov)
    # Definition off any name -> null.
    send(proc, {'jsonrpc': '2.0', 'id': 11, 'method': 'textDocument/definition',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 0, 'character': 2}}})
    if read_response(proc, 11).get('result', 'missing') is not None:
        fail('definition off a name should be null')
    # Tuple hover lists its fields with per-field types.
    send(proc, {'jsonrpc': '2.0', 'id': 15, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 4, 'character': 7}}})
    hov = read_response(proc, 15).get('result')
    value = (hov or {}).get('contents', {}).get('value', '')
    if 'tuple(' not in value or 'px: ' not in value or 'py: ' not in value:
        fail('tuple hover should list per-field types: %r' % hov)

    # ── if/else mux range-union (regression) ────────────────────────────────
    # `mut sel = if a==b { 1 } else { 0 }`: hover on `sel` (line 1, char 6)
    # must report the union range [0, 1], not one arm's value ([0,0] / [1,1]).
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didChange',
                'params': {'textDocument': {'uri': uri, 'version': 4},
                           'contentChanges': [{'text': MUX}]}})
    send(proc, {'jsonrpc': '2.0', 'id': 16, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 1, 'character': 6}}})
    hov = read_response(proc, 16).get('result')
    value = (hov or {}).get('contents', {}).get('value', '')
    if 'bw_min=0, bw_max=1' not in value:
        fail('if/else mux hover should report the union bw_min=0, bw_max=1 '
             '(not a single arm value): %r' % hov)

    # ── struct-wire field width (regression) ────────────────────────────────
    # `z = io.pc` on line 4: hover on `pc` (char 9) must report u64 (the field's
    # declared width), never `int`; the wide u128 field renders too.
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didChange',
                'params': {'textDocument': {'uri': uri, 'version': 5},
                           'contentChanges': [{'text': FIELD}]}})
    send(proc, {'jsonrpc': '2.0', 'id': 17, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 4, 'character': 9}}})
    hov = read_response(proc, 17).get('result')
    value = (hov or {}).get('contents', {}).get('value', '')
    if 'u64' not in value or 'int' in value:
        fail('struct-wire field hover should report the declared width u64, not int: %r' % hov)

    # ── cross-file definition through import() (2n Phase C) ─────────────────
    # A real sibling .prp on disk; the importing buffer is unsaved (didOpen
    # text only) — sibling discovery reads the buffer URI's directory.
    libdir = tempfile.mkdtemp()
    with open(os.path.join(libdir, 'libx.prp'), 'w') as f:
        f.write('pub comb addx(a, b) -> (r) { r = a + b }\n')
    uri2 = 'file://' + os.path.join(libdir, 'main.prp')
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
                'params': {'textDocument': {
                    'uri': uri2, 'languageId': 'pyrope', 'version': 1,
                    'text': 'const lib = import("libx")\n'
                            'const y = lib.addx(a=1, b=2)\n'}}})
    # definition on the `addx` member -> the pub name identifier in libx.prp.
    send(proc, {'jsonrpc': '2.0', 'id': 12, 'method': 'textDocument/definition',
                'params': {'textDocument': {'uri': uri2},
                           'position': {'line': 1, 'character': 15}}})
    defs = read_response(proc, 12).get('result')
    if not defs or not defs[0].get('uri', '').endswith('/libx.prp'):
        fail('definition of an imported member should land in libx.prp: %r' % defs)
    start = defs[0]['range']['start']
    if start['line'] != 0 or start['character'] != 9:
        fail('definition of addx should anchor its name token (0,9): %r' % defs)
    # definition on the import binding -> the imported file first, then the
    # local `const lib = import(...)` statement.
    send(proc, {'jsonrpc': '2.0', 'id': 13, 'method': 'textDocument/definition',
                'params': {'textDocument': {'uri': uri2},
                           'position': {'line': 0, 'character': 7}}})
    defs = read_response(proc, 13).get('result')
    if not defs or len(defs) < 2 or not defs[0].get('uri', '').endswith('/libx.prp') \
            or defs[1].get('uri') != uri2:
        fail('definition of an import binding should list [imported file, local decl]: %r' % defs)
    # hover on the member reports the imported lambda's kind.
    send(proc, {'jsonrpc': '2.0', 'id': 14, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri2},
                           'position': {'line': 1, 'character': 15}}})
    hov = read_response(proc, 14).get('result')
    if not hov or 'comb' not in hov.get('contents', {}).get('value', ''):
        fail('hover on an imported member should report its comb kind: %r' % hov)
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didClose',
                'params': {'textDocument': {'uri': uri2}}})

    # didSave keeps the good text; didClose drops the buffer.
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didSave',
                'params': {'textDocument': {'uri': uri}, 'text': GOOD}})
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didClose',
                'params': {'textDocument': {'uri': uri}}})

    # Unknown request with an id -> MethodNotFound error response.
    send(proc, {'jsonrpc': '2.0', 'id': 4, 'method': 'workspace/bogus',
                'params': {}})
    err = read_response(proc, 4)
    if err.get('error', {}).get('code') != -32601:
        fail('expected MethodNotFound for bogus request: %r' % err)

    send(proc, {'jsonrpc': '2.0', 'id': 5, 'method': 'shutdown'})
    if read_response(proc, 5).get('result', 'missing') is not None:
        fail('shutdown response must carry result null')
    send(proc, {'jsonrpc': '2.0', 'method': 'exit'})
    proc.stdin.close()
    rc = proc.wait(timeout=60)
    if rc != 0:
        fail('server exit code %d after clean shutdown' % rc)

    print('PASS lhd_lsp_test')


if __name__ == '__main__':
    main()
