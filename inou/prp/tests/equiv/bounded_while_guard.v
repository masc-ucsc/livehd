module bounded_while_guard(input logic [7:0] num, output logic [2:0] count, output logic [4:0] total, output logic [2:0] skipped);
  integer i, j;
  always_comb begin
    total = 1;
    i = 2;
    while (i < 6 && !num[i]) begin
      total = total + i;
      i++;
    end
    count = i[2:0];
    j = 6;
    while (j < 6 && num[0]) j++;
    skipped = j[2:0];
  end
endmodule
