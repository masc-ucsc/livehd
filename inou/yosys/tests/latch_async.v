// todo/livehd/2f-latch M7 — an ASYNC-RESET latch through the yosys reader.
//
// This fixture exists to pin a NEGATIVE claim, which is why it is worth having
// even though it looks like an ordinary round-trip: the importer carries a
// hard-error guard for the `$adlatch` cell (it used to hit a catch-all `log()`
// and leave an untyped Ntype_op::Invalid node at exit 0 — a silent drop), and
// that guard is UNREACHABLE today. The reader's yosys script runs `proc -ifx`
// plus the opt passes, which lower this shape into a plain `$dlatch` with the
// reset folded into D and EN before the importer ever sees a cell.
//
// So this test asserts two things at once:
//   * that lowering is LOSSLESS — the round-trip LEC-PROVES against this
//     source, so nothing is lost by never seeing an $adlatch;
//   * and that the situation has not CHANGED. If a future yosys script or
//     version starts delivering a real $adlatch here, the importer's guard
//     fires, this test goes red, and whoever changed it learns that latch
//     set/reset support now needs writing rather than discovering a silently
//     dropped register later.
//
// A latch's reset is inherently asynchronous (there is no clock edge to
// synchronize to), which is why the Pyrope side spells the same thing as
// `reg l:u8:[latch=true] = 3` — see inou/prp/tests/equiv/latch_reset.prp.
module latch_async(input c, input rst, input d, output logic q);

always_latch begin
  if (rst)
    q <= 1'b0;
  else if (c)
    q <= d;
end
endmodule
