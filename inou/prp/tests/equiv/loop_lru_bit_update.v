// Hand-written golden for loop_lru_bit_update.prp: a 4-requester LRU arbiter
// whose pairwise matrix is spelled bit by bit. Bit i*4 + j (i < j) is set when
// requester i outranks requester j; the rest of the word is never written and
// keeps its reset value (0).
module \loop_lru_bit_update.top (
  input         clock,
  input         reset,
  input         en,
  input  [3:0]  request,
  output [3:0]  grant,
  output [15:0] state_q
);
  reg [15:0] state;

  // o_ij: requester i outranks requester j (i < j); j outranks i is its complement.
  wire o01 = state[1];
  wire o02 = state[2];
  wire o03 = state[3];
  wire o12 = state[6];
  wire o13 = state[7];
  wire o23 = state[11];

  // A requester is granted when every other active requester loses to it.
  wire [3:0] can;
  assign can[0] = (!request[1] | o01) & (!request[2] | o02) & (!request[3] | o03);
  assign can[1] = (!request[0] | !o01) & (!request[2] | o12) & (!request[3] | o13);
  assign can[2] = (!request[0] | !o02) & (!request[1] | !o12) & (!request[3] | o23);
  assign can[3] = (!request[0] | !o03) & (!request[1] | !o13) & (!request[2] | !o23);
  assign grant  = request & can;

  // Pair (i, j): cleared when i is granted (i becomes the lowest priority),
  // else set when j is granted.
  wire upd = en & (request != 4'b0);
  always @(posedge clock) begin
    if (reset) begin
      state <= 16'h08CE;
    end else if (upd) begin
      state[1]  <= grant[0] ? 1'b0 : (grant[1] ? 1'b1 : state[1]);
      state[2]  <= grant[0] ? 1'b0 : (grant[2] ? 1'b1 : state[2]);
      state[3]  <= grant[0] ? 1'b0 : (grant[3] ? 1'b1 : state[3]);
      state[6]  <= grant[1] ? 1'b0 : (grant[2] ? 1'b1 : state[6]);
      state[7]  <= grant[1] ? 1'b0 : (grant[3] ? 1'b1 : state[7]);
      state[11] <= grant[2] ? 1'b0 : (grant[3] ? 1'b1 : state[11]);
    end
  end
  assign state_q = state;
endmodule
