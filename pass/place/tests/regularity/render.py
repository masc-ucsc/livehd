"""Dependency-free diagnostic PNGs and orientation-independent pattern scores.

These are geometric observers, never placer inputs. Expected pictures are
hand-designed topology sketches, not fabricated measurements or golden DEFs.
"""
import hashlib
import math
import re
import struct
import zlib
from pathlib import Path

SIZE = 720


class Canvas:
    def __init__(self):
        self.pixels = bytearray([248, 249, 252]) * SIZE * SIZE

    def rect(self, x, y, w, h, color):
        x0, y0 = max(0, int(x)), max(0, int(y))
        x1, y1 = min(SIZE, int(x+w)+1), min(SIZE, int(y+h)+1)
        if x1 <= x0 or y1 <= y0:
            return
        row = bytes(color) * (x1-x0)
        for py in range(y0, y1):
            self.pixels[(py*SIZE+x0)*3:(py*SIZE+x1)*3] = row

    def line(self, a, b, color=(135, 150, 170), width=2):
        steps = max(1, int(max(abs(a[0]-b[0]), abs(a[1]-b[1]))))
        for i in range(steps+1):
            x = a[0] + (b[0]-a[0])*i/steps
            y = a[1] + (b[1]-a[1])*i/steps
            self.rect(x-width/2, y-width/2, width, width, color)

    def number(self, value, x, y, scale=2):
        digits = ['111101101101111', '010110010010111', '111001111100111', '111001111001111',
                  '101101111001001', '111100111001111', '111100111101111', '111001001001001',
                  '111101111101111', '111101111001111']
        label = str(value)
        left = x-(4*len(label)-1)*scale/2
        for offset, digit in enumerate(label):
            for i, bit in enumerate(digits[int(digit)]):
                if bit == '1':
                    self.rect(left+(offset*4+i%3)*scale, y+(i//3-2.5)*scale, scale-0.1, scale-0.1, (255, 255, 255))

    def save(self, path):
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
        raw = b''.join(b'\0' + self.pixels[i*SIZE*3:(i+1)*SIZE*3] for i in range(SIZE))
        Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', SIZE, SIZE, 8, 2, 0, 0, 0))
                               + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def color(index):
    palette = [(55, 120, 190), (210, 105, 55), (65, 155, 110), (155, 90, 180),
               (190, 155, 50), (55, 155, 175), (195, 85, 130), (105, 125, 65)]
    return palette[index % len(palette)]


def reference_routes(case, points, edges):
    routes = []
    for a, b in edges:
        route = [points[a], points[b]]
        if case['pattern'] == 'serpentine' and (a, b) == (0, len(points)-1):
            # Draw the closing connection outside the folded runs. This is a
            # reference sketch, not a routing guide or a measured wire path.
            route = [points[a], (0, points[a][1]), (0, points[b][1]), points[b]]
        routes.append((a, b, route))
    return routes


def expected(case, points, edges, path):
    image = Canvas()
    routes = reference_routes(case, points, edges)
    vertices = [p for _, _, route in routes for p in route]
    xmin, xmax = min(x for x, _ in vertices), max(x for x, _ in vertices)
    ymin, ymax = min(y for _, y in vertices), max(y for _, y in vertices)
    pitch = 540 / max(xmax-xmin, ymax-ymin)
    left, bottom = (SIZE-(xmax-xmin)*pitch)/2, (SIZE-(ymax-ymin)*pitch)/2
    def project(point):
        x, y = point
        return left+(x-xmin)*pitch, SIZE-bottom-(y-ymin)*pitch
    centers = [project(p) for p in points]
    half = min(24, pitch/4)
    for a, b, route in routes:
        for start, end in zip(route, route[1:]):
            image.line(project(start), project(end), width=3)
        # Standard-cell logic belongs in the corridors between adjacent macros.
        segments = [(project(start), project(end)) for start, end in zip(route, route[1:])]
        lengths = [math.dist(start, end) for start, end in segments]
        for t in range(3, 8):
            distance = sum(lengths)*t/10
            for (start, end), length in zip(segments, lengths):
                if distance <= length:
                    x = start[0]+(end[0]-start[0])*distance/length
                    y = start[1]+(end[1]-start[1])*distance/length
                    radius = min(5, pitch/24)
                    image.rect(x-radius, y-radius, 2*radius, 2*radius, color(a))
                    break
                distance -= length
    for i, (x, y) in enumerate(centers):
        outline = (190, 45, 55) if i in case.get('fixed_macros', []) else (25, 40, 65)
        image.rect(x-half, y-half, 2*half, 2*half, outline)
        image.rect(x-half+3, y-half+3, 2*half-6, 2*half-6, color(i))
        image.number(i, x, y)
    if case['pattern'] == 'hierarchy':
        for x in range(2):
            for y in range(2):
                lo = (90+2*x*pitch-42, SIZE-90-(2*y+1)*pitch-42)
                hi = (lo[0]+pitch+84, lo[1]+pitch+84)
                for a, b in [(lo, (hi[0], lo[1])), (lo, (lo[0], hi[1])),
                             (hi, (hi[0], lo[1])), (hi, (lo[0], hi[1]))]:
                    image.line(a, b, (40, 60, 85))
    image.save(path)


def read_def(path):
    text = Path(path).read_text()
    units = int(re.search(r'UNITS DISTANCE MICRONS (\d+)', text)[1])
    die = [float(v) for v in re.search(r'DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)', text).groups()]
    section = re.search(r'^COMPONENTS \d+ ;(.*?)^END COMPONENTS', text, re.M | re.S)[1]
    cells = {}
    for entry in section.split(';'):
        m = re.search(r'-\s+(\S+)\s+(\S+).*?\+\s+(FIXED|PLACED|COVER)\s+\(\s*(-?\d+)\s+(-?\d+)\s*\)\s+(\S+)', entry, re.S)
        if m:
            name, master, status, x, y, orientation = m.groups()
            cells[name] = dict(master=master, x=float(x), y=float(y), status=status, orientation=orientation)
    return units, die, cells


def tile_index(name, case):
    # The physical names retain call-site names through synthesis flattening.
    if case['pattern'] == 'hierarchy':
        group = re.search(r'(?:^|[/._])g([0-3])(?:[/._]|$)', name)
        tile = re.search(r'(?:^|[/._])t([0-3])(?:[/._]|$)', name)
        if group and tile:
            g, t = int(group[1]), int(tile[1])
            return (2*(g//2)+t//2)*4 + 2*(g%2)+t%2
    else:
        match = re.search(r'(?:^|[/._])t(\d+)(?:[/._]|$)', name)
        if match:
            return int(match[1])
    return None


def actual(case, units, die, cells, path, positions=None):
    image = Canvas()
    xl, yl, xh, yh = die
    scale = 640/max(xh-xl, yh-yl)
    widths = dict(INV=1, BUF=1, NAND2=2, NOR2=2, XOR2=3, TIE0=1, TIE1=1, DFF=4, REG_MACRO=40)
    for macro in (False, True):
        for name, cell in cells.items():
            if (cell['master'] == 'REG_MACRO') != macro:
                continue
            if positions is not None and name not in positions:
                raise ValueError(f'snapshot misses physical cell {name}')
            x, y = positions[name] if positions is not None else (cell['x'], cell['y'])
            w, h = widths[cell['master']]*units, (40 if macro else 2)*units
            if cell['orientation'] in ('E', 'W', 'FE', 'FW'):
                w, h = h, w
            index = tile_index(name, case)
            ink = color(index) if index is not None else (145, 150, 160)
            px, py = 40+(x-xl)*scale, SIZE-40-(y+h-yl)*scale
            if macro:
                outline = (190, 45, 55) if index in case.get('fixed_macros', []) else (25, 40, 65)
                image.rect(px-2, py-2, w*scale+4, h*scale+4, outline)
            image.rect(px, py, max(1, w*scale), max(1, h*scale), ink)
            if macro and index is not None:
                image.number(index, px+w*scale/2, py+h*scale/2, scale=max(1, min(2, int(w*scale/10))))
    image.save(path)


def scores(case, points, edges, cells, positions=None):
    observed = {}
    for name, cell in cells.items():
        if cell['master'] == 'REG_MACRO':
            index = tile_index(name, case)
            if index is None or index in observed:
                raise ValueError(f'cannot identify unique tile for macro {name}')
            x, y = positions[name] if positions is not None else (cell['x'], cell['y'])
            observed[index] = complex(x, y)
    if set(observed) != set(range(len(points))):
        raise ValueError(f'expected {len(points)} identifiable macros, found {len(observed)}: {observed}')
    # Fit a translation, uniform scale and arbitrary rotation; complex
    # conjugation also permits reflection. No coordinate thresholds are hidden.
    target = [complex(x, y) for x, y in points]
    measured = [observed[i] for i in range(len(points))]
    target = [v-sum(target)/len(target) for v in target]
    measured = [v-sum(measured)/len(measured) for v in measured]
    if sum(abs(v)**2 for v in measured) < 1e-12:
        return dict(pattern_rms_pitch=None, edge_length_cv=None)
    errors = []
    for reflected in (False, True):
        shape = [v.conjugate() if reflected else v for v in target]
        fit = sum(a.conjugate()*b for a, b in zip(shape, measured))/sum(abs(v)**2 for v in shape)
        errors.append(math.sqrt(sum(abs(b-fit*a)**2 for a, b in zip(shape, measured))/len(shape))/max(abs(fit), 1e-9))
    lengths = [abs(observed[a]-observed[b])/abs(complex(*points[a])-complex(*points[b])) for a, b in edges]
    mean = sum(lengths)/len(lengths)
    return dict(pattern_rms_pitch=min(errors), edge_length_cv=math.sqrt(sum((v-mean)**2 for v in lengths)/len(lengths))/max(mean, 1e-9))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
