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

Values are JSON when they parse (numbers, true/false, lists), else strings. The reply is printed as JSON; the exit
status is 1 when it says "ok": false."""
import argparse
import http.client
import json
import os
import socket
import sys


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
        return args.socket
    if os.environ.get('LHD_GUI_SOCKET'):
        return os.environ['LHD_GUI_SOCKET']
    if args.workdir:
        pointer = os.path.join(args.workdir, 'synth/place/gui_agent.json')
        with open(pointer) as f:
            info = json.load(f)
        try:
            os.kill(info['pid'], 0)
        except ProcessLookupError:
            raise SystemExit(f'the lhd session (pid {info["pid"]}) is not running; {pointer} is stale')
        except PermissionError:
            pass
        return info['socket']
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--socket')
    ap.add_argument('--workdir')
    ap.add_argument('--timeout', type=float, default=30)
    ap.add_argument('command', help='a command (help, find, highlight, critical, selection, ...) or a JSON object')
    ap.add_argument('pairs', nargs='*', metavar='key=value')
    args = ap.parse_args(argv)
    if args.command.lstrip().startswith('{'):
        payload = json.loads(args.command)
    else:
        payload = {'cmd': args.command}
        for pair in args.pairs:
            key, _, value = pair.partition('=')
            payload[key] = parse_value(value)
    reply = request(find_socket(args), payload, args.timeout)
    print(json.dumps(reply, indent=2))
    return 0 if reply.get('ok', True) else 1


if __name__ == '__main__':
    sys.exit(main())
