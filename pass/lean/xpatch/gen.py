#!/usr/bin/env python3
"""gen.py -- writes the example's Pyrope designs and its testbench into <outdir>.

Every design is built from ONE set of statement texts, so the product designs that sym_verify
proves contain the two designs' own statements; there, each register is renamed with a prefix
(a_ for A, b_ for B, m_ for the monitor).

  A.prp   an 8-bit accumulator acc (power-on value 5, adds din when en), a free-running 8-bit
          counter cnt, and a 16-word 8-bit memory arr (stores din at wa when we): the design of
          pass/lean/tools/tests/lhd_ckpt_import_test.sh with 16 words instead of 12, so every
          4-bit address is inside the memory, statements regrouped.
  B.prp   A with one more register, flg (power-on value false), a flag set when a write stores
          0xFF and never cleared. No output reads flg, so B's outputs are A's.
  product.prp        A and B side by side on the same inputs, plus the monitor m_flg, the same
          flag kept along A's run. Its one output, ok, is 1 exactly when B's state equals
          R(A's state, m_flg): B's flg == m_flg, and B's acc, cnt and arr == A's.
  product_wrong.prp  the same, except that a write of any other value clears the monitor's flag
          (a control that must fail).
  tb.stim  40 cycles of inputs for cert_trace (stim v1, absolute cycles), from a fixed linear
          congruential generator (no library RNG, so the file is the same on every machine),
          except that cycle 9 writes 0xFF (the generator draws no 0xFF in these 40 cycles).

`ok` and the monitor are written before A's and B's updates in the body, so they read each
cycle's start state. usage: gen.py <outdir>"""
import os, sys

HDR = "mod top(en:Bool, din:U8, wa:U4, we:Bool) -> (@OUTS@) {\n"

def sub(text, p):
    return text.replace("{p}", p)

# ---- design bodies: (declarations, updates); {p} is the register prefix -------------------
A = ("""  reg {p}acc:U8 = 5
  reg {p}cnt:U8 = 0
  reg {p}arr:[16]U8 = nil
""", """  if en { wrap {p}acc += din }
  wrap {p}cnt += 1
  if we { {p}arr[wa] = din }
""")
B = ("""  reg {p}acc:U8 = 5
  reg {p}cnt:U8 = 0
  reg {p}flg:Bool = false
  reg {p}arr:[16]U8 = nil
""", """  if en { wrap {p}acc += din }
  wrap {p}cnt += 1
  if we and (din == 0xFF) { {p}flg = true }
  if we { {p}arr[wa] = din }
""")
# the monitor's update, (what it keeps, its statement): the same flag as B's flg, and the
# control's flag, which a write of any other value clears
MON = ("set when a write stores 0xFF, never cleared",
       "  if we and (din == 0xFF) { m_flg = true }\n")
MON_WRONG = ("set when a write stores 0xFF, cleared when a write stores any other value",
             "  if we { m_flg = (din == 0xFF) }\n")

def standalone(name, body):
    d, u = body
    return (f"/*\n:name: {name}\n:type: simulation\n*/\n"
            + HDR.replace("@OUTS@", "fout:U8@[0], cout:U8@[0], mout:U8@[0]")
            + sub(d, '') + sub(u, '') + "  fout = acc\n  cout = cnt\n  mout = arr[3]\n}\n")

def arr_eq(x, y):
    return " and ".join(f"({x}[{k}] == {y}[{k}])" for k in range(16))

def product(name, mon):
    what, upd = mon
    s = f"/*\n:name: {name}\n:type: simulation\n*/\n" + HDR.replace("@OUTS@", "ok:Bool@[0]")
    s += "  // declarations: A (a_), B (b_), monitor (m_)\n" + sub(A[0], 'a_') + sub(B[0], 'b_')
    s += "  reg m_flg:Bool = false\n"
    s += "  // ok: B's state == R(A's state, m_flg), on the cycle's start state\n"
    s += "  ok = (b_acc == a_acc) and (b_cnt == a_cnt) and (b_flg == m_flg) and " + arr_eq('b_arr', 'a_arr') + "\n"
    s += f"  // the monitor: a flag {what}, read from the input\n" + upd
    s += "  // A's and B's own updates, verbatim\n" + sub(A[1], 'a_') + sub(B[1], 'b_')
    return s + "}\n"

# input ordinals of A and B (run.sh checks them against both designs' _io.json)
DIN, EN, WA, WE = 1, 2, 4, 5
FF_AT = 9   # the one cycle that writes 0xFF; the first checkpoint is cycle 10

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
        if c == FF_AT:                          # a write of 0xFF (it sets B's flg)
            din, we = 0xFF, 1
        lines += [f'at {c} in {EN} 1 {en}', f'at {c} in {DIN} 8 {din}',
                  f'at {c} in {WA} 4 {wa}', f'at {c} in {WE} 1 {we}']
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    files = {'A.prp': standalone('A', A), 'B.prp': standalone('B', B),
             'product.prp': product('product', MON),
             'product_wrong.prp': product('product_wrong', MON_WRONG),
             'tb.stim': stim()}
    for n, t in files.items():
        with open(os.path.join(out, n), 'w') as f:
            f.write(t)
    print('wrote', ' '.join(sorted(files)))
