typedef struct packed { logic flag; logic [3:0] value; } rob_t;
module struct_port(input rob_t i, output rob_t o, output logic [4:0] w);
  assign o.flag = i.value[0];
  assign o.value = i.value + 1;
  assign w = {i.flag, i.value};
endmodule
