#!/usr/bin/env python3
"""Index replayable macro animations from explicitly supplied placement directories."""
import argparse
from datetime import datetime, timezone
import html
import json
import os
from pathlib import Path
import time
from urllib.parse import quote


def render(output, entries):
    rows = []
    ready = []
    for label, directory in entries:
        report = None
        # The staged flow writes macro/ first, then copies its replay to the
        # parent after final cell placement. Publish the earlier copy too.
        for source in (directory, directory / 'macro'):
            animation_path = source / 'macro_animation.html'
            try:
                text = animation_path.read_text()
                candidate = json.loads((source / 'magnet.json').read_text())
                # The producer writes JSON before HTML. Do not link a partial page.
                if text.rstrip().endswith('</html>') and candidate.get('frames'):
                    report = candidate
                    break
            except (OSError, ValueError):
                pass
        cells = [html.escape(label)]
        if report is None:
            cells += ['Awaiting macro-stage playback', '—', '—', '—']
        else:
            ready.append(label)
            link = quote(os.path.relpath(animation_path, output.parent), safe='/')
            cells += [f'<a href="{link}">Replay animation</a>', str(len(report['frames'])),
                      str(len(report['frames'][0]['macros'])), str(len(report['groups']))]
        rows.append('<tr>' + ''.join(f'<td>{value}</td>' for value in cells) + '</tr>')
    stamp = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')
    refresh = '<meta http-equiv="refresh" content="30">' if len(ready) < len(entries) else ''
    page = f'''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">{refresh}
<title>Macro placement playback</title><style>
body{{font:16px/1.5 system-ui;margin:2rem;background:#101925;color:#e5edf5}}
a{{color:#94caff}}table{{border-collapse:collapse}}td,th{{text-align:left;padding:.65rem;border-bottom:1px solid #567}}
</style><h1>Macro placement playback</h1><p>Updated {stamp}.</p>
<p>Each replay shows macros over movable standard-cell footprint density, with rigid groups and merge/snap events.
Use Play, Previous/Next, the frame slider and Names to identify behavior for feedback.</p>
<p>The larger design evaluations run the magnet-disabled baseline first, then the magnet-enabled arm.
Playback becomes available when the macro stage writes its report; final cell placement may still be running.
These are placement experiments; playback availability does not certify routed results.</p>
<table><tr><th>Design / experiment</th><th>Playback</th><th>Frames</th><th>Macros</th><th>Groups</th></tr>
{''.join(rows)}</table></html>'''
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + '.tmp')
    temporary.write_text(page)
    temporary.replace(output)
    return tuple(ready)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--entry', action='append', required=True, metavar='LABEL=PLACEMENT_DIRECTORY')
    parser.add_argument('--watch-seconds', type=int, default=0)
    args = parser.parse_args()
    entries = []
    for value in args.entry:
        label, separator, directory = value.partition('=')
        if not label or not separator or not directory:
            parser.error('entries must be LABEL=PLACEMENT_DIRECTORY')
        entries.append((label, Path(directory).resolve()))
    if args.watch_seconds < 0:
        parser.error('watch-seconds must be nonnegative')
    output = args.output.resolve()
    deadline = time.monotonic() + args.watch_seconds
    previous = None
    while True:
        ready = render(output, entries)
        if ready != previous:
            print(json.dumps({'gallery': str(output), 'ready': ready}), flush=True)
            previous = ready
        if len(ready) == len(entries) or time.monotonic() >= deadline:
            break
        time.sleep(min(15, max(0, deadline - time.monotonic())))


if __name__ == '__main__':
    main()
