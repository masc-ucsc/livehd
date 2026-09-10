module packed_variable_modifiers(input signed [3:0] a, b, output signed [7:0] s,
 output signed [3:0] high, output [3:0] count, output parity, output any, output all);
 wire [7:0] p = {b,a};
 assign s = p;
 assign high = b;
 assign count = {3'b0,p[0]} + {3'b0,p[1]} + {3'b0,p[2]} + {3'b0,p[3]} +
                {3'b0,p[4]} + {3'b0,p[5]} + {3'b0,p[6]} + {3'b0,p[7]};
 assign parity = ^p;
 assign any = |p;
 assign all = &p;
endmodule
