// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
interface active_if;
 logic data;
 assign data = 1'b1;
 modport sink(input data);
endinterface
module active_sink(active_if.sink b, output y);
 assign y = b.data;
endmodule
module interface_active(output y);
 active_if b();
 active_sink s(b, y);
endmodule

interface bidir_if;
 wire data;
 modport bidir(inout data);
endinterface
module bidir_sink(bidir_if.bidir b, output y);
 assign y = b.data;
endmodule
module interface_bidir(output y);
 bidir_if b();
 bidir_sink s(b, y);
endmodule

interface plain_if;
 logic data;
endinterface
module undirected_sink(plain_if b, output y);
 assign y = b.data;
endmodule
module interface_undirected(input x, output y);
 plain_if b();
 assign b.data = x;
 undirected_sink s(b, y);
endmodule
