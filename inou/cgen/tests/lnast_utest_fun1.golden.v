module lnast_utest_fun1 (input clk,
                         input reset,
                         input $a,
                         input $b,
                         output %o);
  always @(*) begin
    %o = $a ^ $b;
  end
end module

