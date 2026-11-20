# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Durable experiment receipts. Placements live in immutable worker directories."""

import contextlib
import json
import sqlite3
import time


class Journal:
    def __init__(self, root):
        self.path = root / 'experiments.sqlite3'
        with self.connection() as db:
            db.execute(
                'CREATE TABLE IF NOT EXISTS experiments (id TEXT PRIMARY KEY, started REAL, body TEXT, search TEXT)'
            )
            db.execute(
                'CREATE TABLE IF NOT EXISTS scores '
                '(trial TEXT, branch TEXT, stages TEXT, wl REAL, wns REAL, PRIMARY KEY(trial,branch))'
            )
            db.execute('CREATE INDEX IF NOT EXISTS score_by_wl ON scores(stages,wl)')
            db.execute('CREATE INDEX IF NOT EXISTS score_by_wns ON scores(stages,wns)')
            db.execute(
                'CREATE TABLE IF NOT EXISTS decisions '
                '(sequence INTEGER PRIMARY KEY, trial TEXT, branch TEXT, action TEXT, finding TEXT, time REAL)'
            )

            db.execute('CREATE TABLE IF NOT EXISTS accepted (revision TEXT PRIMARY KEY, state TEXT)')
            db.execute('CREATE TABLE IF NOT EXISTS undo (after TEXT PRIMARY KEY, before TEXT, trial TEXT, branch TEXT)')

            db.execute('CREATE TABLE IF NOT EXISTS clock (id INTEGER PRIMARY KEY, epoch INTEGER)')
            db.execute('INSERT OR IGNORE INTO clock VALUES(1,0)')

    def epoch(self, advance=False):
        with self.connection() as db:
            if advance:
                db.execute('UPDATE clock SET epoch=epoch+1 WHERE id=1')
            return db.execute('SELECT epoch FROM clock WHERE id=1').fetchone()[0]

    def snapshot(self, rev, score, state):
        with self.connection() as db:
            db.execute('INSERT OR IGNORE INTO accepted VALUES(?,?)', (rev, json.dumps(state)))
            if score.get('hpwl_um') is None:
                return
            db.execute(
                'INSERT OR IGNORE INTO scores VALUES(?,?,?,?,?)',
                ('main', rev, score['coverage'], score['hpwl_um'], score['estimated_wns_ps']),
            )

    def checkpoint(self, after, before, trial=None, branch=None):
        with self.connection() as db:
            db.execute('INSERT OR REPLACE INTO undo VALUES(?,?,?,?)', (after, json.dumps(before), trial, branch))

    def parent(self, after):
        with self.connection() as db:
            entry = db.execute('SELECT before,trial,branch FROM undo WHERE after=?', (after,)).fetchone()
        return (after, json.loads(entry[0]), entry[1], entry[2]) if entry else None

    def saved_main(self, rev, required=True):
        with self.connection() as db:
            row = db.execute('SELECT state FROM accepted WHERE revision=?', (rev,)).fetchone()
        if not row:
            if not required:
                return None
            raise ValueError('unknown accepted revision')
        return json.loads(row[0])

    @contextlib.contextmanager
    def connection(self):
        with contextlib.closing(sqlite3.connect(self.path, timeout=10)) as db, db:
            yield db

    def save(self, record):
        key = ' '.join(
            [
                record['id'],
                record['home'],
                record['hypothesis'],
                *record['selection'],
                *record['candidates'],
            ]
        )
        with self.connection() as db:
            db.execute(
                'INSERT OR REPLACE INTO experiments VALUES (?,?,?,?)',
                (
                    record['id'],
                    record['started'],
                    json.dumps(record, allow_nan=False),
                    key,
                ),
            )
            for label, row in record['candidates'].items():
                score = row.get('assembled', {})
                if row['status'] == 'done' and score.get('hpwl_um') is not None:
                    db.execute(
                        'INSERT OR REPLACE INTO scores VALUES (?,?,?,?,?)',
                        (
                            record['id'],
                            label,
                            score['coverage'],
                            score['hpwl_um'],
                            score['estimated_wns_ps'],
                        ),
                    )

    def decide(self, ident, branch, action, finding):
        with self.connection() as db:
            db.execute(
                'INSERT INTO decisions VALUES(NULL,?,?,?,?,?)',
                (ident, branch, action, finding, time.time()),
            )

    def get(self, ident):
        with self.connection() as db:
            row = db.execute('SELECT body FROM experiments WHERE id=?', (ident,)).fetchone()
            if not row:
                raise ValueError('unknown experiment')
            return json.loads(row[0])

    def search(self, query='', limit=25, offset=0):
        if type(limit) is not int or not 1 <= limit <= 250 or type(offset) is not int or offset < 0:
            raise ValueError('invalid history page')
        with self.connection() as db:
            rows = db.execute(
                'SELECT id,body FROM experiments e WHERE instr(lower(search),lower(?)) OR EXISTS '
                '(SELECT 1 FROM decisions d WHERE d.trial=e.id AND instr(lower(d.finding),lower(?))) '
                'ORDER BY started DESC LIMIT ? OFFSET ?',
                (query, query, limit, offset),
            )
            results = []
            for ident, body in rows:
                entry = json.loads(body)
                entry['decisions'] = [
                    {'branch': b, 'action': a, 'finding': f}
                    for b, a, f in db.execute(
                        'SELECT branch,action,finding FROM decisions WHERE trial=? ORDER BY sequence',
                        (ident,),
                    )
                ]
                results.append(entry)
            return results

    def best(self, coverage, objective):
        order = 'wns DESC, wl' if objective == 'wns' else 'wl'
        with self.connection() as db:
            row = db.execute(
                f'SELECT trial,branch,wl,wns FROM scores WHERE stages=? AND '
                f'{"wns" if objective == "wns" else "wl"} IS NOT NULL ORDER BY {order} LIMIT 1',
                (coverage,),
            ).fetchone()
        return {'trial': row[0], 'branch': row[1], 'hpwl_um': row[2], 'estimated_wns_ps': row[3]} if row else None
