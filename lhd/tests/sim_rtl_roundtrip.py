#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Original RTL versus both LHD input paths, with LEC on generated Pyrope."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


CASES = (
    "latch_window", "cache_control", "cache_control_bundle", "array_copy",
    "bundle_link", "partial_pipeline", "function_accumulate",
    "packed_bulk_override", "next_state_memory", "data_latch_edges", "gated_latch_pair",
)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--case", choices=CASES, help="Run one independent regression")
parser.add_argument("--backend", choices=("slop", "llvm"), help="Run one simulator backend")
ARGS = parser.parse_args()

LHD = str(Path(os.environ.get("LHD", "lhd/lhd")).resolve())
WORK = Path(os.environ.get("TEST_TMPDIR") or tempfile.mkdtemp(prefix="lhd-rtl-roundtrip-")).resolve()
VERILATOR = os.environ.get("VERILATOR") or shutil.which("verilator")


def run(args, log):
    with log.open("w") as out:
        result = subprocess.run(list(map(str, args)), stdout=out, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{args[0]} exited {result.returncode}: {log}\n{log.read_text()}")
    return log.read_text()


def check(name, rtl, stimulus, checks, cpp):
    if ARGS.case and ARGS.case != name:
        return
    work = WORK / name
    work.mkdir(parents=True, exist_ok=True)
    source = work / "top.sv"
    source.write_text(rtl)
    run([LHD, "compile", source, "--top", "top", "--emit-dir", f"lg:{work}/lg",
         "--emit-dir", f"pyrope:{work}/prp", "--workdir", work / "compile", "--set", "compile.upass.inline=false"], work / "compile.log")
    # LEC is independent of the simulator backend; check it once in the Slop
    # leg (or the unsplit manual run), alongside the external Verilator twin.
    if ARGS.backend != "llvm":
        lec = run([LHD, "lec", "--ref", source, "--impl", work / "prp/top.prp", "--top", "top",
                   "--workdir", work / "lec", "--set", "compile.upass.inline=false"], work / "lec.log")
        records = [json.loads(line) for line in lec.splitlines() if line.startswith('{')]
        if records[-1].get("lec", {}).get("verdict") != "proven":
            raise RuntimeError(f"round-trip LEC did not prove equivalence:\n{lec}")
    for language in ("verilog", "pyrope"):
        entry = 'lg:top' if language == "verilog" else 'prp/top.top'
        tb = work / f"{language}_tb.prp"
        tb.write_text(f'''const design = import("{entry}")
test top.compare {{
  mut dut = design
  tick 8 {{
{stimulus}
    step
{checks}
  }}
}}
''')
        inputs = [f"lg:{work}/lg", tb] if language == "verilog" else [tb]
        # Both C++/Slop and LLVM must implement the same phase dependencies.
        for backend in ((ARGS.backend,) if ARGS.backend else ("slop", "llvm")):
            run([LHD, "sim", *inputs, "--workdir", work / f"{language}-{backend}",
                 "--set", "sim.tune.profile=off", "--set", f"sim.tune.backend={backend}",
                 "--set", "compile.upass.inline=false"],
                work / f"{language}-{backend}.log")
    if VERILATOR and ARGS.backend != "llvm":
        twin = work / "main.cpp"
        twin.write_text('''#include "Vtop.h"
#include "verilated.h"
int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  Vtop dut;
  dut.clk = 0;
  dut.eval();
''' + cpp + '\n}\n')
        run([VERILATOR, "--cc", "--exe", "--build", "--top-module", "top", "--Mdir", work / "vobj",
             source, twin], work / "verilator-build.log")
        run([work / "vobj/Vtop"], work / "verilator-run.log")
    elif not VERILATOR and ARGS.backend != "llvm":
        print("SKIP: external Verilator comparison (set VERILATOR to enable)")
    print(f"PASS: {name}: RTL and generated Pyrope, "
          f"{'LEC and ' if ARGS.backend != 'llvm' else ''}{ARGS.backend or 'Slop and LLVM'} simulation")



check("gated_latch_pair", '''module top(input logic clk, en, clear_pair, input logic [7:0] d,
 output logic [7:0] low_q = 0, high_q = 0, captured = 0);
always_latch if (clear_pair) low_q = 0; else if (!clk && en) low_q = high_q;
always_latch if (clear_pair) high_q = 0; else if (clk && en) high_q = d;
always_ff @(negedge clk) captured <= high_q;
endmodule
''', '''    dut.en = (clock % 2) == 0
    dut.clear_pair = clock == 0
    dut.d = 100 + clock''', '''    const count = if clock < 2 { 0 } else { 100 + clock - (clock % 2) }
    assert(dut.high_q == count, "high window tracks data only when enabled")
    assert(dut.low_q == count, "low window settles after the fall and holds when disabled")
    assert(dut.captured == count, "negedge captures the settled high window")''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.en = c % 2 == 0;
    dut.clear_pair = c == 0;
    dut.d = 100 + c;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    dut.clk = 0;
    dut.eval();
    const unsigned count = c < 2 ? 0 : 100 + c - c % 2;
    if (dut.high_q != count || dut.low_q != count || dut.captured != count) return 1;
  }''')

check("data_latch_edges", '''module top(input logic clk, en, clear_latch, input logic [7:0] d,
 output logic [7:0] rise_q = 0, fall_q = 0, rise_latch = 0, fall_latch = 0,
 output logic [7:0] captured = 0);
logic en_q = 0;
always_ff @(posedge clk) begin
  rise_q <= d;
  en_q <= en;
end
always_ff @(negedge clk) begin
  fall_q <= d + 20;
  captured <= rise_latch;
end
always_latch if (clear_latch) rise_latch = 7; else if (en) rise_latch = rise_q;
always_latch if (en_q) fall_latch = fall_q;
endmodule
''', '''    dut.clear_latch = clock == 4
    dut.en = (clock % 2) == 0
    dut.d = 100 + clock''', '''    const held = 100 + (clock - (clock % 2))
    const held_rise = if (clock == 4) or (clock == 5) { 7 } else { held }
    assert(dut.rise_q == 100 + clock, "posedge register commits")
    assert(dut.fall_q == 120 + clock, "negedge register commits")
    assert(dut.rise_latch == held_rise, "data latch settles after rising edge, resets and holds when closed")
    assert(dut.fall_latch == held + 20, "data latch settles after falling edge and holds when closed")
    assert(dut.captured == held_rise, "negedge register samples settled rising-edge latch cone")''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.clear_latch = c == 4;
    dut.en = (c % 2) == 0;
    dut.d = 100 + c;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    dut.clk = 0;
    dut.eval();
    const unsigned held = 100 + c - c % 2;
    const unsigned held_rise = (c == 4 || c == 5) ? 7 : held;
    if (dut.rise_q != 100 + c || dut.fall_q != 120 + c ||
        dut.rise_latch != held_rise || dut.fall_latch != held + 20 || dut.captured != held_rise) return 1;
  }''')


# Observe a low-transparent latch after the fall has settled. Reading only its
# closing-edge stored Q delays the newly launched data until the following tick.
# No register samples a latch on its OPENING edge (a blocking-assignment race).
check("latch_window", '''module top(input logic clk, input logic [7:0] d,
 output logic [7:0] low_q, high_q, captured, low_from_fall, high_from_fall, rise_captured = 0);
logic [7:0] launch_q = 0;
logic [7:0] fall_launch_q = 0;
always_ff @(posedge clk) launch_q <= d;
always_ff @(negedge clk) fall_launch_q <= d + 1;
always_latch if (!clk) low_q = launch_q;
always_latch if (clk) high_q = launch_q;
always_latch if (!clk) low_from_fall = fall_launch_q;
always_latch if (clk) high_from_fall = fall_launch_q;
always_ff @(posedge clk) rise_captured <= low_from_fall;
always_ff @(negedge clk) captured <= high_q;
endmodule
''', "    dut.d = 200 + clock", '''    assert(dut.low_q == 200 + clock, "low latch follows data after the fall")
    assert(dut.high_q == 200 + clock, "high latch holds the settled high-window data")
    assert(dut.captured == 200 + clock, "falling register samples the closing high latch")
    assert(dut.low_from_fall == 201 + clock, "low latch follows the newly updated negedge flop")
    if clock != 0 {
      assert(dut.high_from_fall == 200 + clock, "high latch holds the value from before the fall")
      assert(dut.rise_captured == 200 + clock, "rising register samples the closing low latch")
    }''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.d = 200 + c;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    dut.clk = 0;
    dut.eval();
    if (dut.low_q != 200 + c || dut.high_q != 200 + c || dut.captured != 200 + c ||
        dut.low_from_fall != 201 + c) return 1;
    if (c && (dut.high_from_fall != 200 + c || dut.rise_captured != 200 + c)) return 1;
  }''')

# CVA6 cache-control shapes: a forward-driven ready net, an inlined function
# argument, and fields 80..84 of a packed child output. Test both mux arms and
# both cacheability outcomes; translating the net into an early-read mut or
# losing the function actual argument cannot pass this positive check.
check("cache_control", '''module lane(input logic [84:0] a, b, input logic sel, output logic [84:0] q);
assign q = sel ? a : b;
endmodule
module top(input logic clk, req, idle, input logic [63:0] addr,
 output logic ready, cacheable, output logic [63:0] selected, captured,
 output logic [4:0] attrs);
logic [63:0] previous = 0;
logic [84:0] packed_req;
function automatic logic in_range(input logic [63:0] address);
 return address >= 64'h80000000 && address < 64'hc0000000;
endfunction
assign selected = ready && req ? addr : previous;
assign cacheable = in_range({selected[63:12], 12'b0});
always_comb begin
 ready = 0;
 if (idle) ready = 1;
end
always_ff @(posedge clk) previous <= selected;
assign captured = previous;
lane bus(.a({5'd11, 16'b0, selected}), .b({5'd19, 16'b0, selected}), .sel(idle), .q(packed_req));
assign attrs = packed_req[84:80];
endmodule
''', '''    dut.req = 1
    dut.idle = u1((clock & 1) == 0)
    dut.addr = if (clock & 2) == 0 { 0x80000004 } else { 0x10000004 }''', '''    const expected = if (clock & 2) == 0 { 0x80000004 } else { 0x10000004 }
    assert(dut.ready == u1((clock & 1) == 0))
    assert(dut.selected == expected)
    assert(dut.captured == expected)
    assert(dut.cacheable == u1((clock & 2) == 0))
    assert(dut.attrs == if (clock & 1) == 0 { 11 } else { 19 })''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.req = 1;
    dut.idle = (c & 1) == 0;
    dut.addr = (c & 2) == 0 ? 0x80000004 : 0x10000004;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    dut.clk = 0;
    dut.eval();
    if (dut.ready != ((c & 1) == 0) || dut.selected != dut.addr || dut.captured != dut.addr
        || dut.cacheable != ((c & 2) == 0) || dut.attrs != ((c & 1) == 0 ? 11 : 19)) return 1;
  }''')

# Exact CVA6 failure shapes: struct-output feedback plus a runtime argument
# consumed only inside a deferred, configuration-dependent function loop.
check("cache_control_bundle", '''package cfg;
typedef struct packed {int unsigned count; logic [63:0] base; logic [63:0] len;} cfg_t;
function automatic logic range_check(logic [63:0] base, len, address);
return address >= base && {1'b0,address} < (65'(base)+len);
endfunction
function automatic logic cacheable(cfg_t c, logic [63:0] address);
logic [1:0] pass;
pass = '0;
for (int unsigned k=0; k<c.count; k++) pass[k] = range_check(c.base,c.len,address);
return |pass;
endfunction
endpackage
module top #(parameter cfg::cfg_t Cfg = '{1,64'h80000000,64'h40000000})(input logic clk,req,idle,input logic[63:0] addr,
output logic [7:0] cfg_width,output logic cacheable,output logic[63:0] selected,output struct packed {logic ready; logic valid;} response);
localparam WIDTH = 64 / Cfg.count;
assign cfg_width = 8'(WIDTH);
logic[63:0] previous=0;
assign selected = response.ready && req ? addr : previous;
assign cacheable = cfg::cacheable(Cfg,{selected[63:12],12'b0});
always_comb begin
response.ready=0;
response.valid=selected != 0;
if(idle) response.ready=1;
end
always_ff @(posedge clk) previous<=selected;
endmodule
''', '''    dut.req = 1
    dut.idle = u1((clock & 1) == 0)
    dut.addr = if (clock & 2) == 0 { 0x80000004 } else { 0x10000004 }''', '''    const expected = if (clock & 2) == 0 { 0x80000004 } else { 0x10000004 }
    assert(dut.response.ready == u1((clock & 1) == 0))
    assert(dut.response.valid == 1)
    assert(dut.cfg_width == 64)
    assert(dut.selected == expected)
    assert(dut.cacheable == u1((clock & 2) == 0))''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.req = 1;
    dut.idle = (c & 1) == 0;
    dut.addr = (c & 2) == 0 ? 0x80000004 : 0x10000004;
    dut.eval();
    dut.clk = 1; dut.eval();
    dut.clk = 0; dut.eval();
    if (dut.cfg_width != 64 || dut.response != (((c & 1) == 0 ? 2 : 0) | 1) || dut.selected != dut.addr
        || dut.cacheable != ((c & 2) == 0)) return 1;
  }''')

# A next-state array must preserve the whole copy before an indexed override.
check("array_copy", '''module top(input logic clk, input logic [1:0] addr,
 input logic [7:0] data, output logic [7:0] q);
typedef struct packed {logic [7:0] data;} item_t;
item_t mem[4] = '{default:'0};
item_t next_mem[4];
always_comb begin
 next_mem = mem;
 next_mem[addr].data = data;
end
always_ff @(posedge clk) mem <= next_mem;
assign q = mem[addr].data;
endmodule
''', '''    dut.addr = clock & 3
    dut.data = 200 + clock''', '''    assert(dut.q == 200 + clock)''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.addr = c & 3; dut.data = 200 + c;
    dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
    if (dut.q != 200 + c) return 1;
  }''')

# Multiple drivers of disjoint local struct fields form a coarse module cycle.
# Child input bindings must read the resolved response, not its early poison.
check("bundle_link", '''typedef struct packed {logic ready; logic valid; logic [7:0] data;} response_t;
module consumer(input logic clk, input response_t response,
 output logic request, output logic [7:0] captured = 0);
assign request = response.valid;
always_ff @(posedge clk) if (response.ready && response.valid) captured <= response.data;
endmodule
module top(input logic clk, ready, valid, input logic [7:0] data,
 output logic request, output logic [7:0] captured);
response_t link;
assign link.valid = valid;
assign link.data = data;
consumer sink(.clk(clk), .response(link), .request(request), .captured(captured));
assign link.ready = ready && request;
endmodule
''', '''    dut.ready = u1((clock & 1) == 0)
    dut.valid = 1
    dut.data = 200 + clock''', '''    assert(dut.request == 1)
    assert(dut.captured == 200 + (clock & 6))''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.ready = (c & 1) == 0; dut.valid = 1; dut.data = 200 + c;
    dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
    if (dut.request != 1 || dut.captured != 200 + (c & 6)) return 1;
  }''')

# A clocked consumer can precede the continuous slice of a mixed state vector.
check("partial_pipeline", '''module top(input logic clk, rst, input logic [7:0] data,
 output logic [7:0] q, output logic [7:0] live);
logic [1:0][7:0] stages;
always_ff @(posedge clk or posedge rst)
 if (rst) stages[1] <= 0; else stages[1] <= stages[0];
assign q = stages[1];
assign live = stages[0];
assign stages[0] = data;
endmodule
''', '''    dut.rst = u1(clock == 0)
    dut.data = 200 + clock''', '''    assert(dut.live == 200 + clock)
    assert(dut.q == if clock == 0 { 0 } else { 200 + clock })''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.rst = c == 0; dut.data = 200 + c;
    dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
    if (dut.live != 200 + c || dut.q != (c == 0 ? 0 : 200 + c)) return 1;
  }''')

# Inlining a boolean conversion must preserve each call's intermediate value.
check("function_accumulate", '''module top(input logic clk, input logic [15:0] data,
 output logic q);
function automatic logic is_zero(input logic [7:0] value);
 if (value == 0) return 1; else return 0;
endfunction
always_comb begin
 q = 0;
 for (int i=0; i<2; ++i) q |= (is_zero(data[i*8+:8]) != 0);
end
endmodule
''', '''    dut.data = if (clock & 1) == 0 { 0x1200 } else { 0x1234 }''', '''    assert(dut.q == u1((clock & 1) == 0))''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.data = (c & 1) == 0 ? 0x1200 : 0x1234;
    dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
    if (dut.q != ((c & 1) == 0)) return 1;
  }''')

# A bulk state update followed by a packed field override retains the other lanes.
check("packed_bulk_override", '''module top(input logic clk, rst, input logic [2:0] addr,
 input logic [7:0] data, output logic [7:0] q);
typedef struct packed {logic valid; logic [7:0] data; logic [4:0] rd;} item_t;
item_t [7:0] mem_q, mem_n;
always_comb begin
 mem_n = mem_q;
 mem_n[addr] = {1'b1,data,5'b00011};
end
always_ff @(posedge clk or posedge rst)
 if (rst) mem_q <= '0;
 else begin
  mem_q <= mem_n;
  mem_q[0].rd <= mem_n[0].rd;
 end
assign q = mem_q[addr].data;
endmodule
''', '''    dut.rst = u1(clock == 0)
    dut.addr = clock & 7
    dut.data = 200 + clock''', '''    assert(dut.q == if clock == 0 { 0 } else { 200 + clock })''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.rst = c == 0; dut.addr = c & 7; dut.data = 200 + c;
    dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
    if (dut.q != (c == 0 ? 0 : 200 + c)) return 1;
  }''')

# Whole reads of a combinational memory include its indexed writes this cycle.
check("next_state_memory", '''module top(input logic clk, rst, input logic [2:0] addr,
 input logic [7:0] data, output logic [7:0] q);
typedef struct packed {logic [7:0] data;} item_t;
item_t mem_q[1:0][3:0], mem_d[1:0][3:0];
always_comb begin
 mem_d = mem_q;
 mem_d[addr[2]][addr[1:0]].data = data;
end
always_ff @(posedge clk) begin
 if(rst) begin
  for(int i=0;i<2;i++) for(int j=0;j<4;j++) mem_q[i][j] <= '0;
 end else mem_q <= mem_d;
end
assign q = mem_q[addr[2]][addr[1:0]].data;
endmodule
''', '''    dut.rst = u1(clock == 0)
    dut.addr = clock & 7
    dut.data = 200 + clock''', '''    assert(dut.q == if clock == 0 { 0 } else { 200 + clock })''', '''
  for (unsigned c = 0; c < 8; ++c) {
    dut.rst = c == 0; dut.addr = c & 7; dut.data = 200 + c;
    dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
    if (dut.q != (c == 0 ? 0 : 200 + c)) return 1;
  }''')
