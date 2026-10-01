module rolled_inline_call_keys(input [7:0] a, input [7:0] c, output [7:0] o, output [7:0] p);
  assign o = c + {a[5:0], 2'b00};
  assign p = a + {c[5:0], 2'b00};
endmodule
