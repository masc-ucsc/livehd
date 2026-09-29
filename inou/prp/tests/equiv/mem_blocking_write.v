module mem_blocking_write(input logic clk, we, we2,
    input logic [1:0] wa, wd, wa2, wd2, ra,
    output logic [1:0] prior, forwarded, committed);
  logic [1:0] mem[4];
  assign committed = mem[ra];
  always_ff @(posedge clk) begin
    prior <= mem[ra];
    if (we) mem[wa] = wd;
    if (we2) mem[wa2] = wd2;
    forwarded <= mem[ra];
  end
endmodule
