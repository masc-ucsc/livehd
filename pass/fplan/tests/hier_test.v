module leaf1(output [14:0] a);
  assign a[1] = a[0];
endmodule

module leaf2(output [14:0] a, output [24:0] b);
  assign a[4:3] = b[1:0] ^ ~b[7:6] ^ a[12:11];
endmodule

module leaf3(output [29:0] c);
  assign c[29:28] = c[28:29];
endmodule

module leaf4();

endmodule

module leaf5(output [29:0] c);
  assign c[27:26] = c[26:27];
endmodule

module leaf6();

endmodule

module leaf7(output [24:0] b);
  assign b[24:23] = b[23:24];
endmodule

module mid1(output [899:0] d);
  wire [14:0] wa;
  wire [24:0] wb;

  leaf1 l1(.a(wa));
  leaf2 l2(.a(wa), .b(wb));
  leaf7 l7(.b(wb));

  assign d[899:898] = d[898:899];
endmodule

module mid2(output [899:0] d, output [9:0] e);
  wire [29:0] wc;
  
  leaf3 l3(.c(wc));
  leaf4 l4();
  leaf5 l5(.c(wc));

  assign d[897:896] = d[896:897];
  assign e[9:8] = e[8:9];
endmodule

module mid4(output [9:0] e, output [4:0] f);
  assign e[7:6] = e[6:7];
endmodule

module mid3(output [4:0] f);
  leaf6 l6();
  assign f[4:3] = f[3:4];
endmodule

module root();
  wire [899:0] wd;
  wire [9:0] we;
  wire [4:0] wf;

  mid1 m1(.d(wd));
  mid2 m2(.d(wd), .e(we));
  mid3 m3(.f(wf));
  mid4 m4(.e(we), .f(wf));
  
endmodule