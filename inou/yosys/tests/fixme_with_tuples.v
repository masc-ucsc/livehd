
typedef struct packed{
  logic [3:0]    addr;  //4 bits
  logic [7:0]    data;  //8 bits
}Packet_type; // 12bits

module with_tupples(input [3:0] a, input [7:0] b, output Packet_type c);

  always_comb begin
    c.addr = a;
    c.data = b;
  end

endmodule

module with_tupples_v95(input [3:0] a, input [7:0] b, output [11:0] c);

  assign ___tmp_c_addr = c[3:0];
  assign ___tmp_c_data = c[11:4];

  always @(a or b) begin
    ___tmp_c_addr = a;
    ___tmp_c_data = b;
  end

  always @(___tmp_c_addr or ___tmp_c_data) begin
    c = {___tmp_c_data, ___tmp_c_addr};
  end

endmodule

