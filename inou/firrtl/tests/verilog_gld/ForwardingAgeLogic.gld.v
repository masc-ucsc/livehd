module ForwardingAgeLogic(
  input        clock,
  input        reset,
  input  [7:0] io_addr_matches,
  input  [2:0] io_youngest_st_idx,
  output       io_forwarding_val,
  output [2:0] io_forwarding_idx
);
  wire  _T = 3'h0 >= io_youngest_st_idx; // @[lsu.scala 1691:17]
  wire  age_mask_0 = _T ? 1'h0 : 1'h1; // @[lsu.scala 1690:19 1692:7 1693:22]
  wire  _T_1 = 3'h1 >= io_youngest_st_idx; // @[lsu.scala 1691:17]
  wire  age_mask_1 = _T_1 ? 1'h0 : 1'h1; // @[lsu.scala 1690:19 1692:7 1693:22]
  wire  _T_2 = 3'h2 >= io_youngest_st_idx; // @[lsu.scala 1691:17]
  wire  age_mask_2 = _T_2 ? 1'h0 : 1'h1; // @[lsu.scala 1690:19 1692:7 1693:22]
  wire  _T_3 = 3'h3 >= io_youngest_st_idx; // @[lsu.scala 1691:17]
  wire  age_mask_3 = _T_3 ? 1'h0 : 1'h1; // @[lsu.scala 1690:19 1692:7 1693:22]
  wire  _T_4 = 3'h4 >= io_youngest_st_idx; // @[lsu.scala 1691:17]
  wire  age_mask_4 = _T_4 ? 1'h0 : 1'h1; // @[lsu.scala 1690:19 1692:7 1693:22]
  wire  _T_5 = 3'h5 >= io_youngest_st_idx; // @[lsu.scala 1691:17]
  wire  age_mask_5 = _T_5 ? 1'h0 : 1'h1; // @[lsu.scala 1690:19 1692:7 1693:22]
  wire  _T_6 = 3'h6 >= io_youngest_st_idx; // @[lsu.scala 1691:17]
  wire  age_mask_6 = _T_6 ? 1'h0 : 1'h1; // @[lsu.scala 1690:19 1692:7 1693:22]
  wire [7:0] _T_14 = {1'h0,age_mask_6,age_mask_5,age_mask_4,age_mask_3,age_mask_2,age_mask_1,age_mask_0}; // @[lsu.scala 1699:46]
  wire [7:0] _T_15 = io_addr_matches & _T_14; // @[lsu.scala 1699:35]
  wire [15:0] matches_ = {_T_15,io_addr_matches}; // @[Cat.scala 29:58]
  wire [1:0] _GEN_13 = matches_[2] ? 2'h2 : {{1'd0}, matches_[1]}; // @[lsu.scala 1710:7 1712:28]
  wire [1:0] _GEN_15 = matches_[3] ? 2'h3 : _GEN_13; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_17 = matches_[4] ? 3'h4 : {{1'd0}, _GEN_15}; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_19 = matches_[5] ? 3'h5 : _GEN_17; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_21 = matches_[6] ? 3'h6 : _GEN_19; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_23 = matches_[7] ? 3'h7 : _GEN_21; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_25 = matches_[8] ? 3'h0 : _GEN_23; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_27 = matches_[9] ? 3'h1 : _GEN_25; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_29 = matches_[10] ? 3'h2 : _GEN_27; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_31 = matches_[11] ? 3'h3 : _GEN_29; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_33 = matches_[12] ? 3'h4 : _GEN_31; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_35 = matches_[13] ? 3'h5 : _GEN_33; // @[lsu.scala 1710:7 1712:28]
  wire [2:0] _GEN_37 = matches_[14] ? 3'h6 : _GEN_35; // @[lsu.scala 1710:7 1712:28]
  assign io_forwarding_val = matches_[15] | (matches_[14] | (matches_[13] | (matches_[12] | (matches_[11] | (matches_[10
    ] | (matches_[9] | (matches_[8] | (matches_[7] | (matches_[6] | (matches_[5] | (matches_[4] | (matches_[3] | (
    matches_[2] | (matches_[1] | matches_[0])))))))))))))); // @[lsu.scala 1710:7 1711:22]
  assign io_forwarding_idx = matches_[15] ? 3'h7 : _GEN_37; // @[lsu.scala 1710:7 1712:28]
endmodule
