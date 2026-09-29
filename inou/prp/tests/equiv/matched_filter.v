// Behavioral golden for matched_filter.prp: an N-tap pulse-compression
// correlator — a tap chain (delayed sample x coefficient, both loaded through
// shift chains) feeding a LOG2N-level registered adder tree. Written from the
// same specification, as explicit generate loops.
module tap(
  input               clock,
  input               reset,
  input  signed [3:0] x_in,
  input  signed [3:0] c_in,
  input               load,
  output signed [3:0] x_out,
  output signed [3:0] c_out,
  output signed [7:0] p
);
  reg signed [3:0] x_r;
  reg signed [3:0] c_r;
  always @(posedge clock) begin
    if (reset) begin
      x_r <= 4'sd0;
      c_r <= 4'sd0;
    end else begin
      x_r <= x_in;
      if (load) c_r <= c_in;
    end
  end
  assign x_out = x_r;
  assign c_out = c_r;
  assign p     = x_r * c_r;
endmodule

module add_node(
  input                clock,
  input                reset,
  input  signed [11:0] a,
  input  signed [11:0] b,
  output signed [11:0] s
);
  reg signed [11:0] s_r;
  always @(posedge clock) begin
    if (reset) s_r <= 12'sd0;
    else       s_r <= a + b;
  end
  assign s = s_r;
endmodule

module \matched_filter.top (
  input                clock,
  input                reset,
  input  signed [3:0]  x,
  input  signed [3:0]  c_in,
  input                load,
  output signed [11:0] y
);
  localparam N     = 8;
  localparam LOG2N = 3;
  localparam SW    = 12;

  // Tap chain: xs[i]/cs[i] enter tap i, xs[i+1]/cs[i+1] leave it.
  wire signed [3:0]  xs [0:N];
  wire signed [3:0]  cs [0:N];
  wire signed [7:0] p  [0:N-1];
  assign xs[0] = x;
  assign cs[0] = c_in;
  genvar i, l, j;
  generate
    for (i = 0; i < N; i = i + 1) begin : taps
      tap t(.clock(clock), .reset(reset), .x_in(xs[i]), .c_in(cs[i]), .load(load),
            .x_out(xs[i+1]), .c_out(cs[i+1]), .p(p[i]));
    end
  endgenerate

  // Adder tree: level l holds N>>l values as one packed bus; level 0 is the
  // sign-extended products, level l+1 pairs level l through a registered add.
  wire [SW*N-1:0] lv [0:LOG2N];
  generate
    for (i = 0; i < N; i = i + 1) begin : lvl0
      assign lv[0][SW*i +: SW] = {{(SW-8){p[i][7]}}, p[i]};
    end
    for (l = 0; l < LOG2N; l = l + 1) begin : levels
      for (j = 0; j < (N >> (l + 1)); j = j + 1) begin : nodes
        add_node n(.clock(clock), .reset(reset),
                   .a(lv[l][SW*(2*j)   +: SW]),
                   .b(lv[l][SW*(2*j+1) +: SW]),
                   .s(lv[l+1][SW*j +: SW]));
      end
      // Entries that fall out of the live range at this level are unused.
      if ((N >> (l + 1)) < N) begin : pad
        assign lv[l+1][SW*N-1 : SW*(N >> (l + 1))] = {(SW*(N - (N >> (l + 1)))){1'b0}};
      end
    end
  endgenerate
  assign y = lv[LOG2N][SW-1:0];
endmodule
