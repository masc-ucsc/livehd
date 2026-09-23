#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
import copy
import json
import pathlib
import random
import subprocess
import sys
import tempfile
import unittest

from check_witness import Budget, Exhausted, Invalid, check, check_source, boundary_digest, source_digest, symbolic_relation


def table(n, bits):
    return {"inputs": n, "words": [f"{bits:016x}"]}


def original(inputs=(), bits=0, source=False):
    return {"source": source, "inputs": list(inputs), "table": table(len(inputs), bits)}


def node(kind, origin, ports=(), terms=(), logical=(), witness=(), bits=0, negative=False, level=0):
    return {"kind": kind, "origin": origin, "negative": negative, "ports": list(ports),
            "terms": [list(t) for t in terms], "logical_inputs": list(logical), "witness_inputs": list(witness),
            "completion": table(len(logical), bits), "level": level}


def record(source, network):
    return {"schema_version": 1, "kind": "unate_witness", "producer_version": "test", "record": 0,
            "recipe": [2, 3, 16, 4], "source": source, "network": network,
            "source_digest_fnv1a64": source_digest(source)}


def shrinking():
    source = {"nodes": [original(source=True), original(source=True), original([0, 1], 6),
                         original([2, 1], 6), original([], 0), original([], 1)], "outputs": [3, 4, 5]}
    network = {"nodes": [node("source", 0),
                          node("function", 3, [0], [[0]], [0], [0, 1], 2, level=1),
                          node("function", 4, level=1), node("function", 5, terms=[[]], bits=1, level=1)],
               "outputs": [1, 2, 3], "depth": 1, "max_support": 1, "max_literals": 1, "max_series": 1,
               "source_inverters": 0}
    return record(source, network)


def cut_source():
    result = shrinking()
    result.update(schema_version=2, kind='unate_source')
    result['boundary'] = {
        'scope': 'abc_ci_co_register_cut', 'names_scope': 'original_search_region', 'clock_reset_semantics_verified': False,
        'inputs': [{'name': 'a', 'kind': 'primary', 'state': -1, 'source_node': 0},
                   {'name': 'q', 'kind': 'state', 'state': 0, 'source_node': 1}],
        'outputs': [{'name': 'd', 'kind': 'state', 'state': 0, 'source_node': 3},
                    {'name': 'zero', 'kind': 'primary', 'state': -1, 'source_node': 4},
                    {'name': 'one', 'kind': 'primary', 'state': -1, 'source_node': 5}],
        'states': [{'name': 'state\\"\n', 'init': 'unspecified', 'input': 0, 'output': 1}]}
    result['boundary_digest_fnv1a64'] = boundary_digest(result['boundary'])
    return result


def twins():
    source = {"nodes": [original(source=True), original(source=True), original([0, 1], 7), original([2, 0], 4)],
              "outputs": [2, 3]}
    network = {"nodes": [node("source", 0), node("source", 1),
                          node("source_inverter", 0, [0], negative=True),
                          node("source_inverter", 1, [1], negative=True),
                          node("function", 2, [2, 3], [[0], [1]], [0, 1], [0, 1], 7, level=1),
                          node("function", 2, [0, 1], [[0, 1]], [0, 1], [0, 1], 8, negative=True, level=1),
                          node("function", 3, [0, 5], [[0, 1]], [0, 2], [0, 2], 2, level=2)],
               "outputs": [4, 6], "depth": 2, "max_support": 2, "max_literals": 2, "max_series": 2,
               "source_inverters": 2}
    return record(source, network)


def image_completion():
    source = {"nodes": [original(source=True), original(source=True), original([0, 1], 8),
                         original([0, 1], 2), original([2, 3], 6)], "outputs": [4]}
    root = node("function", 4, [3, 4], [[0], [1]], [2, 3], [2, 3], 14, level=2)
    root.update(care=table(2, 7), image_sources=[0, 1])
    network = {"nodes": [node("source", 0), node("source", 1),
                          node("source_inverter", 1, [1], negative=True),
                          node("function", 2, [0, 1], [[0, 1]], [0, 1], [0, 1], 8, level=1),
                          node("function", 3, [0, 2], [[0, 1]], [0, 1], [0, 1], 2, level=1), root],
               "outputs": [5], "depth": 2, "max_support": 2, "max_literals": 2, "max_series": 2,
               "source_inverters": 1}
    result = record(source, network)
    result["schema_version"] = 2
    return result


