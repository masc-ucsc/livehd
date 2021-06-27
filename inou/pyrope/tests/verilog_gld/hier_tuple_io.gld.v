
module hier_tuple_io(\out.tup1.a , \out.tup1.b , \out.tup3 , \out.tup2.c , \out2.sub );
  wire lg_0;
  wire [1:0] lg_1;
  wire [2:0] lg_2;
  wire [2:0] lg_3;
  output \out.tup1.a ;
  output [1:0] \out.tup1.b ;
  output [2:0] \out.tup2.c ;
  output [2:0] \out.tup3 ;
  output [3:0] \out2.sub ;
  assign lg_3 = 3'h7;
  assign lg_2 = 3'h5;
  assign lg_1 = 2'h3;
  assign lg_0 = 1'h1;
  assign \out.tup3  = 3'h7;
  assign \out.tup2.c  = 3'h5;
  assign \out.tup1.b  = 2'h3;
  assign \out.tup1.a  = 1'h1;
  assign \out2.sub = 4'h9;
endmodule
