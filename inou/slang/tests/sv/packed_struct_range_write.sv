// A constant part-select write that spans several fields of a packed struct variable
// (`s[4:0] = v`), the cvfpu `unit_status[4:0] = fflags[4:0]` shape. The struct is kept as
// per-field leaves, so the 5-bit value is split across five 1-bit fields; each piece must be
// narrowed to its field window (the low bits of the shifted value, 1800 11.5.1) before
// it reaches `set_mask`, or the bitwidth check reports "N-bit value does not fit the
// 1-bit destination slice".
// :test: lec
typedef struct packed {
  logic nv, dz, of, uf, nx;
} status_t;

typedef struct packed {
  logic [2:0] mode;
  logic       en;
  logic [3:0] cnt;
} cfg_t;

module packed_struct_range_write (
    input  logic [4:0] fl,
    input  logic [7:0] raw,
    input  logic       sel,
    output status_t    y,
    output cfg_t       z
);
  status_t st;
  cfg_t    cfg;
  always_comb begin
    st[4:0] = fl[4:0];
    cfg     = '0;
    cfg[6:2] = raw[4:0];
    if (sel) cfg[7:4] = raw[7:4];
  end
  assign y = st;
  assign z = cfg;
endmodule
