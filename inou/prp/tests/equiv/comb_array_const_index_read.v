// Golden for comb_array_const_index_read — a CONST-indexed read of a comb
// (`mut`) array after a write to the same entry.
//
// `upass/constprop` tracks a comptime value per element key. An element key is
// never SSA-versioned (`d[0]` writes and reads the one key `d.0`), so a
// RUNTIME write has to invalidate the slot. It did not: neither store branch
// fired when the value was a runtime ref, the declaration's init stayed in the
// table, and a later `d[0]` folded to it — DELETING the write. The emitted
// netlist then has no read port on the array at all:
//
//     mut d:[2]u3 = 5
//     d[0] = a          // runtime
//     o   = d[0]        // -> 5
//
// It reaches minion through the Pyrope WRITER, which emits a whole-array copy
// as N CONST-INDEXED per-entry stores (Pyrope has no runtime whole-array
// assignment — a 2-child store to an array name is its initializer). So the
// `q <= d` below round-trips into `q[0] = d[0] … q[7] = d[7]`, and every write
// port's din came back a constant: `minion_dcache_replay_queue` lost its whole
// `uc_load_id_q` update, which is what //bench:minion_lec saw as an
// undecidable `mem:8x3#0` cut.
//
// The interesting direction is the ROUND TRIP (prp-v2prp2v-*): it is
// the writer's per-entry expansion that creates the shape.
module comb_array_const_index_read (
  input  logic       clk_i,
  input  logic       en_i,
  input  logic [2:0] wa_i,
  input  logic [2:0] wd_i,
  input  logic [2:0] ra_i,
  output logic [2:0] rd_o
);
  logic [2:0] q [8];
  logic [2:0] d [8];

  always_comb begin
    d       = q;
    d[wa_i] = wd_i;
  end

  always_ff @(posedge clk_i) begin
    if (en_i) q <= d;
  end

  assign rd_o = q[ra_i];
endmodule
