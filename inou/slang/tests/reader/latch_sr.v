// todo/livehd/2f-latch M7 — a SET/RESET latch through the yosys reader.
//
// The twin of latch_async.v, pinning the same negative claim for `$dlatchsr`.
// That cell had a nastier bug than $adlatch: `strncmp(type, "$dlatch", 7)` is a
// PREFIX match, so $dlatchsr fell into the plain-$dlatch branch, which wires
// only D and EN — silently DROPPING the SET and CLR ports and producing a latch
// that ignores its set/clear entirely. It now has its own branch and hard-errors.
//
// Like $adlatch, that guard is UNREACHABLE through this reader: `proc -ifx` plus
// the opt passes fold the set/reset priority into D and EN and emit a plain
// $dlatch. This test pins that the fold is LOSSLESS (the round-trip LEC-PROVES
// against this source) and that it stays that way — if a real $dlatchsr ever
// arrives, the guard fires and this goes red instead of a set/clear vanishing
// unnoticed.
module latch_sr(input c, input s, input r, input d, output logic q);

always_latch begin
  if (s)
    q <= 1'b1;
  else if (r)
    q <= 1'b0;
  else if (c)
    q <= d;
end
endmodule
