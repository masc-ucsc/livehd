#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cat > "$W/source.v" <<'SV'
module \a.b (input x, output y); assign y = ~x; endmodule
module unrelated; missing_module bad(); endmodule
SV
cat > "$W/ref.v" <<'SV'
module b(input x, output y); assign y = ~x; endmodule
SV
for mode in default library raw; do
  flags=(--)
  if [ "$mode" = library ]; then flags=(-- --defaultLibName custom); fi
  if [ "$mode" = raw ]; then flags=(-- --top work.a.b); fi
  "$LHD" compile "$W/source.v" --reader slang --top a.b \
    --emit verilog:"$W/$mode.v" --workdir "$W/$mode" "${flags[@]}" > "$W/$mode.log" 2>&1 \
    || { cat "$W/$mode.log"; exit 1; }
  "$LHD" lec --impl "$W/$mode.v" --ref "$W/ref.v" --top b \
    --workdir "$W/$mode-lec" -q
done
echo 'PASS: literal escaped top selects only its hierarchy with default, custom and raw library options'
