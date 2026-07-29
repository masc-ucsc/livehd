// FIXME tracker -- a declaration-time register initializer is DROPPED.
// `reg internal_full_n = 1'b1;` gives the flop a power-on value of 1; the Pyrope
// below declares a bare `reg internal_full_n:u1` with no `init=`, so the emitted
// netlist powers up at 0. yosys' miter starts both sides from their declared
// initial values, so this refutes. 3 corpus files (HLS-generated FIFOs, where the
// `full_n` handshake bit is exactly this idiom).
module reg_decl_init_dropped (
    input  wire       clk,
    input  wire       we,
    input  wire [1:0] a,
    input  wire [3:0] d,
    output wire [3:0] q,
    output wire       full_n
);

reg [3:0] mem [0:3];
reg       internal_full_n = 1'b1;

always @(posedge clk) begin
    if (we) mem[a] <= d;
    internal_full_n <= we;
end

assign q      = mem[a];
assign full_n = internal_full_n;

endmodule
