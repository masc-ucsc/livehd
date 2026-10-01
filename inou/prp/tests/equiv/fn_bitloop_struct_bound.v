// Golden for fn_bitloop_struct_bound: a function-local vector written BIT BY
// BIT in a loop whose bound is a struct-parameter field (the cva6 `inside_rules`
// shape). The slang bit-write lowering updates `pass` in place
// (`set_mask(pass, pass, k, v)`), and the loop is rolled into a `__loop` body.
// The roll planner saw only `store`s as writes, so `pass` looked READ-ONLY:
// it was captured by value as the constant 0 and the loop's writes were lost
// -- `o = f(Cfg, a)` compiled to `o = 0` (exit 0; LEC PROVEN against itself,
// since both sides went through the same reader). The Pyrope twin below is the
// function's truth table, so a lost carry refutes.
typedef struct packed { int unsigned nr_rules; } cfg_t;
module fn_bitloop_struct_bound(input logic [1:0] a, output logic o);
  localparam cfg_t Cfg = '{nr_rules: 3};
  function automatic logic f(cfg_t cfg, logic [1:0] address);
    logic [3:0] pass;
    pass = '0;
    for (int unsigned k = 0; k < cfg.nr_rules; k++) begin
      pass[k] = address == k;
    end
    return |pass;
  endfunction
  assign o = f(Cfg, a);
endmodule
