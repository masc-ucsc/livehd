module counter_mix(
   input signed clock
  ,input signed [1:0] in
  ,output reg signed [5:0] out
);
  reg signed [4:0] \r.bar ;
  reg signed [4:0] \r.bar_next ;
  reg signed t_pin10;
  reg signed [1:0] ___j_0;
  reg signed [4:0] r_8;
  reg signed [4:0] t_pin75;
  reg signed [4:0] \r.foo ;
  reg signed [4:0] \r.foo_next ;
  always_comb begin
    t_pin10 = 1'sh0;
    ___j_0 = (2'sh1) & in;
    if (___j_0) begin
      r_8 = (3'sh3);
    end else begin
      r_8 = (5'shf);
    end
    if (___j_0) begin
      t_pin75 = (3'sh2);
    end else begin
      t_pin75 = (5'she);
    end
  end
  always_comb begin
    out = (\r.foo  + \r.bar );
    \r.bar_next  = r_8;
    \r.foo_next  = t_pin75;
  end
  always @(posedge clock ) begin
    \r.bar  <= \r.bar_next ;
  end
  always @(negedge clock ) begin
    \r.foo  <= \r.foo_next ;
  end
endmodule

