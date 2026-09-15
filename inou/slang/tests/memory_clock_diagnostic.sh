#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
for kind in same different opposite negative; do
  first='posedge clk'
  second='posedge clk'
  [ "$kind" != different ] || second='posedge clk_b'
  [ "$kind" != opposite ] || second='negedge clk'
  if [ "$kind" = negative ]; then first='negedge clk'; second='negedge clk'; fi
  cat > "$W/$kind.v" <<EOF
module memory_clock(input clk, clk_b, we_a, we_b, input [1:0] a, b,
                    input [7:0] d_a, d_b, output [7:0] q);
  reg [7:0] mem[0:3];
  always @($first) if (we_a) mem[a] <= d_a;
  always @($second) if (we_b) mem[b] <= d_b;
  assign q = mem[a];
endmodule
EOF
  emits=(--emit-dir "lg:$W/$kind-lg")
  [ "$kind" = opposite ] || emits+=(--emit "verilog:$W/$kind-out.v")
  "$LHD" compile "$W/$kind.v" --top memory_clock \
    "${emits[@]}" \
    --result-json "$W/$kind.json" --workdir "$W/$kind" > "$W/$kind.log" 2>&1 \
    || { cat "$W/$kind.log"; exit 1; }
  if [ "$kind" = opposite ]; then
    grep -q 'mixes clock edges' "$W/$kind.log"
    set +e
    "$LHD" lec --impl "lg:$W/$kind-lg" --ref "$W/$kind.v" --top memory_clock \
      --workdir "$W/$kind-lec" -q > "$W/$kind-lec.log" 2>&1
    rc=$?
    set -e
    [ "$rc" -eq 7 ] || { cat "$W/$kind-lec.log"; exit 1; }
    grep -q 'PER-PORT clock edge polarity' "$W/$kind-lec.log"
    if "$LHD" compile "lg:$W/$kind-lg" --top memory_clock --emit verilog:"$W/$kind-out.v" \
        --workdir "$W/$kind-emit" > "$W/$kind-emit.log" 2>&1; then
      echo 'FAIL: emitted mixed-edge memory as a uniform-edge memory'; exit 1
    fi
    grep -q 'requires per-port clock edge polarity' "$W/$kind-emit.log"
  elif [ "$kind" = different ]; then
    grep -q '\.wr_clock_0(clk)' "$W/$kind-out.v"
    grep -q '\.wr_clock_1(clk_b)' "$W/$kind-out.v"
  elif [ "$kind" = negative ]; then
    grep -Fq '.clk(~(clk))' "$W/$kind-out.v"
    if "$LHD" lec --impl "$W/$kind-out.v" --ref "$W/$kind.v" --top memory_clock \
        --workdir "$W/$kind-lec" -q > "$W/$kind-lec.log" 2>&1; then
      echo 'FAIL: formal accepted an unsupported falling-edge memory'; exit 1
    else
      rc=$?
      [ "$rc" -eq 7 ] || { cat "$W/$kind-lec.log"; exit 1; }
    fi
    grep -q 'FALLING clock edge' "$W/$kind-lec.log"
    # Emission is supported even though native formal does not model this
    # schedule. Compare both edges against the original RTL in simulation.
    sed 's/module memory_clock/module reference_clock/' "$W/$kind.v" > "$W/negative-ref.v"
    cat > "$W/negative-tb.v" <<'SV'
module tb;
  reg clk=1, clk_b=0, we_a=1, we_b=0;
  reg [1:0] a=0, b=0;
  reg [7:0] d_a=0, d_b=0;
  wire [7:0] q, ref_q;
  memory_clock dut(.*);
  reference_clock gold(.clk(clk),.clk_b(clk_b),.we_a(we_a),.we_b(we_b),.a(a),.b(b),.d_a(d_a),.d_b(d_b),.q(ref_q));
  initial begin
    for (integer k=0; k<4; k=k+1) begin
      a=k; d_a=k; #1; clk=0; #1; clk=1;
    end
    for (integer k=0; k<64; k=k+1) begin
      a=k%4; b=(k+1)%4; d_a=k*3; d_b=k*5; we_a=k&1; we_b=(k>>1)&1; #1;
      if (q !== ref_q) $fatal(1,"negedge memory before falling edge %d",k);
      clk=0; #1;
      if (q !== ref_q) $fatal(1,"negedge memory after falling edge %d",k);
      d_a=k*7; clk=1; #1;
      if (q !== ref_q) $fatal(1,"negedge memory changed on rising edge %d",k);
    end
    $finish;
  end
endmodule
SV
    iverilog -g2012 -I ware/rtl -s tb -o "$W/negative-sim" "$W/negative-out.v" "$W/negative-ref.v" "$W/negative-tb.v"
    vvp "$W/negative-sim"
  else
    "$LHD" lec --impl "$W/$kind-out.v" --ref "$W/$kind.v" --top memory_clock \
      --workdir "$W/$kind-lec" -q > "$W/$kind-lec.log" 2>&1 \
      || { cat "$W/$kind-lec.log"; exit 1; }
  fi
done
# A bulk write has no per-entry store site, but must still carry its process
# clock. Its one update bus cannot combine different writer clocks/edges.
for kind in same different opposite; do
  second='negedge clk_a'
  [ "$kind" != different ] || second='negedge clk_b'
  [ "$kind" != opposite ] || second='posedge clk_a'
  cat > "$W/bulk_$kind.v" <<EOF
module bulk_clock(input clk_a, clk_b, bulk_we, entry_we, input [1:0] a,
                  input [7:0] d, input [31:0] bulk, output [7:0] q);
  reg [7:0] mem[0:3];
  wire [7:0] next_mem[0:3];
  for (genvar k=0; k<4; k=k+1) assign next_mem[k] = bulk[k*8 +: 8];
  always @(negedge clk_a) if (bulk_we) mem <= next_mem;
  always @($second) if (entry_we) mem[a] <= d;
  assign q = mem[a];
endmodule
EOF
  if "$LHD" compile "$W/bulk_$kind.v" --top bulk_clock --emit verilog:"$W/bulk_$kind-out.v" \
      --workdir "$W/bulk_$kind" > "$W/bulk_$kind.log" 2>&1; then
    [ "$kind" = same ] || { echo "FAIL: accepted incompatible bulk writer clock: $kind"; exit 1; }
    grep -q 'negedge clk_a' "$W/bulk_$kind-out.v"
  else
    [ "$kind" != same ] || { cat "$W/bulk_$kind.log"; exit 1; }
    grep -q 'bulk and entry writes require one shared clock' "$W/bulk_$kind.log"
  fi
done
echo 'PASS: native memory clocks survive; mixed edges are diagnosed and refused by formal'
