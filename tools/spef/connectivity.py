"""Offline extracted-SPEF pin coverage and resistor connectivity.

Accepts the OpenROAD detailed-net subset, including name maps and coupling
capacitors. Coupling is never a conductive edge. This audits extracted RC;
geometric DRC and physical pin contact still require routing evidence.
"""
import math
import re


def canonical(name):
    return re.sub(r"\\(.)", r"\1", name)


def audit(lines, expected, abutted=(), max_examples=20):
    """Compare all expected net/pin identities and require connected R graphs.

    Missing single-pin and explicitly abutted nets need no extracted wire.
    Multi-pin detailed nets without resistors are unverifiable unless the
    caller independently establishes abutment. All zero-ohm edges are retained.
    """
    wanted = {}
    for raw, pins in expected.items():
        name = canonical(raw)
        normal = {canonical(pin) for pin in pins}
        if name in wanted or len(normal) != len(pins):
            raise ValueError("ambiguous normalized expected identity")
        wanted[name] = normal
    shorts = {canonical(name) for name in abutted}
    names = {}
    seen = set()
    result = dict(nets=0, connections=0, resistors=0, coupling_capacitors=0,
                  missing_nets=0, unexpected_nets=0, pin_coverage_failures=0,
                  disconnected_nets=0, orphan_components=0,
                  unverifiable_no_resistors=0, abutted_nets=0, examples=[])
    net = None
    section = None
    name_map = False

    def decode(token):
        match = re.match(r"^(\*\d+)(.*)$", token)
        if match:
            alias, tail = match.groups()
            if alias not in names:
                raise ValueError("undefined SPEF name-map entry: " + alias)
            return canonical(names[alias] + tail)
        return canonical(token)

    def number(text):
        value = float(text)
        if not math.isfinite(value) or value < 0:
            raise ValueError("invalid parasitic value")
        return value

    def example(name, issue, **detail):
        if len(result['examples']) < max_examples:
            result['examples'].append(dict(net=name, issue=issue, **detail))

    def finish():
        name, pins, parent = net['name'], net['pins'], net['parent']
        def find(node):
            while parent[node] != node:
                parent[node] = parent[parent[node]]
                node = parent[node]
            return node
        result['nets'] += 1
        result['connections'] += len(pins)
        if name not in wanted:
            result['unexpected_nets'] += 1
            example(name, 'unexpected_net')
        elif pins != wanted[name]:
            result['pin_coverage_failures'] += 1
            example(name, 'pin_coverage', missing=sorted(wanted[name] - pins)[:8],
                    unexpected=sorted(pins - wanted[name])[:8])
        if len(pins) > 1 and not net['edges']:
            if name in shorts:
                result['abutted_nets'] += 1
            else:
                result['unverifiable_no_resistors'] += 1
                example(name, 'multi_pin_net_without_resistors')
        else:
            roots = {find(pin) for pin in pins}
            if len(roots) > 1:
                result['disconnected_nets'] += 1
                example(name, 'disconnected_pin_components', components=len(roots))
            orphans = {find(node) for node in parent} - roots
            result['orphan_components'] += len(orphans)
            if orphans:
                example(name, 'orphan_resistor_or_ground_cap_components', components=len(orphans))

    for line in lines:
        fields = line.split()
        if not fields or fields[0].startswith('//'):
            continue
        tag = fields[0]
        if net is None:
            if tag == '*NAME_MAP':
                name_map = True
                continue
            if name_map and re.fullmatch(r'\*\d+', tag):
                if len(fields) != 2 or tag in names:
                    raise ValueError("invalid or duplicate name-map entry")
                names[tag] = fields[1].strip('"')
                continue
            name_map = False
            if tag == '*DELIMITER' and fields[1:] != [':']:
                raise ValueError("only colon SPEF pin delimiters are supported")
            if tag == '*END':
                raise ValueError("END without detailed net")
            if tag == '*L_NET':
                raise ValueError("lumped nets do not prove resistor connectivity")
            if tag != '*D_NET':
                continue
            if len(fields) != 3:
                raise ValueError("invalid detailed net header")
            number(fields[2])
            name = decode(fields[1])
            if name in seen:
                raise ValueError("duplicate normalized detailed net")
            seen.add(name)
            net = dict(name=name, pins=set(), parent={}, edges=0)
            section = None
            continue
        if tag in ['*CONN', '*CAP', '*RES']:
            section = tag
            continue
        if tag == '*END':
            finish()
            net = None
            section = None
            continue
        if tag == '*D_NET':
            raise ValueError("missing detailed-net END")
        parent = net['parent']
        if section == '*CONN':
            if len(fields) < 3 or tag not in ['*I', '*P'] or fields[2] not in ['I', 'O', 'B']:
                raise ValueError("unsupported connection record")
            pin = decode(fields[1])
            if pin in net['pins']:
                raise ValueError("duplicate normalized connection")
            net['pins'].add(pin)
            parent.setdefault(pin, pin)
        elif section == '*CAP':
            if len(fields) not in [3, 4] or not fields[0].isdigit():
                raise ValueError("invalid capacitor record")
            number(fields[-1])
            node = decode(fields[1])
            if len(fields) == 3:
                parent.setdefault(node, node)
            else:
                # Coupling endpoint order is arbitrary. Only terminals or
                # internal nodes belonging to this detailed net are local.
                endpoints = [node, decode(fields[2])]
                local = [n for n in endpoints if n in net['pins'] or n.rpartition(':')[0] == net['name']]
                if not local:
                    raise ValueError("coupling capacitor has no identifiable local endpoint")
                for n in local:
                    parent.setdefault(n, n)
                result['coupling_capacitors'] += 1
        elif section == '*RES':
            if len(fields) != 4 or not fields[0].isdigit():
                raise ValueError("invalid resistor record")
            number(fields[3])
            a, b = decode(fields[1]), decode(fields[2])
            parent.setdefault(a, a)
            parent.setdefault(b, b)
            def root(node):
                while parent[node] != node:
                    parent[node] = parent[parent[node]]
                    node = parent[node]
                return node
            parent[root(a)] = root(b)
            net['edges'] += 1
            result['resistors'] += 1
        else:
            raise ValueError("unknown detailed-net section")
    if net is not None:
        raise ValueError("unterminated detailed net")
    for name, pins in wanted.items():
        if name not in seen and len(pins) > 1:
            if name in shorts:
                result['abutted_nets'] += 1
            else:
                result['missing_nets'] += 1
                example(name, 'missing_extracted_net')
    result['passed'] = not any(result[key] for key in [
        'missing_nets', 'unexpected_nets', 'pin_coverage_failures',
        'disconnected_nets', 'orphan_components', 'unverifiable_no_resistors'])
    return result
