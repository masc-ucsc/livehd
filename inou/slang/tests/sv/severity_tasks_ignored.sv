// :test: lec
// Run-time severity tasks ($info/$warning/$error/$fatal in a process) report to
// the simulator only: they drive no hardware, so synthesis must not abort on
// them (FlooNoC's route selection carries a `$warning` in an `always`). The
// elaboration forms in a generate scope belong to slang itself.
module severity_tasks_ignored (
    input  logic       clk,
    input  logic       a,
    input  logic       b,
    output logic [3:0] q
);
  localparam int N = 4;
  if (N != 4) begin : g_bad
    $error("N must be 4");
  end else begin : g_ok
    $info("elaborating with N=%0d", N);
  end

  always_ff @(posedge clk) q <= {q[2:0], a ^ b};

  always @(posedge clk) begin
    if (a && b && q[0]) $warning("mismatch");
    if (a && !b) $error("bad %0d", q);
    if (!a && b) $info("hi");
    if (q == 4'hf && a) $fatal(1, "full");
  end
endmodule
