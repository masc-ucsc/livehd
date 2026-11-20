#!/usr/bin/env python3
"""Talk to a running `lhd synth --gui` from a local process (an assistant, a script) over its Unix-domain socket.

The socket is created with mode 0600 next to the session (`lhd synth --gui` prints it and writes
<workdir>/synth/place/gui_agent.json), so only the user running lhd can use it; no token is needed. What the agent
paints (highlights, selection) shows up in the page at once.

  tools/lhd_gui.py --workdir W help
  tools/lhd_gui.py --workdir W highlight regex='^pc' scope=regs color='#ffd23f' label=pc
  tools/lhd_gui.py --workdir W critical n=20
  tools/lhd_gui.py --workdir W selection            # what the user clicked: name, master, class, size, position, pins
  tools/lhd_gui.py --workdir W describe name=ram0
  tools/lhd_gui.py --workdir W '{"cmd":"cmd","command":{"cmd":"pause"}}'
  tools/lhd_gui.py --workdir W screenshot out=/tmp/fp.png w=900 h=900 layers=macros,comb,regs   # a PNG to look at
  tools/lhd_gui.py --workdir W wait state=gate timeout=300   # until the flow reaches a gate (also phase=, gate=)
  tools/lhd_gui.py --workdir W macros                        # every macro: pose, size, orientation, pinned
  tools/lhd_gui.py --workdir W macro_scan node=ram0 grid='{"nx":12,"ny":12}' top=5   # ranked positions by pin wirelength
  tools/lhd_gui.py --retry 120 --workdir W state             # any command, riding out a restart: the socket is down for a moment

`help` returns an overview (command groups, the usual loop) and the full command reference. Guide:
.agents/skills/livehd/agent_socket.md

Values are JSON when they parse (numbers, true/false, lists), else strings. The reply is printed as JSON; the exit
status is 1 when it says "ok": false."""
import argparse
import http.client
import json
import os
import re
import socket
import sys
import time


class Unix_connection(http.client.HTTPConnection):
    def __init__(self, path, timeout):
        super().__init__('localhost', timeout=timeout)
        self.path = path

    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect(self.path)


def find_socket(args):
    if args.socket:
        # The log line is commonly pasted verbatim, including the usage hint.
        pasted = re.search(r'agent socket \(local, mode 0600\): (.+?\.sock)(?:\s|$)', args.socket)
        return pasted.group(1) if pasted else args.socket
    if os.environ.get('LHD_GUI_SOCKET'):
        return os.environ['LHD_GUI_SOCKET']
    if args.workdir:
        directory = os.path.join(args.workdir, 'synth/place')
        paths = [os.path.join(directory,n) for n in ('chunk_agent.json', 'gui_agent.json')]
        for pointer in sorted((p for p in paths if os.path.isfile(p)), key=os.path.getmtime, reverse=True):
            if os.path.isfile(pointer):
                with open(pointer) as f:
                    info = json.load(f)
                if os.path.exists(info['socket']):
                    try:
                        os.kill(info['pid'], 0)
                    except ProcessLookupError:
                        continue
                    except PermissionError:
                        pass
                    return info['socket']
        raise FileNotFoundError('no live floorplanning socket in ' + directory)
    raise SystemExit('give --socket PATH, --workdir W or set LHD_GUI_SOCKET')


def parse_value(text):
    try:
        return json.loads(text)
    except ValueError:
        return text


def request(path, payload, timeout=30):
    conn = Unix_connection(path, timeout)
    conn.request('POST', '/agent', body=json.dumps(payload), headers={'Content-Type': 'application/json'})
    reply = conn.getresponse().read()
    conn.close()
    return json.loads(reply)


def fetch(path, route, timeout=30):
    """GET a route of the session (the socket serves them without a token): the bytes and the status."""
    conn = Unix_connection(path, timeout)
    conn.request('GET', route)
    response = conn.getresponse()
    body = response.read()
    conn.close()
    return response.status, body


