module direct_io_assign(
   input signed [2:0] foo
  ,output reg signed [2:0] bar
);
always_comb begin
end
always_comb begin
  bar = foo;
end
endmodule