class WitnessChecker(unittest.TestCase):
    def test_state_cut_manifest_binds_order_pairs_and_initialization(self):
        check_source(cut_source(), Budget(100000))
        for mutation in ('source', 'pair', 'orphan', 'init', 'kind', 'clock', 'digest', 'bool_index'):
            item = cut_source()
            boundary = item['boundary']
            if mutation == 'source': boundary['inputs'][1]['source_node'] = 0
            elif mutation == 'pair': boundary['states'][0]['output'] = 0
            elif mutation == 'orphan': boundary['states'].append(copy.deepcopy(boundary['states'][0]))
            elif mutation == 'init': boundary['states'][0]['init'] = 'unknown_clock'
            elif mutation == 'kind': boundary['inputs'][1]['kind'] = 'primary'
            elif mutation == 'clock': boundary['clock_reset_semantics_verified'] = True
            elif mutation == 'bool_index': boundary['inputs'][1]['state'] = False
            else: boundary['inputs'][0]['name'] = 'different'
            if mutation != 'digest':
                item['boundary_digest_fnv1a64'] = boundary_digest(boundary)
            with self.subTest(mutation=mutation), self.assertRaises(Invalid):
                check_source(item, Budget(100000))
        oversized = cut_source()
        oversized['boundary']['inputs'][0]['name'] = 'x' * 65537
        with self.assertRaises(Exhausted):
            check_source(oversized, Budget(1000000))

    def test_hierarchical_encoders_preserve_original_inputs_and_reject_cycles(self):
        source = {'nodes': [original(source=True) for _ in range(5)] +
                           [original(range(5), 1 << 31)], 'outputs': [5]}
        nodes = [node('source', i) for i in range(5)] + [
            node('function', 6, [0, 1], [[0, 1]], [0, 1], [0, 1], 8, level=1),
            node('function', 7, [2, 3], [[0, 1]], [2, 3], [2, 3], 8, level=1),
            node('function', 8, [5, 6], [[0, 1]], [6, 7], [6, 7], 8, level=2)]
        decoder = node('function', 5, [7, 4], [[0, 1]], [8, 4], [8, 4], 8, level=3)
        decoder.update(functional=True, care=table(2, 15), image_sources=list(range(5)))
        nodes.append(decoder)
        network = {'nodes': nodes, 'encodings': [
            {'inputs': [0, 1], 'table': table(2, 8)},
            {'inputs': [2, 3], 'table': table(2, 8)},
            {'inputs': [6, 7], 'table': table(2, 8)}],
            'outputs': [8], 'depth': 3, 'max_support': 2, 'max_literals': 2,
            'max_series': 2, 'source_inverters': 0}
        valid = record(source, network)
        valid['schema_version'] = 5
        valid['recipe'] = [3, 2, 8, 2]
        self.assertEqual(check(valid, Budget(1000000)), 1)
        for kind in ['cycle', 'forward', 'internal', 'definition', 'wire', 'level', 'schema', 'admission']:
            bad = copy.deepcopy(valid)
            if kind == 'cycle':
                bad['network']['encodings'][2]['inputs'][0] = 8
            elif kind == 'forward':
                bad['network']['encodings'][0]['inputs'][0] = 7
            elif kind == 'internal':
                bad['network']['encodings'][0]['inputs'][0] = 5
            elif kind == 'definition':
                bad['network']['encodings'][2]['table'] = table(2, 14)
            elif kind == 'wire':
                bad['network']['nodes'][7]['ports'][1] = 5
            elif kind == 'level':
                bad['network']['nodes'][7]['level'] = 1
            elif kind == 'schema':
                bad['schema_version'] = 4
            else:
                bad['network']['encodings'] *= 86
            with self.subTest(kind=kind), self.assertRaises(Invalid):
                check(bad, Budget(1000000))
        with self.assertRaises(Exhausted):
            check(valid, Budget(0))

    def test_two_disjoint_encoder_definitions_reconstruct_original_function(self):
        bits = sum(1 << x for x in range(64) if bin(x & 7).count('1') % 2 and bin(x >> 3).count('1') % 2)
        source = {'nodes': [original(source=True) for _ in range(6)] +
                           [original(range(6), bits)], 'outputs': [6]}
        nodes = [node('source', i) for i in range(6)] + [
            node('source_inverter', i, [i], negative=True) for i in range(6)]
        for group in range(2):
            inputs = list(range(3 * group, 3 * group + 3))
            nodes.append(node('function', 7 + group, inputs + [i + 6 for i in inputs],
                              [[0, 4, 5], [3, 1, 5], [3, 4, 2], [0, 1, 2]],
                              inputs, inputs, 150, level=1))
        decoder = node('function', 6, [12, 13], [[0, 1]], [7, 8], [7, 8], 8, level=2)
        decoder.update(functional=True, care=table(2, 15), image_sources=list(range(6)))
        nodes.append(decoder)
        network = {'nodes': nodes, 'encodings': [
            {'inputs': [0, 1, 2], 'table': table(3, 150)},
            {'inputs': [3, 4, 5], 'table': table(3, 150)}],
            'outputs': [14], 'depth': 2, 'max_support': 3, 'max_literals': 12,
            'max_series': 3, 'source_inverters': 6}
        valid = record(source, network)
        valid['schema_version'] = 4
        self.assertEqual(check(valid, Budget(1000000)), 1)
        for kind in ['definition', 'sources', 'wire', 'care', 'missing']:
            bad = copy.deepcopy(valid)
            if kind == 'definition':
                bad['network']['encodings'][1]['table'] = table(3, 151)
            elif kind == 'sources':
                bad['network']['encodings'][1]['inputs'][0] = 0
            elif kind == 'wire':
                bad['network']['nodes'][14]['ports'][1] = 12
            elif kind == 'care':
                bad['network']['nodes'][14]['care'] = table(2, 7)
            else:
                bad['network']['encodings'].pop()
            with self.subTest(kind=kind), self.assertRaises(Invalid):
                check(bad, Budget(1000000))

    def test_new_encoder_definitions_are_checked_against_original_outputs(self):
        source = {'nodes': [original(source=True) for _ in range(4)] + [original([0, 1, 2, 3], 0x9600)],
                  'outputs': [4]}
        encoder = node('function', 5, [0, 1, 2, 4, 5, 6],
                       [[0, 4, 5], [3, 1, 5], [3, 4, 2], [0, 1, 2]], [0, 1, 2], [0, 1, 2], 150, level=1)
        decoder = node('function', 4, [7, 3], [[0, 1]], [5, 3], [5, 3], 8, level=2)
        decoder.update(functional=True, care=table(2, 15), image_sources=[0, 1, 2, 3])
        network = {'nodes': [node('source', i) for i in range(4)] +
                             [node('source_inverter', i, [i], negative=True) for i in range(3)] + [encoder, decoder],
                   'encodings': [{'inputs': [0, 1, 2], 'table': table(3, 150)}],
                   'outputs': [8], 'depth': 2, 'max_support': 3, 'max_literals': 12, 'max_series': 3,
                   'source_inverters': 3}
        valid = record(source, network)
        valid['schema_version'] = 4
        self.assertEqual(check(valid, Budget(1000000)), 1)
        for kind in ['table', 'source', 'removed', 'schema', 'original']:
            bad = copy.deepcopy(valid)
            if kind == 'table':
                bad['network']['encodings'][0]['table'] = table(3, 151)
            elif kind == 'source':
                bad['network']['encodings'][0]['inputs'][0] = 4
            elif kind == 'removed':
                bad['network']['encodings'] = []
            elif kind == 'schema':
                bad['schema_version'] = 3
            else:
                bad['source']['nodes'][4]['table'] = table(4, 0x6996)
                bad['source_digest_fnv1a64'] = source_digest(bad['source'])
            with self.subTest(kind=kind), self.assertRaises(Invalid):
                check(bad, Budget(1000000))

    def test_symbolic_large_source_witness_and_corruption(self):
        nodes = [original(source=True) for _ in range(16)]
        forward, reverse = 0, 15
        for i in range(1, 16):
            nodes.append(original([forward, i], 8))
            forward = len(nodes) - 1
        for i in range(14, -1, -1):
            nodes.append(original([reverse, i], 8))
            reverse = len(nodes) - 1
        nodes.append(original([forward, reverse], 6))
        root = len(nodes) - 1
        function = node('function', root, witness=[forward, reverse], level=1)
        function.update(care=table(2, 9), image_sources=list(range(16)))
        source = {'nodes': nodes, 'outputs': [root]}
        network = {'nodes': [function], 'outputs': [0], 'depth': 1, 'max_support': 0,
                   'max_literals': 0, 'max_series': 0, 'source_inverters': 0}
        valid = record(source, network)
        valid['schema_version'] = 3
        self.assertEqual(check(valid, Budget(1000000)), 1)
        corrupted = copy.deepcopy(valid)
        corrupted['network']['nodes'][0]['care'] = table(2, 11)
        with self.assertRaisesRegex(Invalid, 'exact source image'):
            check(corrupted, Budget(1000000))
        # The same constant may instead carry a nonseparating empty dependency.
        functional = copy.deepcopy(valid)
        functional['network']['nodes'][0].update(functional=True, witness_inputs=[], care=table(0, 1))
        self.assertEqual(check(functional, Budget(1000000)), 1)
        functional['source']['nodes'][root]['table'] = table(2, 14)
        functional['source_digest_fnv1a64'] = source_digest(functional['source'])
        with self.assertRaisesRegex(Invalid, 'do not determine'):
            check(functional, Budget(1000000))
        with self.assertRaises(Exhausted):
            check(valid, Budget(100))
        with self.assertRaises(Exhausted):
            symbolic_relation(nodes, [int(n['table']['words'][0], 16) for n in nodes],
                              list(range(16)), list(range(16, root + 1)), [forward, reverse],
                              None, Budget(1000000), node_limit=1)

    def test_symbolic_engine_matches_independent_enumeration(self):
        rng = random.Random(0x51bed)
        for _ in range(50):
            nodes = [original(source=True) for _ in range(5)]
            for i in range(5, 13):
                nodes.append(original([rng.randrange(i), rng.randrange(i)], rng.randrange(16)))
            tables = [int(n['table']['words'][0], 16) for n in nodes]
            leaves = [2, 8, 11]
            reachable = 0
            dependency = {}
            refuted = False
            for assignment in range(32):
                values = [bool((assignment >> j) & 1) for j in range(5)]
                for i in range(5, len(nodes)):
                    word = sum(int(values[p]) << j for j, p in enumerate(nodes[i]['inputs']))
                    values.append(bool((tables[i] >> word) & 1))
                image = sum(int(values[p]) << j for j, p in enumerate(leaves))
                reachable |= 1 << image
                refuted |= image in dependency and dependency[image] != values[-1]
                dependency[image] = values[-1]
            args = (nodes, tables, list(range(5)), list(range(5, 13)), leaves)
            self.assertEqual(symbolic_relation(*args, None, Budget(1000000))[0], reachable)
            if refuted:
                with self.assertRaisesRegex(Invalid, 'do not determine'):
                    symbolic_relation(*args, 12, Budget(1000000))
            else:
                self.assertEqual(symbolic_relation(*args, 12, Budget(1000000)), (reachable, dependency))

    def test_nonseparating_paid_divisors_require_a_source_space_dependency_proof(self):
        source = {"nodes": [original(source=True) for _ in range(3)] +
                            [original([0, 1], 14), original([0, 2], 8), original([1, 2], 8), original([4, 5], 14)],
                  "outputs": [3, 6]}
        root = node("function", 6, [2, 3], [[0, 1]], [2, 3], [2, 3], 8, level=2)
        root.update(functional=True, care=table(2, 15), image_sources=[0, 1, 2])
        network = {"nodes": [node("source", i) for i in range(3)] +
                             [node("function", 3, [0, 1], [[0], [1]], [0, 1], [0, 1], 14, level=1), root],
                   "outputs": [3, 4], "depth": 2, "max_support": 2, "max_literals": 2, "max_series": 2,
                   "source_inverters": 0}
        valid = record(source, network)
        valid["schema_version"] = 3
        self.assertEqual(check(valid, Budget(10000)), 2)
        for mutation in [lambda r: r["network"]["nodes"][4].update(functional=False),
                         lambda r: r["network"]["nodes"][4].update(care=table(2, 7)),
                         lambda r: r["network"]["nodes"][4].update(image_sources=[0, 1]),
                         lambda r: r.update(schema_version=2)]:
            r = copy.deepcopy(valid)
            mutation(r)
            with self.assertRaises(Invalid):
                check(r, Budget(10000))
        r = copy.deepcopy(valid)
        r["source"]["nodes"][5]["table"] = table(2, 14)
        r["source_digest_fnv1a64"] = source_digest(r["source"])
        with self.assertRaisesRegex(Invalid, "divisors do not determine"):
            check(r, Budget(10000))

    def test_twin_completions_may_disagree_off_the_proved_image(self):
        r = image_completion()
        # Positive rail completes XOR to OR. The negative rail completes its
        # care values to XNOR, so BOTH are true on the unreachable tuple 11.
        nodes = r["network"]["nodes"]
        nodes += [node("source_inverter", 0, [0], negative=True),
                  node("function", 2, [6, 2], [[0], [1]], [0, 1], [0, 1], 7, negative=True, level=1),
                  node("function", 3, [6, 1], [[0], [1]], [0, 1], [0, 1], 13, negative=True, level=1),
                  node("function", 4, [7, 8, 3, 4], [[0, 1], [2, 3]], [2, 3], [2, 3], 9, negative=True, level=2)]
        nodes[-1].update(care=table(2, 7), image_sources=[0, 1])
        r["network"].update(max_literals=4, source_inverters=2)
        self.assertEqual(check(r, Budget(10000)), 1)
        nodes[-1]["completion"] = table(2, 1)
        with self.assertRaises(Invalid):
            check(r, Budget(10000))

    def test_exact_image_and_total_completion_are_independently_checked(self):
        self.assertEqual(check(image_completion(), Budget(10000)), 1)
        mutations = [
            lambda r: r["network"]["nodes"][5].update(care=table(2, 6)),
            lambda r: r["network"]["nodes"][5].update(care=table(2, 15)),
            lambda r: r["network"]["nodes"][5].update(image_sources=[0]),
            lambda r: r["network"]["nodes"][5].update(image_sources=[1, 0]),
            lambda r: r["network"]["nodes"][5].update(completion=table(2, 6)),
            lambda r: r.update(schema_version=1),
        ]
        for mutate in mutations:
            r = image_completion()
            mutate(r)
            with self.assertRaises(Invalid):
                check(r, Budget(10000))
        r = image_completion()
        r["source"]["nodes"][3]["table"] = table(2, 8)
        r["source_digest_fnv1a64"] = source_digest(r["source"])
        with self.assertRaises(Invalid):
            check(r, Budget(10000))

    def test_support_shrinking_constants_and_both_rails(self):
        self.assertEqual(check(shrinking(), Budget(10000)), 3)
        self.assertEqual(check(twins(), Budget(10000)), 2)

    def test_corruption_is_rejected_independently(self):
        mutations = [
            lambda r: r["network"]["nodes"][1]["completion"].update(words=["0000000000000001"]),
            lambda r: r["network"]["nodes"][1].update(witness_inputs=[0]),
            lambda r: r["network"]["nodes"][1].update(terms=[]),
            lambda r: r["network"]["nodes"][1].update(ports=[1]),
            lambda r: r["network"]["nodes"][1].update(level=2),
            lambda r: r["network"].update(outputs=[2, 1, 3]),
            lambda r: r["network"].update(max_literals=2),
            lambda r: r["source"].update(outputs=[4, 4, 5]),
            lambda r: r.update(recipe=[2, 0, 16, 4]),
        ]
        for mutation in mutations:
            r = copy.deepcopy(shrinking())
            mutation(r)
            with self.assertRaises(Invalid):
                check(r, Budget(10000))
        r = twins()
        r["network"]["nodes"][5]["negative"] = False
        with self.assertRaises(Invalid):
            check(r, Budget(10000))

    def test_changed_source_with_recomputed_digest_still_requires_semantic_proof(self):
        r = shrinking()
        r["source"]["nodes"][2]["table"] = table(2, 8)
        r["source_digest_fnv1a64"] = source_digest(r["source"])
        with self.assertRaises(Invalid):
            check(r, Budget(10000))

    def test_exhaustion_is_inconclusive(self):
        with self.assertRaises(Exhausted):
            check(shrinking(), Budget(0))
        with self.assertRaises(Exhausted):
            check(shrinking(), Budget(10000, max_nodes=1))

    def test_multiword_truth_tables(self):
        wide = {"inputs": 7, "words": ["0000000000000000", "8000000000000000"]}
        inputs = list(range(7))
        source = {"nodes": [original(source=True) for _ in inputs] + [original(inputs)], "outputs": [7]}
        source["nodes"][7]["table"] = wide
        function = node("function", 7, inputs, [inputs], inputs, inputs, level=1)
        function["completion"] = wide
        network = {"nodes": [node("source", i) for i in inputs] + [function], "outputs": [7],
                   "depth": 1, "max_support": 7, "max_literals": 7, "max_series": 7, "source_inverters": 0}
        r = record(source, network)
        r["recipe"] = [1, 7, 7, 7]
        self.assertEqual(check(r, Budget(10000)), 1)

    def test_cli_distinguishes_verified_invalid_and_inconclusive(self):
        checker = pathlib.Path(__file__).with_name("check_witness.py")
        with tempfile.TemporaryDirectory() as directory:
            archive = pathlib.Path(directory) / "witness.jsonl"
            archive.write_text(json.dumps(shrinking()) + "\n")
            def run(*args):
                return subprocess.run([sys.executable, str(checker), str(archive), *args],
                                      capture_output=True, text=True, check=False)
            verified = run()
            self.assertEqual(verified.returncode, 0, verified.stderr)
            self.assertEqual(json.loads(verified.stdout)["outputs"], 3)
            self.assertEqual(run("--work", "0").returncode, 2)
            archive.write_text("{broken\n")
            self.assertEqual(run().returncode, 1)
            archive.write_text("")
            self.assertEqual(run().returncode, 2)


if __name__ == "__main__":
    unittest.main()
