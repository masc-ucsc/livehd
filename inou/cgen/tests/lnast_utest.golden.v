module lnast_utest (input clk,
                    input reset,
                    input [0] $a,
                    input [0] $b,
                    input     $c,
                    input     $d,
                    input     $e,
                    input     $f,
                    output     %o1,
                    output     %o2,
                    output [0] %s);
  wire result;
  wire x;
  wire y;

  always @(*) begin
    *(LNAST: $a as __bits:1)*
    *(LNAST: $b as __bits:1)*
    *(LNAST: %s as __bits:1)*
    %s = $a & $b;
    result = lnast_utest_fun1(clk, reset, a:3, b:4);
    x = $a;
    if ($a > 1) {
      x = $e;
      if ($a > 2) {
        x = $b;
      } elif ($a + 1 > 3) {
        x = $c;
      } else {
        x = $d;
      }
      y = $e;
    } else {
      x = $f;
    }
    %o1 = x + $a;
    %o2 = y + $a;
  end
end module
