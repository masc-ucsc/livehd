// An instance output read ONLY as the index of an array element whose FIELD is
// accessed (`data_i[bin].tag`) must still register as a READ of that net.
//
// Dep_collector's MemberAccess handler canonicalizes a member read to its root
// symbol, but defining handle() REPLACES slang's default traversal — so
// returning without descending discarded the whole sub-expression, and with it
// the read nested in the element SELECTOR. `bin` then had no reader at all: the
// back-edge wire classification never saw the early read, `bin` stayed a
// `0sb?`-poisoned `mut`, and its instance-output binding — emitted after every
// reader — was dropped as dead. The consumer read X forever.
//
// This is CVA6 miss_handler's `data_i[lfsr_bin].tag/.data/.dirty` against
// `lfsr_8bit`'s `refill_way_bin` output (3 sites); `refill_way_oh`, read as a
// plain value, was unaffected, which is what made the asymmetry visible.
//
// `oh` is here as the control: whatever the fix does to `bin`, `oh` must keep
// working. Both outputs feed the result, so losing either refutes.
typedef struct packed {
  logic       dirty;
  logic [7:0] tag;
} line_t;

module instance_output_as_member_index_lfsr (
    input  logic       clk_i,
    input  logic       en_i,
    output logic [7:0] way_oh_o,
    output logic [1:0] way_bin_o
);
  logic [7:0] shift_q;
  always_ff @(posedge clk_i) if (en_i) shift_q <= {shift_q[6:0], ~shift_q[7]};
  assign way_oh_o  = shift_q;
  assign way_bin_o = shift_q[1:0];
endmodule

module instance_output_as_member_index (
    input  logic          clk_i,
    input  line_t [3:0]   data_i,
    output logic [7:0]    oh_o,
    output logic [7:0]    tag_o,
    output logic          dirty_o
);
  // Declared BEFORE the instance and read below it: the reads must still bind
  // to the instance driver, not to a poison initializer.
  logic [7:0] way_oh;
  logic [1:0] way_bin;

  always_comb begin
    oh_o    = way_oh;              // plain value read  (the control)
    tag_o   = data_i[way_bin].tag;    // read as a MEMBER-ACCESS index (the bug)
    dirty_o = data_i[way_bin].dirty;
  end

  instance_output_as_member_index_lfsr i_lfsr (
      .clk_i    (clk_i),
      .en_i     (1'b1),
      .way_oh_o (way_oh),
      .way_bin_o(way_bin)
  );
endmodule
