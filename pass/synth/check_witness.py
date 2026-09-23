#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Independent total-care, image and dependency witness checker. No optimizer or ABC imports."""
import argparse
import json
import pathlib
import sys


def fnv1a64(canonical):
    """The archive's canonical digest. Key order, ASCII keys, lowercase hex and
    compact separators are all part of the wire format, so they are fixed here."""
    value = 0xcbf29ce484222325
    for byte in json.dumps(canonical, separators=(",", ":"), ensure_ascii=False).encode():
        value = ((value ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    return f"{value:016x}"


class Invalid(ValueError):
    pass


class Exhausted(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise Invalid(message)


class Budget:
    def __init__(self, work, max_nodes=100000):
        self.remaining = work
        self.max_nodes = max_nodes

    def spend(self, work=1):
        if work > self.remaining:
            raise Exhausted("witness work budget exhausted; result is inconclusive")
        self.remaining -= work


def integer(value, maximum, description):
    require(type(value) is int and 0 <= value <= maximum, f"invalid {description}")
    return value


def ids(values, limit, description, unique=False):
    require(type(values) is list, f"invalid {description}")
    for value in values:
        integer(value, limit - 1, description)
    if unique:
        require(len(set(values)) == len(values), f"duplicate {description}")
    return values


def truth(table, arity):
    require(type(table) is dict and table["inputs"] == arity, "truth table arity mismatch")
    integer(table["inputs"], 12, "truth table arity")
    words = table["words"]
    require(type(words) is list and len(words) == ((1 << arity) + 63) // 64, "truth table storage mismatch")
    result = 0
    for i, word in enumerate(words):
        require(type(word) is str and len(word) == 16 and all(c in "0123456789abcdef" for c in word), "invalid table word")
        result |= int(word, 16) << (64 * i)
    require(result >> (1 << arity) == 0, "nonzero truth table padding")
    return result


def source_digest(source):
    # The wire format fixes key order, ASCII keys, lowercase hex and compact JSON.
    canonical = {"nodes": [{"source": n["source"], "inputs": n["inputs"],
                             "table": {"inputs": n["table"]["inputs"], "words": n["table"]["words"]}}
                            for n in source["nodes"]], "outputs": source["outputs"]}
    return fnv1a64(canonical)


class DecisionDiagram:
    """Independent reduced Boolean AND/OR engine, with bounded unique/apply tables."""
    def __init__(self, budget, limit):
        self.budget, self.limit = budget, limit
        self.nodes = [(float('inf'), 0, 0), (float('inf'), 1, 1)]
        self.unique, self.cache = {}, {}

    def make(self, variable, low, high):
        self.budget.spend()
        if low == high:
            return low
        key = (variable, low, high)
        if key not in self.unique:
            if len(self.nodes) - 2 >= self.limit:
                raise Exhausted("symbolic node budget exhausted; result is inconclusive")
            self.unique[key] = len(self.nodes)
            self.nodes.append(key)
        return self.unique[key]

    def remember(self, key, value):
        if len(self.cache) >= 4 * self.limit:
            self.cache.clear()
        self.cache[key] = value
        return value

    def negate(self, value):
        self.budget.spend()
        if value < 2:
            return 1 - value
        key = ('not', value)
        if key in self.cache:
            return self.cache[key]
        variable, low, high = self.nodes[value]
        return self.remember(key, self.make(variable, self.negate(low), self.negate(high)))

    def apply(self, disjunction, left, right):
        self.budget.spend()
        left, right = sorted((left, right))
        if left == right:
            return left
        if disjunction:
            if left == 0:
                return right
            if left == 1:
                return 1
        else:
            if left == 0:
                return 0
            if left == 1:
                return right
        key = (disjunction, left, right)
        if key in self.cache:
            return self.cache[key]
        top = min(self.nodes[left][0], self.nodes[right][0])
        def branch(value, high):
            var, lo, hi = self.nodes[value]
            return (hi if high else lo) if var == top else value
        low = self.apply(disjunction, branch(left, False), branch(right, False))
        high = self.apply(disjunction, branch(left, True), branch(right, True))
        return self.remember(key, self.make(top, low, high))

    def compose(self, inputs, bits, position=0, assignment=0):
        self.budget.spend()
        if position == len(inputs):
            return (bits >> assignment) & 1
        low = self.compose(inputs, bits, position + 1, assignment)
        high = self.compose(inputs, bits, position + 1, assignment | (1 << position))
        zero = self.apply(False, self.negate(inputs[position]), low)
        one = self.apply(False, inputs[position], high)
        return self.apply(True, zero, one)


def symbolic_relation(original, tables, sources, cone, leaves, root, budget, node_limit=65536):
    if len(sources) > 256:
        raise Exhausted("symbolic source admission limit; result is inconclusive")
    bdd = DecisionDiagram(budget, node_limit)
    values = {source: bdd.make(source, 0, 1) for source in sources}
    for current in cone:
        values[current] = bdd.compose([values[p] for p in original[current]['inputs']], tables[current])
    reachable, dependency = 0, {}
    for assignment in range(1 << len(leaves)):
        budget.spend()
        block = 1
        for j, leaf in enumerate(leaves):
            literal = values[leaf] if (assignment >> j) & 1 else bdd.negate(values[leaf])
            block = bdd.apply(False, block, literal)
        if block == 0:
            continue
        reachable |= 1 << assignment
        if root is not None:
            yes = bdd.apply(False, block, values[root])
            no = bdd.apply(False, block, bdd.negate(values[root]))
            require(not (yes and no), "divisors do not determine the source function")
            dependency[assignment] = bool(yes)
    return reachable, dependency


def boundary_digest(boundary):
    canonical = {'scope': boundary['scope'], 'names_scope': boundary['names_scope'],
                 'clock_reset_semantics_verified': boundary['clock_reset_semantics_verified'],
                 'inputs': [{key: p[key] for key in ('name', 'kind', 'state', 'source_node')} for p in boundary['inputs']],
                 'outputs': [{key: p[key] for key in ('name', 'kind', 'state', 'source_node')} for p in boundary['outputs']],
                 'states': [{key: s[key] for key in ('name', 'init', 'input', 'output')} for s in boundary['states']]}
    return fnv1a64(canonical)


def check_boundary(record, budget):
    boundary = record['boundary']
    require(type(boundary) is dict and boundary.get('scope') == 'abc_ci_co_register_cut'
            and boundary.get('names_scope') == 'original_search_region'
            and boundary.get('clock_reset_semantics_verified') is False, 'invalid boundary scope')
    inputs, outputs, states = (boundary[key] for key in ('inputs', 'outputs', 'states'))
    require(all(type(items) is list for items in (inputs, outputs, states)), 'invalid boundary lists')
    if len(inputs) + len(outputs) + len(states) > 200000:
        raise Exhausted('boundary admission limit; result is inconclusive')
    original = record['source']['nodes']
    sources = [i for i, node in enumerate(original) if node['source']]
    require(len(inputs) == len(sources) and len(outputs) == len(record['source']['outputs']), 'boundary count mismatch')
    state_ports = [set(), set()]
    name_bytes = 0

    def check_name(name):
        nonlocal name_bytes
        size = len(name.encode('utf-8'))
        name_bytes += size
        if size > 65536 or name_bytes > 16 * 1024 * 1024:
            raise Exhausted('boundary name admission limit; result is inconclusive')
        budget.spend(size)

    for side, (ports, bindings) in enumerate(((inputs, sources), (outputs, record['source']['outputs']))):
        for index, (port, source) in enumerate(zip(ports, bindings)):
            budget.spend()
            require(type(port) is dict and type(port.get('name')) is str and port.get('kind') in ('primary', 'state', 'opaque'),
                    'invalid boundary port')
            check_name(port['name'])
            require(type(port.get('source_node')) is int and port['source_node'] == source, 'boundary source binding mismatch')
            state = port.get('state')
            require(type(state) is int, 'invalid boundary state index')
            if port['kind'] == 'state':
                require(0 <= state < len(states) and state not in state_ports[side], 'missing or repeated state cut')
                state_ports[side].add(state)
                entry = states[state]
                require(type(entry) is dict and type(entry.get('output' if side == 0 else 'input')) is int
                        and entry['output' if side == 0 else 'input'] == index, 'state cut pairing mismatch')
            else:
                require(state == -1, 'non-state port has state binding')
    require(state_ports[0] == state_ports[1] == set(range(len(states))), 'unpaired state cut')
    for state in states:
        budget.spend()
        require(type(state.get('name')) is str and state.get('init') in ('zero', 'one', 'dont_care', 'unspecified'),
                'invalid state-cut initialization')
        check_name(state['name'])
    require(boundary_digest(boundary) == record['boundary_digest_fnv1a64'], 'boundary digest mismatch')


def check_source(record, budget):
    require(type(record.get('producer_version')) is str and record['producer_version'], 'missing producer version')
    source = record["source"]
    original = source["nodes"]
    require(type(original) is list, "invalid original node list")
    if len(original) > budget.max_nodes:
        raise Exhausted("source node admission limit; result is inconclusive")
    tables = []
    for index, node in enumerate(original):
        budget.spend()
        require(type(node["source"]) is bool, "invalid source flag")
        inputs = ids(node["inputs"], index, "source input")
        budget.spend(len(inputs))
        require(not node["source"] or not inputs, "source has fanins")
        tables.append(truth(node["table"], len(inputs)))
    source_outputs = ids(source["outputs"], len(original), "source output")
    require(source_digest(source) == record["source_digest_fnv1a64"], "source digest mismatch")
    if record.get('kind') == 'unate_source':
        require(type(record.get('schema_version')) is int and record['schema_version'] in (1, 2), 'unsupported source schema')
        if record['schema_version'] == 2:
            check_boundary(record, budget)
    return source, original, tables, source_outputs


def check(record, budget):
    require(type(record["schema_version"]) is int and record["schema_version"] in (1, 2, 3, 4, 5)
            and record["kind"] == "unate_witness", "unsupported witness schema")
    require(type(record["producer_version"]) is str and record["producer_version"], "missing producer version")
    recipe = record["recipe"]
    require(type(recipe) is list and len(recipe) == 4, "invalid recipe")
    levels, support, literals, series = recipe
    integer(levels, (1 << 32) - 1, "recipe depth bound")  # 0 = unbounded depth
    for value, maximum in zip(recipe[1:], (12, (1 << 32) - 1, (1 << 32) - 1)):
        integer(value, maximum, "recipe bound")
        require(value > 0, "zero recipe bound")
    depth_limit = levels if levels else (1 << 32) - 1
    source, original, tables, source_outputs = check_source(record, budget)
    network = record["network"]
    original_count = len(original)
    encodings = network.get("encodings", [])
    hierarchical = record["schema_version"] >= 5
    require(type(encodings) is list and len(encodings) <= (256 if hierarchical else 12), "invalid encoder definitions")
    require(not encodings or record["schema_version"] >= 4, "encoder definitions require schema 4")
    original = list(original)
    for definition in encodings:
        budget.spend()
        inputs = ids(definition["inputs"], len(original) if hierarchical else original_count, "encoder input", unique=True)
        require(len(inputs) <= support and all(p >= original_count or original[p]["source"] for p in inputs),
                "encoder must use original sources or preceding encoders")
        tables.append(truth(definition["table"], len(inputs)))
        original.append({"source": False, "inputs": inputs, "table": definition["table"]})
    nodes = network["nodes"]
    require(type(nodes) is list and len(nodes) <= 2 * len(original), "network node admission limit")
    identities = set()
    depth = max_support = max_literals = max_series = inverters = 0
    for index, node in enumerate(nodes):
        budget.spend()
        origin = integer(node["origin"], len(original) - 1, "node origin")
        negative = node["negative"]
        require(type(negative) is bool, "invalid rail polarity")
        require((origin, negative) not in identities, "duplicate rail producer")
        identities.add((origin, negative))
        ports = ids(node["ports"], index, "function port", unique=True)
        functional = node.get("functional", False)
        support_limit = len(original) if functional else origin
        logical = ids(node["logical_inputs"], support_limit, "logical input", unique=True)
        witness = ids(node["witness_inputs"], support_limit, "witness input", unique=True)
        terms = node["terms"]
        require(type(terms) is list, "invalid terms")
        functional = node.get("functional", False)
        require(type(functional) is bool and (not functional or record["schema_version"] >= 3), "invalid dependency witness mode")
        kind = node["kind"]
        level = integer(node["level"], depth_limit, "logical depth")
        if kind in ("source", "source_inverter"):
            require(original[origin]["source"] and not logical and not witness and not terms and level == 0
                    and node.get("care") is None and not node.get("image_sources", []) and not functional,
                    "invalid source rail")
            if kind == "source":
                require(not negative and not ports, "invalid positive source")
            else:
                require(negative and len(ports) == 1, "invalid source inverter")
                producer = nodes[ports[0]]
                require(producer["kind"] == "source" and producer["origin"] == origin, "wrong inverter producer")
                inverters += 1
            continue
        require(kind == "function" and not original[origin]["source"], "invalid function kind/origin")
        require(len(logical) <= support and len(witness) <= 12 and set(logical) <= set(witness), "invalid separating support")
        completion = truth(node["completion"], len(logical))
        variables = []
        for port in ports:
            producer = nodes[port]
            require(producer["origin"] in logical, "port not in logical support")
            variables.append((producer["origin"], producer["negative"]))
        count = stack = 0
        for term in terms:
            ids(term, len(ports), "term port", unique=True)
            budget.spend(len(term) + 1)
            require(len({variables[p][0] for p in term}) == len(term), "repeated variable or contradictory rails in term")
            count += len(term)
            stack = max(stack, len(term))
        require(count <= literals and stack <= series, "function complexity exceeds recipe")
        require(level == max([0] + [nodes[p]["level"] for p in ports]) + 1, "incorrect dependency depth")
        care = (1 << (1 << len(witness))) - 1
        dependency = {}
        require(not functional or node.get("care") is not None, "dependency witness requires care")
        if node.get("care") is not None:
            require(record["schema_version"] >= 2, "image care requires witness schema 2 or newer")
            care = truth(node["care"], len(witness))
            declared = ids(node["image_sources"], len(original), "image source", unique=True)
            pending, ancestors, sources = list(witness) + ([origin] if functional else []), set(), set()
            while pending:
                budget.spend()
                current = pending.pop()
                if current in ancestors:
                    continue
                ancestors.add(current)
                if original[current]["source"]:
                    sources.add(current)
                else:
                    pending.extend(original[current]["inputs"])
            require(sorted(sources) == declared, "incorrect image source closure")
            reachable = 0
            ordered_image = sorted(ancestors - sources)
            if len(declared) > 12:
                reachable, dependency = symbolic_relation(original, tables, declared, ordered_image, witness,
                                                          origin if functional else None, budget)
            else:
                for assignment in range(1 << len(declared)):
                    budget.spend(len(declared) + len(witness) + 1)
                    values = {source: bool((assignment >> j) & 1) for j, source in enumerate(declared)}
                    for current in ordered_image:
                        inputs = original[current]["inputs"]
                        budget.spend(len(inputs) + 1)
                        word = sum(int(values[driver]) << j for j, driver in enumerate(inputs))
                        values[current] = bool((tables[current] >> word) & 1)
                    image = sum(int(values[driver]) << j for j, driver in enumerate(witness))
                    reachable |= 1 << image
                    if functional:
                        value = values[origin]
                        require(image not in dependency or dependency[image] == value, "divisors do not determine the source function")
                        dependency[image] = value
            require(care == reachable, "care mask is not the exact source image")
        else:
            require(not node.get("image_sources", []), "image sources without image care")
        # Rebuild the original cone independently. A cut must intercept every
        # source path. Correlations are allowed only through the exact image
        # independently reconstructed above, never sampled absence.
        leaves = set(witness)
        pending, cone = ([] if functional else [origin]), set()
        while pending:
            budget.spend()
            current = pending.pop()
            if current in leaves or current in cone:
                continue
            require(not original[current]["source"], "cut fails to separate source cone")
            cone.add(current)
            pending.extend(original[current]["inputs"])
        ordered = sorted(cone)
        for assignment in range(1 << len(witness)):
            budget.spend(len(witness) + len(logical) + len(ports) + count + len(terms) + 1)
            values = {leaf: bool((assignment >> i) & 1) for i, leaf in enumerate(witness)}
            for current in ordered:
                inputs = original[current]["inputs"]
                budget.spend(len(inputs) + 1)
                word = sum(int(values[driver]) << i for i, driver in enumerate(inputs))
                values[current] = bool((tables[current] >> word) & 1)
            expected = (dependency.get(assignment, False) if functional else values[origin]) != negative
            projection = sum(int(values[driver]) << i for i, driver in enumerate(logical))
            completed = bool((completion >> projection) & 1)
            if (care >> assignment) & 1:
                require(completed == expected, "completion/source mismatch on care image")
            rails = [values[driver] != sign for driver, sign in variables]
            actual = any(all(rails[port] for port in term) for term in terms)
            require(actual == completed, "unate form/total completion mismatch")
        depth, max_support = max(depth, level), max(max_support, len(logical))
        max_literals, max_series = max(max_literals, count), max(max_series, stack)
    outputs = ids(network["outputs"], len(nodes), "network output")
    require(len(outputs) == len(source_outputs), "output count mismatch")
    for output, expected in zip(outputs, source_outputs):
        require(nodes[output]["origin"] == expected and not nodes[output]["negative"], "output correspondence mismatch")
    for field, expected in (("depth", depth), ("max_support", max_support), ("max_literals", max_literals),
                            ("max_series", max_series), ("source_inverters", inverters)):
        require(type(network[field]) is int and network[field] == expected, f"incorrect {field}")
    return len(outputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("--work", type=int, default=50000000)
    parser.add_argument("--max-bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--max-nodes", type=int, default=100000)
    args = parser.parse_args()
    try:
        require(args.work >= 0 and args.max_bytes >= 0 and args.max_nodes >= 0, "negative checker limit")
        if args.archive.stat().st_size > args.max_bytes:
            raise Exhausted("archive size admission limit; result is inconclusive")
        budget, records, outputs, decompositions = Budget(args.work, args.max_nodes), 0, 0, 0
        with args.archive.open() as source:
            for line in source:
                record = json.loads(line)
                require(type(record["record"]) is int and record["record"] == records, "archive record order mismatch")
                if record.get("kind") == "unate_source":
                    require(type(record.get("schema_version")) is int and record["schema_version"] in (1, 2),
                            "invalid source record schema")
                    check_source(record, budget)
                else:
                    outputs += check(record, budget)
                    decompositions += 1
                records += 1
        if decompositions == 0:
            raise Exhausted("archive contains no decomposition witnesses")
        print(json.dumps({"status": "verified", "records": records, "decompositions": decompositions, "outputs": outputs,
                          "scope": "archived_boolean_decompositions", "work": args.work - budget.remaining}))
        return 0
    except Exhausted as error:
        print(f"inconclusive: {error}", file=sys.stderr)
        return 2
    except (Invalid, KeyError, TypeError, IndexError, OSError, UnicodeError, json.JSONDecodeError) as error:
        print(f"invalid witness: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
