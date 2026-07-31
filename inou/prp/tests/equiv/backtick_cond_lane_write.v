// Golden for backtick_cond_lane_write — a CONDITIONAL PARTIAL (bit-range) write
// to a struct FIELD.
//
// `v.addr` is a struct leaf, and the Pyrope writer emits a struct leaf as a
// BACKTICK-ESCAPED opaque identifier (`` `v.addr` ``) so the dot stays part of
// the name instead of re-lexing as field access. upass/tolg then canonicalizes
// that name (strips the backticks) in record()/resolve(), so `pin_map_` is
// keyed on the bare `v.addr`.
//
// set_mask_base() probed `pin_map_` with the RAW name. On a backtick-escaped
// leaf that probe always MISSED, which the fallback read as "declared but never
// driven" and answered with a 0sb? base -- DISCARDING the value the variable
// was already carrying. The write below is guarded, so the un-covered bits
// `addr[4:0]` must survive from `a_i`; before the fix the branch arm rebuilt
// the word from don't-care and those bits came back 0.
//
// It needs the CONDITIONAL: an unconditional partial write reads the same base,
// but by then the name is in pin_map_ under both spellings' shared canonical
// key via the ordinary store path, so the bad probe was never reached.
//
// Found from minion_dcache_replay_queue (`req_written_rearm.addr`, whose
// low 5 bits are the byte offset a misaligned second access must keep).
//
// The top is deliberately UNDOTTED for lgcheck's slang gold reader.
typedef struct packed {
  logic [9:0] addr;
  logic       flag;
} bclw_t;

module backtick_cond_lane_write_top (
  input  logic [9:0]  a_i,
  input  logic        c_i,
  output logic [10:0] o
);
  bclw_t v;

  always_comb begin
    v = '0;
    v.addr = a_i;
    if (c_i) v.addr[9:5] = a_i[9:5] + 1'b1;
  end

  assign o = v;
endmodule
