module connect_through(
  input signed reset
  ,input signed clock
  ,output reg signed out1
  ,output reg signed out2
);
  \connect_through.sub  \iyy1_0.sub (
    .clock(clock)
    ,.reset(reset)
  );
  \connect_through.sub  \iyy2_0.sub (
    .clock(clock)
    ,.reset(reset)
  );
endmodule

module \connect_through.sub (
  input signed reset
  ,input signed clock
  ,output reg signed [7:0] \xx1.out1
  ,output reg signed [7:0] \xx2.out1
);
  reg signed [7:0] t_pin44_6;
  reg signed [7:0] t_pin45_6;
  \connect_through.sub.subsub  \i%xx2_0.subsub (
    .clock(clock)
    ,.foo(5'sha)
    ,.out1(t_pin45_6)
    ,.reset(reset)
  );
  \connect_through.sub.subsub  \i%xx1_0.subsub (
    .clock(clock)
    ,.foo(3'sh2)
    ,.out1(t_pin44_6)
    ,.reset(reset)
  );
  always_comb begin
    \xx2.out1  = t_pin45_6;
    \xx1.out1  = t_pin44_6;
  end
endmodule

module \connect_through.sub.subsub (
  input signed reset
  ,input signed clock
  ,input signed [4:0] foo
  ,output reg signed [7:0] out1
);
  reg signed [7:0] counter;
  reg signed [7:0] ___next_counter;
  reg signed [7:0] t_pin44_0;
  always_comb begin
    t_pin44_0 = (counter + foo);
  end
  always_comb begin
    out1 = counter;
    ___next_counter = t_pin44_0;
  end
  always @(posedge clock ) begin
    if (reset) begin
      counter <= 'h0;
    end else begin
      counter <= ___next_counter;
    end
  end
endmodule
