// FIXME tracker -- prp_writer does not ESCAPE module names.
// Signal names go through the writer's backtick escaper; module names do not. A
// Verilog escaped identifier (`\s\m`) is therefore emitted verbatim into the
// Pyrope module header and file name, and `\` is not a legal Pyrope token, so the
// generated file cannot even be TOKENIZED ("unexpected character in input").
// Same family as the `mod` keyword collision seen elsewhere in the corpus.
module \s\m (input [1:0] a, output [1:0] o);
  assign o = ~a;
endmodule