def with_retry(args, action, repeat_read=False):
    """Run action(socket path); with --retry N, ride out the moments the socket is down (a restart or area change
    re-executes lhd, and the pointer file names the new socket)."""
    deadline = time.time() + args.retry
    while True:
        try:
            return action(find_socket(args))
        except (ConnectionRefusedError, FileNotFoundError, ConnectionResetError, http.client.RemoteDisconnected, json.JSONDecodeError) as error:
            if not repeat_read and not isinstance(error, (ConnectionRefusedError, FileNotFoundError)):
                raise
            if time.time() >= deadline:
                raise
            time.sleep(0.5)


def local_command(args):
    """Commands that are a few agent calls or a file download put together; None for a plain agent command."""
    pairs = {}
    for pair in args.pairs:
        key, _, value = pair.partition('=')
        pairs[key] = parse_value(value)
    project = {'project': pairs.pop('project')} if 'project' in pairs else {}
    call = lambda payload: with_retry(args, lambda sock: request(sock, {**project, **payload}, args.timeout))
    if args.command == 'finish-plan':
        path = find_socket(args)
        state = request(path, {'cmd':'state'}, args.timeout)
        if state.get('phase') != 'fp' or state.get('state') != 'gate':
            return dict(ok=False, error='finish-plan requires the floorplan gate')
        return request(path, {'cmd':'cmd','command':{'cmd':'finish'}}, args.timeout)
    if args.command == 'edit' and 'edit' not in pairs:
        options = {k:pairs.pop(k) for k in ('home','stage','names','iterations','hypothesis','scope') if k in pairs}
        return call(dict(cmd='edit', edit=pairs, **options))
    if args.command == 'trial' and 'candidates' not in pairs:
        options = {k:pairs.pop(k) for k in ('home','stage','names','iterations','metric','scope','hypothesis','keep_if','wait') if k in pairs}
        return call(dict(cmd='trial', candidates=pairs, **options))
    if args.command == 'help':
        reply = call({'cmd':'help'})
        if reply.get('ok',True):
            reply['client_examples'] = (['edit align=row side=right fix=x', 'trial iterations=100 left=... right=... keep_if=control', 'overview', 'next objective=density', 'explore kind=density density=0.45 iterations=300 keep=true', 'interfaces apply=false', 'routing', 'resources', 'history', 'place-design']
                if 'usage' in reply else ['connected kind=color limit=3 add_to_selection=true', 'split home=A new_id=B', 'transfer home=B to=A', 'reshape home=B aspect=1.5'])
        return reply
    if args.command == 'connected':
        return call(dict(cmd='export', what='connected', **pairs))
    if args.command in ('chunks', 'members', 'peers', 'pins'):
        what = 'chunks' if args.command == 'chunks' else 'chunk_' + args.command
        return call(dict(cmd='export', what=what, **pairs))
    if args.command in ('split', 'transfer', 'fuse', 'reshape', 'undo-plan'):
        cmd = args.command
        if cmd == 'undo-plan':
            field, op = pairs.pop('field'), 'undo'
        else:
            field = 'geometry' if cmd == 'reshape' else 'topology'
            op = {cmd: pairs.pop('home')}
            if cmd in ('split', 'transfer'):
                op['members'] = pairs.pop('members', 'selected')
                key = 'new_id' if cmd == 'split' else 'to'
                op[key] = pairs.pop(key)
            elif cmd == 'fuse':
                op[cmd] = [op[cmd], pairs.pop('other')]
            elif cmd == 'reshape':
                op['aspect_ratio'] = pairs.pop('aspect')
        apply = pairs.pop('apply', True)
        if pairs:
            return dict(ok=False, error='unknown arguments: ' + ', '.join(pairs))
        # Submit once. A lost reply after submission is ambiguous.
        path = find_socket(args)
        reply = request(path, dict(cmd='chunk_edit', field=field, op=op, apply=apply), args.timeout)
        deadline = time.monotonic() + args.timeout
        while reply.get('status') == 'queued' and time.monotonic() < deadline:
            time.sleep(.025)
            reply = request(path, dict(cmd='command_status', sequence=reply['sequence']), args.timeout)
        if reply.get('status') != 'applied':
            return dict(reply, ok=False)
        return request(path, dict(cmd='export', what='chunks', view='live' if apply else 'preview'), args.timeout)
    if args.command == 'screenshot':
        out = pairs.pop('out', 'lhd_screenshot.png')
        query = '&'.join(f'{k}={v}' for k, v in pairs.items())
        status, body = with_retry(args, lambda sock: fetch(sock, '/screenshot.png' + ('?' + query if query else ''), args.timeout))
        if status != 200 or not body.startswith(b'\x89PNG'):
            return {'ok': False, 'error': f'screenshot failed ({status}): {body[:200].decode("latin1")}'}
        with open(out, 'wb') as f:
            f.write(body)
        return {'ok': True, 'path': os.path.abspath(out), 'bytes': len(body)}
    if args.command == 'wait':
        timeout = float(pairs.pop('timeout', 300))
        deadline = time.time() + timeout
        while True:
            try:
                state = with_retry(args, lambda sock: request(sock, {'cmd': 'state'}, args.timeout))
            except (ConnectionRefusedError, FileNotFoundError, ConnectionResetError, http.client.RemoteDisconnected, json.JSONDecodeError, SystemExit):
                state = {}  # restarting
            if state and all(state.get(k) == v for k, v in (pairs or {'state': 'gate'}).items()):
                keys = ('state', 'phase', 'gate', 'iter', 'frame', 'hpwl', 'wns', 'tns', 'paused', 'finished', 'note', 'error')
                return {'ok': True, **{k: state.get(k) for k in keys}}
            if state.get('error'):
                return {'ok': False, 'error': state['error'], 'state': state.get('state')}
            if time.time() >= deadline:
                return {'ok': False, 'error': f'timed out after {timeout:g}s waiting for {pairs or {"state": "gate"}}', 'state': state.get('state'), 'phase': state.get('phase')}
            time.sleep(1)
    if args.command == 'macros' and call({'cmd':'hello'}).get('mode') == 'chunk':
        return call(dict(cmd='find', kind='macro', **pairs))
    if args.command == 'macros':
        listing = call({'cmd': 'export', 'what': 'macros'})
        if not listing.get('ok'):
            return listing
        rows = []
        for m in listing.get('macros', []):
            node = call({'cmd': 'describe', 'name': m['name']}).get('node', {})
            rows.append({'name': m['name'], 'x': m['x'], 'y': m['y'], 'orient': m['orient'],
                         **{k: node[k] for k in ('master', 'w_um', 'h_um', 'pins', 'pinned', 'movable') if k in node}})
        return {'ok': True, 'units': listing.get('units'), 'dbu_per_um': listing.get('dbu_per_um'), 'frame': listing.get('frame'),
                'core': listing.get('core_dbu'), 'macros': rows}
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--socket')
    ap.add_argument('--workdir')
    ap.add_argument('--timeout', type=float, default=30)
    ap.add_argument('--retry', type=float, default=0, help='seconds to keep retrying while the socket is down (a restart)')
    ap.add_argument('command', help='a command (help, find, highlight, critical, selection, ...) or a JSON object')
    ap.add_argument('pairs', nargs='*', metavar='key=value')
    args = ap.parse_args(argv)
    try:
        local = local_command(args)
    except KeyError as missing:
        ap.error(f'{args.command} requires {missing.args[0]}=VALUE')
    if local is not None:
        print(json.dumps(local, indent=2))
        return 0 if local.get('ok', True) else 1
    if args.command.lstrip().startswith('{'):
        payload = json.loads(args.command)
    else:
        payload = {'cmd': args.command}
        for pair in args.pairs:
            key, _, value = pair.partition('=')
            payload[key] = parse_value(value)
    reply = with_retry(args, lambda sock: request(sock, payload, args.timeout))
    print(json.dumps(reply, indent=2))
    return 0 if reply.get('ok', True) else 1


if __name__ == '__main__':
    sys.exit(main())
