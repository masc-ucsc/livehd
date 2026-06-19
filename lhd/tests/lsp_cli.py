#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Manual command-line driver for `lhd pyrope lsp` (the Pyrope LSP server over
# stdio, Content-Length framing). Fire ONE LSP feature request at a .prp buffer
# and print the raw JSON response. Stdlib only.
#
# This is the interactive twin of the automated regression in
# lhd/tests/lhd_lsp_test.py: use it to poke hover / definition / references /
# documentSymbol / workspaceSymbol by hand while implementing task 2n phases
# B-D (see todo/livehd/2n.html "Command-line testing").
#
# Usage:
#   python3 lhd/tests/lsp_cli.py FILE.prp diagnostic
#   python3 lhd/tests/lsp_cli.py FILE.prp documentSymbol
#   python3 lhd/tests/lsp_cli.py FILE.prp hover       LINE COL
#   python3 lhd/tests/lsp_cli.py FILE.prp definition  LINE COL
#   python3 lhd/tests/lsp_cli.py FILE.prp references  LINE COL
#   python3 lhd/tests/lsp_cli.py FILE.prp workspaceSymbol QUERY
#
# LINE and COL are 0-based (LSP wire coordinates: line 0 is the first line).
# The lhd binary is taken from $LHD, else ./bazel-bin/lhd/lhd.

import json
import os
import subprocess
import sys

LHD = os.environ.get('LHD', 'bazel-bin/lhd/lhd')

# verb -> (wire method, needs_position, builds the params dict)
#   uri:  file:// uri of the opened buffer
#   pos:  {"line": L, "character": C} or None
#   extra: trailing positional (workspaceSymbol query), or None
METHODS = {
    'diagnostic':      ('textDocument/diagnostic',     False),
    'documentSymbol':  ('textDocument/documentSymbol', False),
    'hover':           ('textDocument/hover',          True),
    'definition':      ('textDocument/definition',     True),
    'references':      ('textDocument/references',      True),
    'workspaceSymbol': ('workspace/symbol',            False),
}


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
    for _ in range(50):  # skip server-initiated notifications (publishDiagnostics)
        msg = read_msg(proc)
        if msg.get('id') == want_id:
            return msg
    raise RuntimeError('no response for id %d' % want_id)


def usage(why):
    print('lsp_cli: %s' % why, file=sys.stderr)
    print(__doc__.split('Usage:', 1)[1].rstrip(), file=sys.stderr)
    sys.exit(2)


def main():
    argv = sys.argv[1:]
    if len(argv) < 2:
        usage('need FILE.prp and a verb')
    path, verb = argv[0], argv[1]
    if verb not in METHODS:
        usage("unknown verb '%s' (one of: %s)" % (verb, ', '.join(METHODS)))
    method, needs_pos = METHODS[verb]

    pos = None
    query = None
    if needs_pos:
        if len(argv) < 4:
            usage('%s needs LINE COL (0-based)' % verb)
        pos = {'line': int(argv[2]), 'character': int(argv[3])}
    elif verb == 'workspaceSymbol':
        query = argv[2] if len(argv) > 2 else ''

    try:
        text = open(path, 'r', encoding='utf-8').read()
    except OSError as e:
        usage('cannot read %s: %s' % (path, e))
    uri = 'file://' + os.path.abspath(path)

    proc = subprocess.Popen([LHD, 'pyrope', 'lsp'], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

    # Lifecycle: negotiate utf-8 + pull diagnostics so a single buffer is enough.
    send(proc, {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
                'params': {'capabilities': {
                    'general': {'positionEncodings': ['utf-8']},
                    'textDocument': {'diagnostic': {}}}}})
    init = read_response(proc, 1)
    caps = init.get('result', {}).get('capabilities', {})
    print('=== capabilities ===')
    print(json.dumps(caps, indent=2))
    send(proc, {'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})

    # Open the buffer so the server has text to analyze.
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
                'params': {'textDocument': {'uri': uri, 'languageId': 'pyrope',
                                            'version': 1, 'text': text}}})

    # The feature request.
    params = {}
    if verb == 'workspaceSymbol':
        params = {'query': query}
    else:
        params = {'textDocument': {'uri': uri}}
        if pos is not None:
            params['position'] = pos
        if verb == 'references':
            params['context'] = {'includeDeclaration': True}
    send(proc, {'jsonrpc': '2.0', 'id': 2, 'method': method, 'params': params})
    resp = read_response(proc, 2)
    print('=== %s ===' % method)
    print(json.dumps(resp, indent=2))

    send(proc, {'jsonrpc': '2.0', 'id': 3, 'method': 'shutdown'})
    read_response(proc, 3)
    send(proc, {'jsonrpc': '2.0', 'method': 'exit'})
    proc.stdin.close()
    proc.wait(timeout=60)

    # A MethodNotFound error means the feature is not implemented yet.
    if resp.get('error', {}).get('code') == -32601:
        sys.exit(1)


if __name__ == '__main__':
    main()
