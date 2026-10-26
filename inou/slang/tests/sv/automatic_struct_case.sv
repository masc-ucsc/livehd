// :test: roundtrip
module automatic_struct_case(input [1:0] op, input [3:0] a, output logic [7:0] y);
typedef struct packed {logic [3:0] a; logic [3:0] b;} status_t;
always_comb begin
 y=0;
 case(op)
 1: begin
  automatic status_t status, other;
  status='0;
  status.a=a;
  y=status;
  other=status_t'(y);
  y[3:0]=other.a;
 end
 default: y=8'h55;
 endcase
end
endmodule
