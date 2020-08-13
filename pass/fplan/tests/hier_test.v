module leaf1(input [14:0] ai, output [14:0] ao);
  assign ao = ~ai;
endmodule

module leaf2(input [14:0] ai, output [14:0] ao, input [24:0] bi, output [24:0] bo);
  // longer temp expression to get some variability in the area of the leaves
  assign ao = bi ^ ~{bi[15:0], bi[24:16]} / ai;
  assign bo = bi * 7;
endmodule

module leaf3(input [29:0] ci, output [29:0] co);
  assign co = ~ci;
endmodule

module leaf4(input tempi, output tempo);
  assign tempo = ~tempi;
endmodule

module leaf5(input [29:0] ci, output [29:0] co);
  assign co = ~ci;
endmodule

module leaf6(input tempi, output tempo);
  assign tempo = ~tempi;
endmodule

module leaf7(input [24:0] bi, output [24:0] bo);
  assign bo = ~bi;
endmodule

module mid1(input [899:0] di, output [899:0] dout);
  wire [14:0] w_1_to_2;
  wire [14:0] w_2_to_1;

  wire [24:0] w_2_to_7;
  wire [24:0] w_7_to_2;

  leaf1 l1(.ai(w_2_to_1), .ao(w_1_to_2));
  leaf2 l2(.ai(w_1_to_2), .ao(w_2_to_1), .bi(w_7_to_2), .bo(w_2_to_7));
  leaf7 l7(.bi(w_2_to_7), .bo(w_7_to_2));

  assign dout = ~{di[899:38], w_1_to_2, w_2_to_7};
endmodule

module mid2(input [899:0] di, output [899:0] dout, input [9:0] ei, output [9:0] eo);
  wire [29:0] w_3_to_5;
  wire [29:0] w_5_to_3;
  
  leaf3 l3(.ci(w_5_to_3), .co(w_3_to_5));
  leaf4 l4(.tempi(w_3_to_5[12]), .tempo(w_3_to_5[13])); // might get 'X' on bits 12/13 but whatever
  leaf5 l5(.ci(w_3_to_5), .co(w_5_to_3));

  assign dout = ~{di[899:30], w_3_to_5};
  assign eo = ~{ei[9:1], w_5_to_3[0]};
endmodule

module mid4(input [9:0] ei, output [9:0] eo, input [4:0] fi, output [4:0] fo);
  assign eo = ~ei;
  assign fo = ~fi;
endmodule

module mid3(input [4:0] fi, output [4:0] fo);
  wire w;
  leaf6 l6(.tempi(fi[3]), .tempo(w));
  assign fo = ~{fi[4:1], w};
endmodule

module hier_test(input [913:0] testi, output [913:0] testo);
  wire [899:0] w_1_to_2;
  wire [899:0] w_2_to_1;

  wire [9:0] w_2_to_4;
  wire [9:0] w_4_to_2;

  wire [4:0] w_4_to_3;
  wire [4:0] w_3_to_4;

  mid1 m1(.di(w_2_to_1), .dout(w_1_to_2));
  mid2 m2(.di(w_1_to_2), .dout(w_2_to_1), .ei(w_2_to_4), .eo(w_4_to_2));
  mid3 m3(.fi(w_4_to_3), .fo(w_3_to_4));
  mid4 m4(.ei(w_4_to_2), .eo(w_2_to_4), .fi(w_3_to_4), .fo(w_4_to_3));

  assign testo = ~{testi[0], w_2_to_1, w_2_to_4, w_3_to_4};
  
endmodule