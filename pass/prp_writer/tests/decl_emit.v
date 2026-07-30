// Fixture for prp_writer_decl_emit_test.sh — two writer emission contracts in
// one file, both of which used to lose information SILENTLY.
//
// 1. `\d\e` is a Verilog ESCAPED identifier used as a module name. Signal names
//    went through the writer's backtick escaper; module names did not, so this
//    was emitted verbatim into the Pyrope header and `\` is not a legal Pyrope
//    token — the generated file could not even be TOKENIZED.
//
// 2. `mem` is an unpacked array whose ONLY use is the runtime-indexed read
//    inside `if (en)`. The reader declares a symbol at its first use, so the
//    declare lands in the if-arm's scope; the writer's nested-mut hoist then
//    rebuilds it at the function top. That rebuild used to record the NAME
//    only, dropping both the `[2]u4` type and the `initial`-block contents the
//    reader had folded in as the declare's initializer. Those two must be
//    restored TOGETHER: the type alone turns the loud tolg abort
//    ("field/index read of '0' could not be resolved") into a silent wrong
//    answer, so the test asserts on the full `mut mem:[2]u4 = (1, 2)` line.
//
// Keep both in ONE module: the hoist runs per function, so a regression in
// either field shows up in the same emitted prologue.
//
// `o2` is deliberately a second, differently-shaped if-arm write, so the
// prologue is not a single-statement special case.
module \d\e (
  input        sel,
  input        en,
  input  [3:0] d,
  output reg [3:0] out,
  output reg [7:0] o2
);
  reg [3:0] mem [1:0];
  initial begin
    mem[0] <= 4'd1;
    mem[1] <= 4'd2;
  end
  always @(*) begin
    out = 4'd0;
    o2  = 8'd0;
    if (en) begin
      out = mem[sel];
      o2  = {4'd0, d} + 8'd7;
    end
  end
endmodule
