#!/usr/bin/env python3
"""Generate synthetic bridge theories to compare phi/nodes lookup representations.

Faithful to the real per-node proof shape from Piece B:

    lemma rec_k: "phi k = eval_node G phi k"

which forces the prover to reduce (a) a `nodes G k` lookup and (b) one `phi`
lookup per dependency.  How those two are represented is the whole question --
Lean measured monolithic forms as O(N^2) and never-finishing at DINO scale.

Design: a chain of N nodes, node k = Op_Not(node k-1), node 1 = Op_Not(source).
All 8-bit, so the op cost is constant and only the lookup representation varies.

Candidates:
  flat   nodes via nodes_of_list (map_of over a list literal), phi via a nested
         if-chain.  The shape the emitter produces today.
  bt     nodes and phi via a balanced binary-tree datatype + recursive find.
         The direct port of Lean's fix.
"""
import sys

W = 8


def fv_defs(n):
    out = [f'definition fv1 :: "{W} word" where "fv1 = Bit_Operations.not (ucast src_val)"']
    for k in range(2, n + 1):
        out.append(f'definition fv{k} :: "{W} word" where '
                   f'"fv{k} = Bit_Operations.not (ucast fv{k-1})"')
    return "\n".join(out)


def certs_def(n):
    entries = []
    for k in range(1, n + 1):
        dep = 100000 if k == 1 else k - 1
        entries.append(f"    \\<lparr>nid = {k}, op = Op_Not, width = {W}, deps = [{dep}]\\<rparr>")
    return ('definition certs :: "node_cert list" where\n  "certs = [\n'
            + ",\n".join(entries) + "]\"")


def bst(pairs, val):
    """Balanced BST literal over sorted (key, value-string) pairs."""
    if not pairs:
        return "BT.Lf"
    mid = len(pairs) // 2
    k, v = pairs[mid]
    lo = bst(pairs[:mid], val)
    hi = bst(pairs[mid + 1:], val)
    return f"(BT.Nd {k} ({v}) {lo} {hi})"


