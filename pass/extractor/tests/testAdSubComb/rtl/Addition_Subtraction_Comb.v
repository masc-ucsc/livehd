module Addition_Subtraction_Comb(input [31:0] a,
    input [31:0] b,
    input add_sub_signal, 
    output [31:0] res,
    output exception
);


wire [7:0] exp_a = a[30:23];
wire [7:0] exp_b = b[30:23];
wire [22:0] mant_a = a[22:0];
wire [22:0] mant_b = b[22:0];
wire sign_a = a[31];
wire sign_b = b[31];


wire operation = sign_a ^ sign_b ^ add_sub_signal;


wire [24:0] extended_mant_a = |exp_a ? {1'b1, mant_a} : {1'b0, mant_a};
wire [24:0] extended_mant_b = |exp_b ? {1'b1, mant_b} : {1'b0, mant_b};


wire [7:0] exp_diff = exp_a > exp_b ? exp_a - exp_b : exp_b - exp_a;
wire [24:0] aligned_mant_a = exp_a >= exp_b ? extended_mant_a : extended_mant_a >> exp_diff;
wire [24:0] aligned_mant_b = exp_b > exp_a ? extended_mant_b : extended_mant_b >> exp_diff;
wire [7:0] aligned_exp = exp_a > exp_b ? exp_a : exp_b;


wire [25:0] sum = operation ? aligned_mant_a + aligned_mant_b : aligned_mant_a - aligned_mant_b;


wire [4:0] leading_zeros = count_leading_zeros(sum[24:0]); 
wire [24:0] normalized_mant = sum << leading_zeros;
wire [7:0] normalized_exp = aligned_exp - leading_zeros;


wire result_sign = operation ? sign_a : (exp_a > exp_b ? sign_a : sign_b);


assign exception = &exp_a | &exp_b;


assign res = {result_sign, normalized_exp[7:0], normalized_mant[22:0]};


function [4:0] count_leading_zeros(input [24:0] value);
begin
    
    if (value[24]) count_leading_zeros = 0;
    else if (value[23]) count_leading_zeros = 1;
    else if (value[22]) count_leading_zeros = 2;
    else if (value[21]) count_leading_zeros = 3;
    else if (value[20]) count_leading_zeros = 4;
    else if (value[19]) count_leading_zeros = 5;
    else if (value[18]) count_leading_zeros = 6;
    else if (value[17]) count_leading_zeros = 7;
    else if (value[16]) count_leading_zeros = 8;
    else if (value[15]) count_leading_zeros = 9;
    else if (value[14]) count_leading_zeros = 10;
    else if (value[13]) count_leading_zeros = 11;
    else if (value[12]) count_leading_zeros = 12;
    else if (value[11]) count_leading_zeros = 13;
    else if (value[10]) count_leading_zeros = 14;
    else if (value[9]) count_leading_zeros = 15;
    else if (value[8]) count_leading_zeros = 16;
    else if (value[7]) count_leading_zeros = 17;
    else if (value[6]) count_leading_zeros = 18;
    else if (value[5]) count_leading_zeros = 19;
    else if (value[4]) count_leading_zeros = 20;
    else if (value[3]) count_leading_zeros = 21;
    else if (value[2]) count_leading_zeros = 22;
    else if (value[1]) count_leading_zeros = 23;
    else if (value[0]) count_leading_zeros = 24;
    else count_leading_zeros = 25; 
end
endfunction

endmodule

