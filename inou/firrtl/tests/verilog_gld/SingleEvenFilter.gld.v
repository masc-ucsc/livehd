module PredicateFilter(
  input         io_in_valid,
  input  [15:0] io_in_bits,
  output        io_out_valid,
  output [15:0] io_out_bits
);
  wire  _T = io_in_bits <= 16'h9; // @[SingleEvenFilter.scala 30:51]
  assign io_out_valid = io_in_valid & _T; // @[SingleEvenFilter.scala 24:31]
  assign io_out_bits = io_in_bits; // @[SingleEvenFilter.scala 25:16]
endmodule
module PredicateFilter_1(
  input         io_in_valid,
  input  [15:0] io_in_bits,
  output        io_out_valid,
  output [15:0] io_out_bits
);
  assign io_out_valid = io_in_valid & io_in_bits[0]; // @[SingleEvenFilter.scala 24:31]
  assign io_out_bits = io_in_bits; // @[SingleEvenFilter.scala 25:16]
endmodule
module SingleEvenFilter(
  input         clock,
  input         reset,
  input         io_in_valid,
  input  [15:0] io_in_bits,
  output        io_out_valid,
  output [15:0] io_out_bits
);
  wire  single_io_in_valid; // @[SingleEvenFilter.scala 30:11]
  wire [15:0] single_io_in_bits; // @[SingleEvenFilter.scala 30:11]
  wire  single_io_out_valid; // @[SingleEvenFilter.scala 30:11]
  wire [15:0] single_io_out_bits; // @[SingleEvenFilter.scala 30:11]
  wire  even_io_in_valid; // @[SingleEvenFilter.scala 35:11]
  wire [15:0] even_io_in_bits; // @[SingleEvenFilter.scala 35:11]
  wire  even_io_out_valid; // @[SingleEvenFilter.scala 35:11]
  wire [15:0] even_io_out_bits; // @[SingleEvenFilter.scala 35:11]
  PredicateFilter single ( // @[SingleEvenFilter.scala 30:11]
    .io_in_valid(single_io_in_valid),
    .io_in_bits(single_io_in_bits),
    .io_out_valid(single_io_out_valid),
    .io_out_bits(single_io_out_bits)
  );
  PredicateFilter_1 even ( // @[SingleEvenFilter.scala 35:11]
    .io_in_valid(even_io_in_valid),
    .io_in_bits(even_io_in_bits),
    .io_out_valid(even_io_out_valid),
    .io_out_bits(even_io_out_bits)
  );
  assign io_out_valid = even_io_out_valid; // @[SingleEvenFilter.scala 43:17]
  assign io_out_bits = even_io_out_bits; // @[SingleEvenFilter.scala 43:17]
  assign single_io_in_valid = io_in_valid; // @[SingleEvenFilter.scala 41:17]
  assign single_io_in_bits = io_in_bits; // @[SingleEvenFilter.scala 41:17]
  assign even_io_in_valid = single_io_out_valid; // @[SingleEvenFilter.scala 42:17]
  assign even_io_in_bits = single_io_out_bits; // @[SingleEvenFilter.scala 42:17]
endmodule
