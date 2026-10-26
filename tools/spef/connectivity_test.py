import io
import unittest

from tools.spef.connectivity import audit


HEADER = '*SPEF "ieee 1481-1999"\n*DELIMITER :\n'


class Connectivity(unittest.TestCase):
    def check(self, body, expected=None, **kwargs):
        return audit(io.StringIO(HEADER+body), expected or {'n': {'u:Y', 'v:A'}}, **kwargs)

    def test_names_escapes_and_internal_nodes(self):
        x = self.check('''*NAME_MAP
*1 n
*2 u\\.0
*3 v
*D_NET *1 2
*CONN
*I *2:Y O
*I *3:A I
*CAP
1 *2:Y 0
2 *1:1 .3
*RES
1 *2:Y *1:1 1
2 *1:1 *3:A 2
*END
''', {'n': {'u.0:Y', 'v:A'}})
        self.assertTrue(x['passed'], x)

    def test_coupling_never_connects_terminals(self):
        x = self.check('''*D_NET n 2
*CONN
*I u:Y O
*I v:A I
*CAP
1 u:Y v:A 2
*RES
1 u:Y n:1 1
*END
''')
        self.assertEqual(x['coupling_capacitors'], 1)
        self.assertEqual(x['disconnected_nets'], 1)
        self.assertFalse(x['passed'])

    def test_zero_resistance_loops_and_parallel_edges_are_conductive(self):
        x = self.check('''*D_NET n 0
*CONN
*I u:Y O
*I v:A I
*RES
1 u:Y n:1 0
2 n:1 v:A 2
3 u:Y v:A 3
4 u:Y v:A 4
*END
''')
        self.assertTrue(x['passed'], x)

    def test_foreign_coupling_endpoint_may_be_first(self):
        x = self.check('''*D_NET n 2
*CONN
*I u:Y O
*I v:A I
*CAP
1 n:other:7 n:1 2
2 u:Y other:8 1
*RES
1 u:Y n:1 1
2 n:1 v:A 1
*END
''')
        self.assertEqual(x['coupling_capacitors'], 2)
        self.assertEqual(x['orphan_components'], 0)
        self.assertTrue(x['passed'], x)

    def test_missing_and_extra_pins_do_not_pass(self):
        x = self.check('''*D_NET n 0
*CONN
*I u:Y O
*I wrong:A I
*RES
1 u:Y wrong:A 1
*END
''')
        self.assertEqual(x['pin_coverage_failures'], 1)
        self.assertFalse(x['passed'])

    def test_abutment_requires_explicit_independent_evidence(self):
        self.assertFalse(self.check('')['passed'])
        self.assertTrue(self.check('', abutted={'n'})['passed'])
        text = '*D_NET n 0\n*CONN\n*I u:Y O\n*I v:A I\n*END\n'
        self.assertFalse(self.check(text)['passed'])
        self.assertTrue(self.check(text, abutted={'n'})['passed'])

    def test_floating_island_is_not_a_complete_net(self):
        x = self.check('''*D_NET n 0
*CONN
*I u:Y O
*I v:A I
*RES
1 u:Y v:A 1
2 n:8 n:9 1
*END
''')
        self.assertEqual(x['orphan_components'], 1)
        self.assertFalse(x['passed'])

    def test_malformed_or_ambiguous_input_is_rejected(self):
        for text in [
            '*D_NET n 0\n*CONN\n*I *9:A I\n*END\n',
            '*D_NET n 0\n*RES\n1 u:Y v:A -1\n*END\n',
            '*D_NET n 0\n*RES\n1 u:Y v:A nan\n*END\n',
            '*D_NET n 0\n*END\n*D_NET n 0\n*END\n',
            '*D_NET n 0\n*CONN\n*I u:Y O\n*I u:Y O\n*END\n',
            '*D_NET n 0\n',
        ]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.check(text)

    def test_single_pin_net_can_be_absent(self):
        x = self.check('', {'n': {'u:Y'}})
        self.assertTrue(x['passed'], x)


if __name__ == '__main__':
    unittest.main()
