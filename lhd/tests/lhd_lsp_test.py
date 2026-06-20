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

LHD = os.environ.get('LHD', 'lhd/lhd')

GOOD = 'comb f(a:u8) -> (z:u8) { z = a }\n'
BAD = 'comb f(a:u8 -> (z:u8) { z = a\n'  # missing ')' and '}'


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
        fail('hover did not report z : u8(...): %r' % hov)
    # Hover off any name (column 2, inside the `comb` keyword) -> null.
    send(proc, {'jsonrpc': '2.0', 'id': 7, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': uri},
                           'position': {'line': 0, 'character': 2}}})
    off = read_response(proc, 7).get('result', 'missing')
    if off is not None:
        fail('hover off a name should be null, got: %r' % off)

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
