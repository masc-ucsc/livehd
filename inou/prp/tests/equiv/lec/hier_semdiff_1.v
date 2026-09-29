/*
The claims lec_semdiff_test.sh made before it was consolidated away. The verdict
alone is NOT the test: this pair exists to pin WHICH defs pass.semdiff skips
structurally and which the solver actually decides. `leaf` is De-Morgan-rewritten,
so it MUST be solved; `mid` and `top` are untouched, so they MUST be skipped.
Without these greps the fixture still reports PROVEN if semdiff wrongly matched
the changed leaf, or if the semdiff shortcut stopped firing entirely.
:lec_grep: lec\[hier\]: 'leaf' PROVEN
:lec_grep_not: lec\[hier\]: 'leaf' MATCHED
:lec_grep: lec\[hier\]: 'mid' MATCHED \(semdiff
:lec_grep: lec\[hier\]: 'top' MATCHED \(semdiff
*/
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = ~((~a) | (~b)); endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
