// Asynchronous active-low reset clears every memory entry immediately.
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
