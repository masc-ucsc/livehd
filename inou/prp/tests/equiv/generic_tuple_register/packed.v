module generic_tuple_register(input clock, reset, input [7:0] a, input v,
                              input [1:0] tag, input flush, enable,
                              output [7:0] q, output valid, output [1:0] t);
  reg [10:0] \first.reg_0 ;
  reg [8:0] \second.reg_0 ;
  always @(posedge clock) begin
    if (reset || flush) begin
      \first.reg_0  <= 0;
      \second.reg_0  <= 0;
    end else if (enable) begin
      \first.reg_0  <= {a,v,tag};
      \second.reg_0  <= \first.reg_0 [10:2];
    end
  end
  assign q = \second.reg_0 [8:1];
  assign valid = \second.reg_0 [0];
  assign t = \first.reg_0 [1:0];
endmodule
