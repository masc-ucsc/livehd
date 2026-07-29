// FIXME tracker -- a false "array index is negative".
// `(sel + 1) % 4` is always in [0,3], but the slang reader expands `a % b` into
// `a - (a/b)*b` (LNAST mod has no direct lowering) and upass.bitwidth's div() does
// not narrow by the constant divisor, so the chain's inferred range comes out as
// [-15, 4] and check_index_nonneg rejects it.
// The forward direction (.prp below) PROVES -- only the Verilog leg is broken,
// which is why just prp-v2prp-* / prp-v2v-* are fixme here.
module negative_array_index(
    input            clk,
    input      [1:0] sel,
    input      [3:0] din,
    output     [3:0] dout
);
    reg [3:0] mem [0:3];

    always @(posedge clk)
        mem[(sel + 1) % 4] <= din;

    assign dout = mem[sel];
endmodule
