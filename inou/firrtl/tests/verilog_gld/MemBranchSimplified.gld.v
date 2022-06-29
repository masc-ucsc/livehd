module MemBranchSimplified(
  input        clock,
  input        reset,
  input        io_enq_bits_data,
  output [7:0] io_deq_bits_data
);
  assign io_deq_bits_data = {{7'd0}, io_enq_bits_data}; // @[]
endmodule
