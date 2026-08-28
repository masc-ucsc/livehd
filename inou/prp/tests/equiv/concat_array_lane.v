// Golden for the runtime array splice. Written from the rule, not from the
// generated netlist: a packed tuple puts ENTRY 0 AT BIT 0, so entry 0 of
// `stages` is the LOWEST byte of its window and the scalar entry `inp` sits
// below all four.
//
// The Verilog spelling of that rule REVERSES the entry list, because Verilog
// concatenation is MSB-first: `{stages[3], stages[2], stages[1], stages[0],
// inp}`. That reversal is the whole reason `concat` was retired -- it read
// like `{a, b}` and was the only bit spelling in Pyrope running high-to-low.
//
// `stages` is a plain unpacked reg array with no reset. Its Pyrope twin is
// `reg stages:[4]u8:[ordering="old"]`: no initializer, so no reset hardware,
// and "old" ordering is exactly a nonblocking assignment -- `stages[k] <=
// stages[k-1]` reads the committed contents, never this cycle's write.
//
// `lanes` is combinational, the second array storage class. Pyrope lowers it
// to one packed bus rather than a memory, so the two outputs together prove
// the splice on BOTH paths.
//
// The module carries the DOTTED `<file>.<top>` name (same convention as
// concat_tuple_lanes.v) so that prp-p2p-* can compare against it: that target
// re-emits this fixture's Pyrope through pass.prp_writer and LECs the recompile
// against this file, and it derives the implementation's top by stripping the
// last dotted component off the golden's.
module \concat_array_lane.top (
   input               clock
  ,input        [7:0]  inp
  ,input        [7:0]  p
  ,input        [7:0]  q
  ,output       [39:0] regw
  ,output       [23:0] combw
);
  reg [7:0] stages [0:3];
  always @(posedge clock) begin
    stages[0] <= inp;
    stages[1] <= stages[0];
    stages[2] <= stages[1];
    stages[3] <= stages[2];
  end

  wire [7:0] lanes [0:2];
  assign lanes[0] = inp;
  assign lanes[1] = p;
  assign lanes[2] = q;

  assign regw  = {stages[3], stages[2], stages[1], stages[0], inp};
  assign combw = {lanes[2], lanes[1], lanes[0]};
endmodule
