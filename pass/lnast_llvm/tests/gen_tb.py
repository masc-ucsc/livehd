# Writes the Pyrope testbench matching lnast_llvm_run's stimulus for the ports in kernel.json.
import json, sys
js = json.load(open(sys.argv[1])); lg_name = sys.argv[2]; top = sys.argv[3]; cycles = sys.argv[4]
out = ['const dut_t = import("lg:%s")' % lg_name, 'test trace(cycles:U32=%s) {' % cycles, '  mut dut = dut_t', '  tick cycles {']
for i, f in enumerate(js['inputs']):
    p = 2654435761 + 1234567 * i; q = 40503 + 2 * i
    expr = '((((clock + 1) * %d) + ((clock + 1) * (clock + 1) * %d)) >> 3) & %s' % (p, q, hex((1 << f['bits']) - 1))
    if f['sign']:
        out.append('    const raw%d = %s' % (i, expr))
        expr = 'raw%d - ((raw%d & %s) << 1)' % (i, i, hex(1 << (f['bits'] - 1)))
    out.append('    dut.%s = %s' % (f['name'], expr))
out.append('    step')
fmt = ' '.join('%s={dut.%s}' % (o['name'], o['name']) for o in js['outputs'])
out.append('    puts("TRACE {clock} %s")' % fmt)
out += ['  }', '}']
print('\n'.join(out))
