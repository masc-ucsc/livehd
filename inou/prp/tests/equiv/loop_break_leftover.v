// GOLDEN for loop_break_leftover.prp — written by hand from the same
// specification, never generated.
//
// The SystemVerilog spelling of the same two comptime loops. Three things here
// are the point of the fixture and have no Pyrope counterpart:
//
//   * `r` is declared OUTSIDE its `for` header, so SystemVerilog leaves it at
//     the value the break froze (3) and `r_o` / `pick_o` read it after the loop;
//   * the inner `for (int c = ...)` header declares its own `c`, SHADOWING the
//     block-level `c` — the inner counter must not write through to it, so
//     `c_o` stays 9 and `sel_o` reads data[9];
//   * both breaks have statements after them inside the same iteration, which
//     SystemVerilog does not execute once the break is taken.
//
// `continue` is spelled as its guard (`if (parity == 0) ...`) because
// `--reader slang` does not lower `continue`; the Pyrope side uses the real
// `continue` statement.
module \loop_break_leftover.top (
  input  logic [15:0] data,
  output logic [7:0]  acc_o,
  output logic [3:0]  r_o,
  output logic [3:0]  rows_o,
  output logic [3:0]  c_seen_o,
  output logic [3:0]  c_o,
  output logic        pick_o,
  output logic        sel_o
);
  always_comb begin
    logic [3:0] r;       // outlives the loop
    logic [3:0] c;       // shadowed by the inner header `c`; must stay 9
    logic [3:0] rows;
    logic [3:0] c_seen;
    logic [7:0] acc;

    acc    = 8'd0;
    c      = 4'd9;
    rows   = 4'd0;
    c_seen = 4'd0;

    for (r = 0; r < 4; r = r + 1) begin
      if (r == 3) break;                        // `rows` below must not run for this row
      for (int c = 0; c < 4; c = c + 1) begin   // shadows the block-level `c`
        if (c == 3) break;                      // the accumulate below must not run for this cell
        if (((r + c) & 1) == 0) begin           // keep only even-parity cells
          acc    = acc + {7'd0, data[r*4 + c]};
          c_seen = c[3:0];
        end
      end
      rows = rows + 1;
    end

    acc_o    = acc;
    r_o      = r;       // 3
    rows_o   = rows;    // 3
    c_seen_o = c_seen;  // 2
    c_o      = c;       // 9 — untouched by the shadowing inner counter
    pick_o   = data[r*4];
    sel_o    = data[c];
  end
endmodule
