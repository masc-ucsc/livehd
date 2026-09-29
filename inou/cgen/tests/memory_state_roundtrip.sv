module memory_state_roundtrip
    (input logic          clk,
     input logic          we,
     input logic          sel,
     input logic [1 : 0]  addr,
     input logic [3 : 0]  din,
     output logic [3 : 0] dout);
  logic [3 : 0] bank_a[4];
  logic [3 : 0] bank_b[4];

  always_ff @(posedge clk) begin
    if (we) begin
      bank_a[addr] <= din;
      bank_b[addr] <= ~din;
    end
  end

  always_comb
    dout = sel ? bank_a[addr] : bank_b[addr];
endmodule
