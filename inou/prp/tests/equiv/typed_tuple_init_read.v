// Golden for typed_tuple_init_read.prp.
module top(input [5:0] a, input w, input s,
           output py, output [5:0] pz,
           output ny, output [5:0] nz,
           output qy, output [5:0] qz,
           output cy, output [5:0] cz,
           output uy, output [5:0] uz);
  assign py = w;       assign pz = a;
  assign ny = ~w;      assign nz = a ^ 6'd1;
  assign qy = w;       assign qz = a ^ 6'd2;
  assign cy = (a == 6'd7); assign cz = a ^ 6'd5;
  assign uy = s ? ~w : w;  assign uz = s ? (a ^ 6'd1) : a;
endmodule
