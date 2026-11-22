#!/usr/bin/env python3
"""gen.py -- writes the example's Pyrope designs and its testbench into <outdir>.

Every design is built from ONE set of statement texts, so the product designs that sym_verify
proves contain the two designs' own statements; there, each register is renamed with a prefix
(a_ for A, b_ for B, m_ for the monitor).

  A.prp   an 8-bit accumulator acc (power-on value 5, adds din when en), a free-running 8-bit
          counter cnt, and a 12-word 8-bit memory arr (stores din at wa when we): the design of
          pass/lean/tools/tests/lhd_ckpt_import_test.sh, statements regrouped.
  B.prp   A with a register on its accumulator's input: inc holds the cycle's increment (din when
          en, else 0), and acc adds inc one cycle later.
  product.prp        A and B side by side on the same inputs, plus the monitor m_last (the
          increment A added in the previous cycle). Its one output, ok, is 1 exactly when B's
          state equals R(A's state, m_last): B's acc + m_last == A's acc, B's inc == m_last, B's
          cnt == A's cnt, B's arr == A's arr.
  product_wrong.prp  the same, except that R leaves out the pending increment: B's acc == A's acc
          (a control that must fail).
  tb.stim  40 cycles of inputs for cert_trace (stim v1, absolute cycles), from a fixed linear
          congruential generator (no library RNG, so the file is the same on every machine).

`ok` and the monitor are written before A's and B's updates in the body, so they read each
cycle's start state. usage: gen.py <outdir>"""
import os, sys

HDR = "mod top(en:Bool, din:U8, wa:U4, we:Bool) -> (@OUTS@) {\n"

def sub(text, p):
    return text.replace("{p}", p)

# ---- design bodies: (declarations, updates); {p} is the register prefix -------------------
A = ("""  reg {p}acc:U8 = 5
  reg {p}cnt:U8 = 0
  reg {p}arr:[12]U8 = nil
""", """  if en { wrap {p}acc += din }
  wrap {p}cnt += 1
  if we { {p}arr[wa] = din }
""")
B = ("""  reg {p}acc:U8 = 5
  reg {p}cnt:U8 = 0
  reg {p}inc:U8 = 0
  reg {p}arr:[12]U8 = nil
""", """  wrap {p}acc += {p}inc
  {p}inc = 0
  if en { {p}inc = din }
  wrap {p}cnt += 1
  if we { {p}arr[wa] = din }
""")

def standalone(name, body):
    d, u = body
    return (f"/*\n:name: {name}\n:type: simulation\n*/\n"
            + HDR.replace("@OUTS@", "fout:U8@[0], cout:U8@[0], mout:U8@[0]")
            + sub(d, '') + sub(u, '') + "  fout = acc\n  cout = cnt\n  mout = arr[3]\n}\n")

def arr_eq(x, y):
    return " and ".join(f"({x}[{k}] == {y}[{k}])" for k in range(12))

def product(name, acc_term):
    s = f"/*\n:name: {name}\n:type: simulation\n*/\n" + HDR.replace("@OUTS@", "ok:Bool@[0]")
    s += "  // declarations: A (a_), B (b_), monitor (m_)\n" + sub(A[0], 'a_') + sub(B[0], 'b_')
    s += "  reg m_last:U8 = 0\n"
    s += "  // ok: B's state == R(A's state, m_last), on the cycle's start state\n"
    s += (f"  mut m_sum:U8 = 0\n  wrap m_sum = {acc_term}\n"
          "  ok = (m_sum == a_acc) and (b_inc == m_last) and (b_cnt == a_cnt) and " + arr_eq('b_arr', 'a_arr') + "\n")
    s += "  // the monitor: the increment A adds in this cycle, read from the input\n"
    s += "  mut m_add:U8 = 0\n  if en { m_add = din }\n  m_last = m_add\n"
    s += "  // A's and B's own updates, verbatim\n" + sub(A[1], 'a_') + sub(B[1], 'b_')
    return s + "}\n"

# input ordinals of A and B (run.sh checks them against both designs' _io.json)
DIN, EN, WA, WE = 1, 2, 4, 5

def stim(cycles=40, seed=2026):
    x = seed
    def nxt():
        nonlocal x
        x = (1103515245 * x + 12345) % (1 << 31)
        return x >> 16
    lines = ['stim v1']
    for c in range(cycles):
        en = 1 if nxt() % 4 else 0              # en high in about 3 cycles of 4
        din, wa, we = nxt() % 256, nxt() % 12, nxt() % 2
        lines += [f'at {c} in {EN} 1 {en}', f'at {c} in {DIN} 8 {din}',
                  f'at {c} in {WA} 4 {wa}', f'at {c} in {WE} 1 {we}']
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    files = {'A.prp': standalone('A', A), 'B.prp': standalone('B', B),
             'product.prp': product('product', 'b_acc + m_last'),
             'product_wrong.prp': product('product_wrong', 'b_acc'),
             'tb.stim': stim()}
    for n, t in files.items():
        with open(os.path.join(out, n), 'w') as f:
            f.write(t)
    print('wrote', ' '.join(sorted(files)))
