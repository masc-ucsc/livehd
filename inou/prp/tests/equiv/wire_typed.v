module \wire_typed.wt (input [7:0] a, output [3:0] o1, output [3:0] o2);
  assign o1 = a[3:0];        // a wire:u4 truncates the u8 driver to 4 bits
  assign o2 = a[3:0] ^ 4'h0;
endmodule
