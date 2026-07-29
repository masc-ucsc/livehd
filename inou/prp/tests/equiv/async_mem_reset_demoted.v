// FIXME tracker -- an ASYNC reset OF a memory is demoted to a synchronous clear.
// The reset arm clears every element (`for (i=0;i<4;i=i+1) mem[i] <= 0`), which is
// a real asynchronous reset of the array. The Pyrope below keeps it as an ordinary
// `if rstn == 0 { mem[0] = 0 ... }` in the clocked body -- i.e. SYNCHRONOUS -- so
// the array only clears on the next clock edge instead of immediately. LEC-refuted.
// Distinct from mem_write_during_reset: there the memory has no reset at all
// and loses its write GATING; here the memory's own reset loses its ASYNC-ness.
module async_mem_reset_demoted (
    input  wire       clk,
    input  wire       rstn,
    input  wire       we,
    input  wire [1:0] a,
    input  wire [3:0] d,
    output wire [3:0] q
);

reg [3:0] mem [0:3];
integer i;

always @(posedge clk or negedge rstn)
    if (!rstn) begin
        for (i = 0; i < 4; i = i + 1)
            mem[i] <= 4'b0;
    end
    else if (we)
        mem[a] <= d;

assign q = mem[a];

endmodule
