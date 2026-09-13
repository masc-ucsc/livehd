module generic_tuple_register(input clock, reset, input [7:0] a, input v,
                              input [1:0] tag, input flush, enable,
                              output [7:0] q, output valid, output [1:0] t);
  // Keep the tuple field names for the state-correspondence check. The packed
  // representation remains in generic_tuple_register/packed.v for LEC coverage.
  reg [7:0] \first.reg_0.payload.data ;
  reg \first.reg_0.payload.valid ;
  reg [1:0] \first.reg_0.tag ;
  reg [7:0] \second.reg_0.data ;
  reg \second.reg_0.valid ;
  always @(posedge clock) begin
    if (reset || flush) begin
      \first.reg_0.payload.data  <= 0;
      \first.reg_0.payload.valid  <= 0;
      \first.reg_0.tag  <= 0;
      \second.reg_0.data  <= 0;
      \second.reg_0.valid  <= 0;
    end else if (enable) begin
      \first.reg_0.payload.data  <= a;
      \first.reg_0.payload.valid  <= v;
      \first.reg_0.tag  <= tag;
      \second.reg_0.data  <= \first.reg_0.payload.data ;
      \second.reg_0.valid  <= \first.reg_0.payload.valid ;
    end
  end
  assign q = \second.reg_0.data ;
  assign valid = \second.reg_0.valid ;
  assign t = \first.reg_0.tag ;
endmodule
