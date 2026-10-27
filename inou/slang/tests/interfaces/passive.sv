// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
interface pipe_if #(parameter W=8);
 logic valid, ready;
 logic [W-1:0] data;
 modport master(output valid, data, input ready);
 modport slave(input valid, data, output ready);
endinterface
module consumer(pipe_if.slave b, input allow, output [7:0] y);
 assign b.ready = allow;
 assign y = b.valid ? b.data : 8'h55;
endmodule
module producer(pipe_if.master b, input [7:0] d, input v, output r);
 assign b.data = d ^ 8'h96;
 assign b.valid = v;
 assign r = b.ready;
endmodule
module interface_smoke(input [7:0] d, input v, allow, output [7:0] y, output r);
 pipe_if b();
 consumer c(b, allow, y);
 producer p(b, d, v, r);
endmodule

interface typed_if #(parameter type T = logic);
 T payload;
 logic valid, ready;
 modport source(output payload, valid, input ready);
 modport sink(input payload, valid, output ready);
endinterface

module typed_sink(typed_if.sink b, input enable, output [15:0] data, output valid);
 assign data = 16'(b.payload);
 assign valid = b.valid;
 assign b.ready = enable ^ b.valid;
endmodule

module typed_forward(typed_if.sink a, typed_if.source b);
 assign b.payload = a.payload;
 assign b.valid = a.valid;
 assign a.ready = b.ready;
endmodule

module interface_types(input [3:0] x, input [15:0] y, input v, enable,
                       output [15:0] a, b, output [1:0] valid, ready);
 typedef struct packed { logic [3:0] tag; logic [11:0] data; } payload_t;
 typed_if #(.T(logic [3:0])) narrow();
 typed_if #(.T(payload_t)) wide();
 typed_if #(.T(payload_t)) forwarded();
 assign narrow.payload = x;
 assign narrow.valid = v;
 assign wide.payload.tag = y[15:12];
 assign wide.payload.data = y[11:0];
 assign wide.valid = ~v;
 typed_forward f(wide, forwarded);
 typed_sink u0(narrow, enable, a, valid[0]);
 typed_sink u1(forwarded, enable, b, valid[1]);
 assign ready = {wide.ready, narrow.ready};
endmodule

module array_sink(pipe_if.slave bus[1:0], input [1:0] enable, output [15:0] data);
 for (genvar i=0; i<2; i++) begin: g
   consumer c(bus[i], enable[i], data[8*i+:8]);
 end
endmodule
module array_forward(pipe_if.slave a[0:1], pipe_if.master b[0:1]);
 for (genvar i=0; i<2; i++) begin: g
   assign b[i].valid = a[i].valid;
   assign b[i].data = a[i].data;
   assign a[i].ready = b[i].ready;
 end
endmodule
module interface_arrays(input [15:0] d, input [1:0] v, enable, output [15:0] y, output [1:0] ready);
 pipe_if a[0:1]();
 pipe_if b[0:1]();
 for (genvar i=0; i<2; i++) begin: g
   assign a[i].data = d[8*i+:8];
   assign a[i].valid = v[i];
   assign ready[i] = a[i].ready;
 end
 array_forward f(a, b);
 array_sink s(b, enable, y);
endmodule

module registered_source(pipe_if.master b, input clk, rst, input [7:0] d, input v);
 always_ff @(posedge clk) begin
   if (rst) begin b.data <= 0; b.valid <= 0; end
   else if (b.ready) begin b.data <= d; b.valid <= v; end
 end
endmodule
module interface_state(input clk, rst, input [7:0] d, input v, enable, output [7:0] y, output r);
 pipe_if b();
 registered_source s(b, clk, rst, d, v);
 consumer c(b, enable, y);
 assign r = b.ready;
endmodule

module shaped_sink(typed_if.sink b[0:1], output [31:0] data);
 assign data = {16'(b[1].payload), 16'(b[0].payload)};
 assign b[0].ready = b[0].valid;
 assign b[1].ready = b[1].valid;
endmodule
module interface_shapes(input [7:0] x, input [31:0] y, input [3:0] valid,
                        output [31:0] a, b, output [3:0] ready);
 typed_if #(.T(logic [3:0])) narrow[0:1]();
 typed_if #(.T(logic [15:0])) wide[0:1]();
 for (genvar i=0; i<2; i++) begin: g
   assign narrow[i].payload = x[i*4+:4];
   assign wide[i].payload = y[i*16+:16];
   assign narrow[i].valid = valid[i];
   assign wide[i].valid = valid[i+2];
   assign ready[i] = narrow[i].ready;
   assign ready[i+2] = wide[i].ready;
 end
 shaped_sink s0(narrow, a);
 shaped_sink s1(wide, b);
endmodule

module loop_source(pipe_if.master b, input x);
 assign b.valid = b.ready ^ x;
 assign b.data = 0;
endmodule
module loop_sink(pipe_if.slave b, output y);
 assign b.ready = b.valid;
 assign y = b.valid;
endmodule
module interface_loop(input x, output y);
 pipe_if b();
 loop_source s(b, x);
 loop_sink t(b, y);
endmodule

module feedback_sink(pipe_if.slave b, input enable, output [7:0] y);
 assign b.ready = enable & ~b.valid;
 assign y = b.data;
endmodule
module interface_state_feedback(input clk, rst, input [7:0] d, input v, enable,
                                output [7:0] y, output r);
 pipe_if b();
 registered_source s(b, clk, rst, d, v);
 feedback_sink c(b, enable, y);
 assign r = b.ready;
endmodule
