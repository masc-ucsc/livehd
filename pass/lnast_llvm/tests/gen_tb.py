"""Generate matching Pyrope and optional independent Icarus testbenches."""
import json
import sys

ports = json.load(open(sys.argv[1]))
lg_name, top, cycles = sys.argv[2:5]
external = "--iverilog" in sys.argv[5:]
every = int(next((a for a in sys.argv[5:] if a != "--iverilog"), "1"))
inputs, outputs = ports["inputs"], ports["outputs"]


def stimulus(i, cycle):
    p, q = 2654435761 + 1234567 * i, 40503 + 2 * i
    return f"(((({cycle} + 1) * {p}) + (({cycle} + 1) * ({cycle} + 1) * {q})) >> 3)"


if external:
    clocked = top in {"comb_reg", "repeated_state"}
    print("module oracle; longint unsigned c;")
    bindings = []
    for i, f in enumerate(inputs):
        print(f"reg [{f['bits'] - 1}:0] stimulus_{i};")
        bindings.append(f".{f['name']}(stimulus_{i})")
    if clocked:
        print("reg clk;")
        bindings.append(".clk(clk)")
    print(f"{top} dut({', '.join(bindings)}); initial begin")
    if clocked:
        print("clk = 0;")
    print(f"for (c=0; c<{cycles}; c=c+1) begin")
    for i, f in enumerate(inputs):
        print(f"stimulus_{i} = {stimulus(i, 'c')};")
    if clocked:
        print("#1; clk = 1; #1; clk = 0;")
    fmt = " ".join(f"{f['name']}=%0d" for f in outputs)
    signals = ", ".join(f"dut.{f['name']}" for f in outputs)
    print(f'#1; $display("TRACE %0d {fmt}", c, {signals}); end end endmodule')
else:
    print(f'const dut_t = import("lg:{lg_name}")\ntest trace(cycles:U32={cycles}) {{\n  mut dut = dut_t\n  tick cycles {{')
    for i, f in enumerate(inputs):
        expr = f"{stimulus(i, 'clock')} & {hex((1 << f['bits']) - 1)}"
        if f["sign"]:
            print(f"    const raw{i} = {expr}")
            expr = f"raw{i} - ((raw{i} & {hex(1 << (f['bits'] - 1))}) << 1)"
        print(f"    dut.{f['name']} = {expr}")
    print("    step")
    fmt = " ".join(f"{f['name']}={{dut.{f['name']}}}" for f in outputs)
    if every > 1:
        print(f'    if clock % {every} == {every - 1} {{ puts("TRACE {{clock}} {fmt}") }}')
    else:
        print(f'    puts("TRACE {{clock}} {fmt}")')
    print("  }\n}")
