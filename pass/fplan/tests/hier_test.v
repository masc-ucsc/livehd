module leaf1(inout [14:0] a)
    
endmodule

module leaf2(inout [14:0] a, inout [24:0] b)

endmodule

module leaf3(inout [29:0] c)

endmodule

module leaf4()

endmodule

module leaf5(inout [29:0] c)

endmodule

module leaf6()

endmodule

module leaf7(inout [24:0] b)

endmodule

module mid1(inout [899:0] d)
  wire [14:0] wa;
  wire [24:0] wb;

  leaf1 l1(.a(wa));
  leaf2 l2(.a(wa), .b(wb));
  leaf7 l7(.b(wb));

endmodule

module mid2(inout [899:0] d, inout [9:0] e)
  wire [29:0] wc;
  
  leaf3 l3(.c(wc));
  leaf4 l4();
  leaf5 l5(.c(wc));
endmodule

module mid4(inout [9:0] e, inout [4:0] f)

endmodule

module mid3(inout [4:0] f)
  leaf6 l6();
endmodule

module root()
  wire [899:0] wd;
  wire [9:0] we;
  wire [4:0] wf;

  mid1 m1(.d(wd));
  mid2 m2(.d(wd), .e(we));
  mid3 m3(.f(wf));
  mid4 m4(.e(we), .f(wf));
  
endmodule