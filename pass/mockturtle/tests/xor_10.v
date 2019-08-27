module xor_10(
    input a,
    input b,
    output z
    );

wire temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7, temp8;
    assign temp0 = a ^ b;
    assign temp1 = temp0 ^ temp0;
    assign temp2 = temp1 ^ temp1;
    assign temp3 = temp2 ^ temp2;
    assign temp4 = temp3 ^ temp3;
    assign temp5 = temp4 ^ temp4;
    assign temp6 = temp5 ^ temp5;
    assign temp7 = temp6 ^ temp6;
    assign temp8 = temp7 ^ temp7;
    assign z = temp8 ^ temp8;


endmodule