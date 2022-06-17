module array(
   input signed [3:0] data
  ,input signed [1:0] in
  ,output reg signed [3:0] out
);
	reg signed [3:0] t_pin87_0;
	reg signed [3:0] t_pin55_0;
	reg signed [3:0] \i___t1_0:tmp_0:__memory [3:0];
	always_comb begin
		\i___t1_0:tmp_0:__memory [0] = 0;
		\i___t1_0:tmp_0:__memory [1] = 0;
		\i___t1_0:tmp_0:__memory [2] = 0;
		\i___t1_0:tmp_0:__memory [3] = 0;

		\i___t1_0:tmp_0:__memory [in] = t_pin87_0;
		t_pin55_0 = \i___t1_0:tmp_0:__memory [in];
	end
	always_comb begin
		t_pin87_0 = data;
	end
	always_comb begin
		out = t_pin55_0;
	end
endmodule
