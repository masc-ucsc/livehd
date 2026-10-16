#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cat > "$W/source.v" <<'EOF'
module selected(input a, output y);
  wire before_declaration = later;
  wire later = a;
  assign y = before_declaration;
endmodule
module unfinished(output y);
  assign y = missing_signal + 1;
endmodule
EOF
# Both compatibility policies are explicit; strict source diagnostics stay intact.
if "$LHD" compile "$W/source.v" --top selected --workdir "$W/strict" \
  --emit diagnostics:"$W/strict.jsonl" -q; then
  echo 'FAIL: strict diagnostics unexpectedly changed'; exit 1
fi
grep -q 'UsedBeforeDeclared' "$W/strict.jsonl"
"$LHD" compile "$W/source.v" --top selected --workdir "$W/selected" \
  --emit-dir verilog:"$W/net" -q -- --allow-use-before-declare --ignore-uninstantiated-modules
[ -f "$W/net/selected.v" ]
[ ! -f "$W/net/unfinished.v" ]
# An error in the selected hierarchy is still an error.
if "$LHD" compile "$W/source.v" --top unfinished --workdir "$W/unfinished" \
  --emit diagnostics:"$W/unfinished.jsonl" -q -- --allow-use-before-declare --ignore-uninstantiated-modules; then
  echo 'FAIL: reachable undeclared signal was accepted'; exit 1
fi
grep -q 'UndeclaredIdentifier' "$W/unfinished.jsonl"
# Parsing remains global, including files containing unused modules.
cat > "$W/syntax.v" <<'EOF'
module selected(input a, output y); assign y=a; endmodule
module unfinished; assign = ; endmodule
EOF
if "$LHD" compile "$W/syntax.v" --top selected --workdir "$W/syntax" -q -- --ignore-uninstantiated-modules; then
  echo 'FAIL: unused-module syntax error was accepted'; exit 1
fi
# Preprocessor-excluded code must never reach the parser, even when malformed.
cat > "$W/guarded.v" <<'EOF'
module selected(input a, output y); assign y=a; endmodule
`ifndef SYNTHESIS
module unfinished(output y,); assign y = missing_signal; else if() endmodule
`endif
EOF
"$LHD" compile "$W/guarded.v" --top selected --workdir "$W/guarded" \
  --emit-dir verilog:"$W/guarded-net" -q -- -DSYNTHESIS=1 --ignore-uninstantiated-modules
[ -f "$W/guarded-net/selected.v" ]
[ ! -f "$W/guarded-net/unfinished.v" ]
echo 'PASS: explicit unused-module policy and SYNTHESIS preprocessing preserve source diagnostics' 

# Generic router feedback can be broken by constant top-level configuration.
cat > "$W/configured.v" <<'EOF'
module configurable(input en, d, output y);
 wire x;
 assign x = en ? x : d;
 assign y = x;
endmodule
module configured(input d, output y);
 configurable u(.en(1'b0), .d(d), .y(y));
endmodule
EOF
# Legality uses the instance binding even when optimization specialization is off.
"$LHD" compile "$W/configured.v" --top configured --workdir "$W/config-strict" \
  --set compile.specialize_modules=false --emit-dir verilog:"$W/config-strict-net" -q
for n in 1 2; do
 "$LHD" compile "$W/configured.v" --top configured --workdir "$W/configured" \
   --set compile.specialize_modules=true --emit-dir verilog:"$W/config-net$n" -q
 done
cmp "$W/config-net1/configured.v" "$W/config-net2/configured.v"
for specialize in false true; do
 if "$LHD" compile "$W/configured.v" --top configurable --workdir "$W/config-dynamic-$specialize" \
   --set compile.specialize_modules="$specialize" > "$W/config-dynamic-$specialize.log" 2>&1; then
   echo 'FAIL: unconstrained router feedback was accepted'; exit 1
 fi
 grep -q '"code":"comb-loop"' "$W/config-dynamic-$specialize.log"
 if grep -q 'pass.legalize.check' "$W/config-dynamic-$specialize.log"; then
   echo 'FAIL: unchanged loop triggered a second loop check'; exit 1
 fi
done

# Inlining exposes constant mux selectors; the existing cprop/bitwidth pair
# must run before deciding whether the resulting mux-to-mux ring is real.
cat > "$W/ring.v" <<'EOF'
module router(input sel, local_data, peer_data, output y);
 assign y = sel ? peer_data : local_data;
endmodule
module ring(input a,b, output x,y);
 router first(.sel(1'b0),.local_data(a),.peer_data(y),.y(x));
 router second(.sel(1'b1),.local_data(b),.peer_data(x),.y(y));
endmodule
module real_ring(input a,b, output x,y);
 router first(.sel(1'b1),.local_data(a),.peer_data(y),.y(x));
 router second(.sel(1'b1),.local_data(b),.peer_data(x),.y(y));
endmodule
EOF
"$LHD" compile "$W/ring.v" --top ring --workdir "$W/ring" \
  --set compile.specialize_modules=false > "$W/ring.log" 2>&1
grep -q 'pass.legalize.check' "$W/ring.log"
if "$LHD" compile "$W/ring.v" --top real_ring --workdir "$W/real-ring" \
  --set compile.specialize_modules=false > "$W/real-ring.log" 2>&1; then
  echo 'FAIL: real mux ring survived the deferred loop check'; exit 1
fi
grep -q 'pass.legalize.check' "$W/real-ring.log"
grep -q '"code":"comb-loop"' "$W/real-ring.log"
if grep -q '"pass.enableopt"' "$W/real-ring.log"; then
  echo 'FAIL: enableopt ran before the pending loop verdict'; exit 1
fi

# Distinct safe configurations cannot be joined into one unconstrained callee.
cat > "$W/multi.v" <<'EOF'
module router(input [1:0] mode, input d, output y);
 wire x;
 assign x = mode[0] ? x : d;
 assign y = x;
endmodule
module multi(input a,b, output x,y);
 router first(.mode(2'd0),.d(a),.y(x));
 router second(.mode(2'd2),.d(b),.y(y));
endmodule
EOF
"$LHD" compile "$W/multi.v" --top multi --workdir "$W/multi" \
  --set compile.specialize_modules=true --emit-dir verilog:"$W/multi-net" -q

# A fresh unsigned one-bit proof permits ready | 1 to break feedback. A wider
# integer must retain its other bits and must still report an actual cycle.
cat > "$W/ready.v" <<'EOF'
module ready(output y);
 wire r;
 assign r = r | 1'b1;
 assign y = r;
endmodule
module wide(output [1:0] y);
 wire [1:0] r;
 assign r = r | 2'd1;
 assign y = r;
endmodule
EOF
"$LHD" compile "$W/ready.v" --top ready --workdir "$W/ready" \
  --set compile.specialize_modules=true --emit-dir verilog:"$W/ready-net" -q -- --ignore-uninstantiated-modules
if "$LHD" compile "$W/ready.v" --top wide --workdir "$W/wide" \
  --set compile.specialize_modules=true -q -- --ignore-uninstantiated-modules; then
  echo 'FAIL: a wide integer OR was treated as Boolean'; exit 1
fi