def gen(cand, n):
    parts = [f"""theory Bake
  imports "LGraph-Translation-Correctness.Translation_OpBridge"
begin

lemma not{W}: "bv_not {W} (bvenc (a::{W} word)) = bvenc (Bit_Operations.not a)"
  using not_bridge[where 'w = "{W}", of a] by simp

definition src_val :: "{W} word" where "src_val = 5"

definition src_env :: "nat \\<Rightarrow> bv" where
  "src_env m = (if m = 100000 then bvenc src_val else bvenc (0::{W} word))"

{fv_defs(n)}

{certs_def(n)}

definition topo_list :: "nat list" where
  "topo_list = [{', '.join(str(k) for k in range(1, n + 1))}]"
"""]

    if cand == "flat":
        phi_chain = "".join(
            f"if m = {k} then bvenc fv{k} else " for k in range(1, n + 1))
        parts.append(f"""
definition G :: graph_cert where
  "G = \\<lparr>topo = topo_list, sources = [100000], nodes = nodes_of_list certs\\<rparr>"

definition phi :: "nat \\<Rightarrow> bv" where
  "phi m = ({phi_chain}src_env m)"
""")
        def proof(k):
            dep = 100000 if k == 1 else k - 1
            return (f'  by (simp add: phi_def G_def certs_def nodes_of_list_def '
                    f'eval_node_def src_env_def fv{k}_def '
                    f'not{W})')

    elif cand == "bt":
        node_pairs = [
            (k, f"\\<lparr>nid = {k}, op = Op_Not, width = {W}, "
                f"deps = [{100000 if k == 1 else k-1}]\\<rparr>")
            for k in range(1, n + 1)]
        phi_pairs = [(k, f"bvenc fv{k}") for k in range(1, n + 1)]
        parts.append(f"""
datatype 'a BT = Lf | Nd nat 'a "'a BT" "'a BT"

fun bt_find :: "'a BT \\<Rightarrow> nat \\<Rightarrow> 'a option" where
  "bt_find BT.Lf _ = None"
| "bt_find (BT.Nd k v lo hi) m =
     (if m < k then bt_find lo m else if m = k then Some v else bt_find hi m)"

definition nodes_tree :: "node_cert BT" where
  "nodes_tree = {bst(node_pairs, None)}"

definition nodes_fn :: "nat \\<Rightarrow> node_cert option" where
  "nodes_fn m = bt_find nodes_tree m"

definition G :: graph_cert where
  "G = \\<lparr>topo = topo_list, sources = [100000], nodes = nodes_fn\\<rparr>"

definition phi_tree :: "bv BT" where
  "phi_tree = {bst(phi_pairs, None)}"

definition phi :: "nat \\<Rightarrow> bv" where
  "phi m = (case bt_find phi_tree m of None \\<Rightarrow> src_env m | Some v \\<Rightarrow> v)"
""")
        def proof(k):
            return (f'  by (simp add: phi_def G_def nodes_fn_def nodes_tree_def '
                    f'phi_tree_def eval_node_def src_env_def fv{k}_def '
                    f'not{W})')

    elif cand in ("eqns", "fun"):
        node_lit = lambda k: (f"\\<lparr>nid = {k}, op = Op_Not, width = {W}, "
                              f"deps = [{100000 if k == 1 else k-1}]\\<rparr>")
        node_pairs = [(k, node_lit(k)) for k in range(1, n + 1)]
        phi_pairs = [(k, f"bvenc fv{k}") for k in range(1, n + 1)]
        if cand == "eqns":
            parts.append(f"""
datatype 'a BT = Lf | Nd nat 'a "'a BT" "'a BT"

fun bt_find :: "'a BT \\<Rightarrow> nat \\<Rightarrow> 'a option" where
  "bt_find BT.Lf _ = None"
| "bt_find (BT.Nd k v lo hi) m =
     (if m < k then bt_find lo m else if m = k then Some v else bt_find hi m)"

definition nodes_tree :: "node_cert BT" where
  "nodes_tree = {bst(node_pairs, None)}"

definition nodes_fn :: "nat \\<Rightarrow> node_cert option" where
  "nodes_fn m = bt_find nodes_tree m"

definition G :: graph_cert where
  "G = \\<lparr>topo = topo_list, sources = [100000], nodes = nodes_fn\\<rparr>"

definition phi_tree :: "bv BT" where
  "phi_tree = {bst(phi_pairs, None)}"

definition phi :: "nat \\<Rightarrow> bv" where
  "phi m = (case bt_find phi_tree m of None \\<Rightarrow> src_env m | Some v \\<Rightarrow> v)"
""")
            for k in range(1, n + 1):
                parts.append(f'\nlemma phi_at{k} [simp]: "phi {k} = bvenc fv{k}"\n'
                             f'  by (simp add: phi_def phi_tree_def)')
                parts.append(f'\nlemma nodes_at{k} [simp]: "nodes_fn {k} = Some ({node_lit(k)})"\n'
                             f'  by (simp add: nodes_fn_def nodes_tree_def)')
            parts.append('\nlemma phi_src [simp]: "phi 100000 = bvenc src_val"\n'
                         '  by (simp add: phi_def phi_tree_def src_env_def)')
        else:  # fun
            phi_eqs = "\n| ".join(f'"phi {k} = bvenc fv{k}"' for k in range(1, n + 1))
            nod_eqs = "\n| ".join(f'"nodes_fn {k} = Some ({node_lit(k)})"'
                                  for k in range(1, n + 1))
            parts.append(f"""
fun phi :: "nat \\<Rightarrow> bv" where
  {phi_eqs}
| "phi m = src_env m"

fun nodes_fn :: "nat \\<Rightarrow> node_cert option" where
  {nod_eqs}
| "nodes_fn m = None"

definition G :: graph_cert where
  "G = \\<lparr>topo = topo_list, sources = [100000], nodes = nodes_fn\\<rparr>"
""")

        def proof(k):
            return (f'  by (simp del: One_nat_def '
                    f'add: G_def eval_node_def src_env_def fv{k}_def not{W})')

    for k in range(1, n + 1):
        parts.append(f'\nlemma rec{k}: "phi {k} = eval_node G phi {k}"\n{proof(k)}')

    parts.append("\n\nend\n")
    return "".join(parts)


if __name__ == "__main__":
    cand, n = sys.argv[1], int(sys.argv[2])
    sys.stdout.write(gen(cand, n))
