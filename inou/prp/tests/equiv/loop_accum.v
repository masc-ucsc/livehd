// Golden for loop_accum.prp (task 2u): z = 3*a, w = a, with a an unsigned u8.
module \loop_accum.top (
   input signed [7:0] a
  ,output signed [9:0] z
  ,output signed [8:0] w
);
  wire [7:0] au = a;       // unsigned view of the u8 input
  assign z = au + au + au; // 3*a
  assign w = au;           // a
endmodule
