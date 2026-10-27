// :test: roundtrip
// A constant element past an unpacked array inside a struct never lands (write)
// and reads the default (read), also under a further dynamic select.
module unpacked_struct_oob(input [7:0] d, input [1:0] k, output [7:0] e, output [7:0] f, output [7:0] g);
  typedef struct { logic [7:0] arr[2]; logic [7:0] sub[2][2]; } st_t;
  st_t s;
  localparam int P = 2;
  always_comb begin
    s.arr[0] = d;
    s.arr[1] = ~d;
    s.sub[0][0] = 1; s.sub[0][1] = 2; s.sub[1][0] = 3; s.sub[1][1] = 4;
    s.sub[P][k] = 8'h99;
    if (k == 3) s.arr[P-1] = 8'h55;
  end
  assign e = s.arr[1];
  assign f = s.sub[k[0]][k[1]];
  assign g = s.sub[P][k[0]];
endmodule
