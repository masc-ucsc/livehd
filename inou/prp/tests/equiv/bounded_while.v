module bounded_while(input logic [7:0] num, output logic [3:0] count);
  integer i;
  always_comb begin
    i = 0;
    while ((i < 8) & ~num[7-i]) i = i + 1;
    count = i[3:0];
  end
endmodule
