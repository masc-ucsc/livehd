// Golden for enum_const_capture.prp: St.Run = 2, St.Done = 3, Color.Green = 2,
// Color.Red = 1.
module enum_const_capture (
  input            clock,
  input            reset,
  input            go,
  input            sel,
  output     [1:0] s,
  output     [1:0] c
);
  reg [1:0] st;
  assign s = st;
  assign c = sel ? 2'd2 : 2'd1;
  always @(posedge clock) begin
    if (reset) st <= 2'd2;
    else if (go) st <= 2'd3;
  end
endmodule
