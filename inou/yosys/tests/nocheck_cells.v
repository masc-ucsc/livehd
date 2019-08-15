
/******************************************************************************/ 
/*                                                                            */ 
/* Copyright (c) 1999 Sun Microsystems, Inc. All rights reserved.             */ 
/*                                                                            */ 
/* The contents of this file are subject to the current version of the Sun    */ 
/* Community Source License, microSPARCII ("the License"). You may not use    */ 
/* this file except in compliance with the License.  You may obtain a copy    */ 
/* of the License by searching for "Sun Community Source License" on the      */ 
/* World Wide Web at http://www.sun.com. See the License for the rights,      */ 
/* obligations, and limitations governing use of the contents of this file.   */ 
/*                                                                            */ 
/* Sun Microsystems, Inc. has intellectual property rights relating to the    */ 
/* technology embodied in these files. In particular, and without limitation, */ 
/* these intellectual property rights may include one or more U.S. patents,   */ 
/* foreign patents, or pending applications.                                  */ 
/*                                                                            */ 
/* Sun, Sun Microsystems, the Sun logo, all Sun-based trademarks and logos,   */ 
/* Solaris, Java and all Java-based trademarks and logos are trademarks or    */ 
/* registered trademarks of Sun Microsystems, Inc. in the United States and   */ 
/* other countries. microSPARC is a trademark or registered trademark of      */ 
/* SPARC International, Inc. All SPARC trademarks are used under license and  */ 
/* are trademarks or registered trademarks of SPARC International, Inc. in    */ 
/* the United States and other countries. Products bearing SPARC trademarks   */ 
/* are based upon an architecture developed by Sun Microsystems, Inc.         */ 
/*                                                                            */ 
/******************************************************************************/ 
/***************************************************************************
****************************************************************************
***
***  Program File:  @(#)cells.v
***
****************************************************************************
****************************************************************************/
//
// BASIC ENABLED REGISTER
// synopsys translate_off

`timescale 1 ns / 1 ns

module Mregister (out, in, clock, enable_l);
parameter bits = 32;	// number of bits in the register
output [bits-1:0] out;
input [bits-1:0] in;
input clock;
input enable_l; // must be low to enable

	reg [bits-1:0]	out;
	reg [bits-1:0] master;
			
	always @ (posedge clock) begin
		if((enable_l === 1'bx) || (clock === 1'bx))  begin
			master = {(bits){1'bx}};
			#1 out = master;
		end
		else if (~enable_l) begin
			master = in;
			#1 out = master;
		end
	end

endmodule

// synopsys translate_on
module tri32 (

    out,
    in,
    enable

    ) ;

    output [31:0] out ;
    input [31:0] in ;
    input enable ;

	tristate tristate_31_0 (out[0], in[0], enable);
	tristate tristate_31_1 (out[1], in[1], enable);
	tristate tristate_31_2 (out[2], in[2], enable);
	tristate tristate_31_3 (out[3], in[3], enable);
	tristate tristate_31_4 (out[4], in[4], enable);
	tristate tristate_31_5 (out[5], in[5], enable);
	tristate tristate_31_6 (out[6], in[6], enable);
	tristate tristate_31_7 (out[7], in[7], enable);
	tristate tristate_31_8 (out[8], in[8], enable);
	tristate tristate_31_9 (out[9], in[9], enable);
	tristate tristate_31_10 (out[10], in[10], enable);
	tristate tristate_31_11 (out[11], in[11], enable);
	tristate tristate_31_12 (out[12], in[12], enable);
	tristate tristate_31_13 (out[13], in[13], enable);
	tristate tristate_31_14 (out[14], in[14], enable);
	tristate tristate_31_15 (out[15], in[15], enable);
	tristate tristate_31_16 (out[16], in[16], enable);
	tristate tristate_31_17 (out[17], in[17], enable);
	tristate tristate_31_18 (out[18], in[18], enable);
	tristate tristate_31_19 (out[19], in[19], enable);
	tristate tristate_31_20 (out[20], in[20], enable);
	tristate tristate_31_21 (out[21], in[21], enable);
	tristate tristate_31_22 (out[22], in[22], enable);
	tristate tristate_31_23 (out[23], in[23], enable);
	tristate tristate_31_24 (out[24], in[24], enable);
	tristate tristate_31_25 (out[25], in[25], enable);
	tristate tristate_31_26 (out[26], in[26], enable);
	tristate tristate_31_27 (out[27], in[27], enable);
	tristate tristate_31_28 (out[28], in[28], enable);
	tristate tristate_31_29 (out[29], in[29], enable);
	tristate tristate_31_30 (out[30], in[30], enable);
	tristate tristate_31_31 (out[31], in[31], enable);

endmodule
// synopsys translate_off


// synopsys translate_on
module tri_regen_32 (		// tri-state with built-in flip-flop for enable

    out,
    in,
    clock,
    enable,
    reset

    ) ;

    output [31:0] out ;
    input [31:0] in ;
    input clock ;
    input enable ;
    input reset ;

    wire enable_ = ~enable ;
    wire reset_  = ~reset ;


	ATSBUFD tristate_31_24 (in[24],  in[25],  in[26],  in[27],
				in[28],  in[29],  in[30],  in[31],
				clock, 1'b0, 1'b0, enable_, reset_,
				out[24], out[25], out[26], out[27],
				out[28], out[29], out[30], out[31], );

	ATSBUFD tristate_23_16 (in[16],  in[17],  in[18],  in[19],
				in[20],  in[21],  in[22],  in[23],
				clock, 1'b0, 1'b0, enable_, reset_,
				out[16], out[17], out[18], out[19],
				out[20], out[21], out[22], out[23], );

	ATSBUFD tristate_15_8  (in[ 8],  in[ 9],  in[10],  in[11],
				in[12],  in[13],  in[14],  in[15],
				clock, 1'b0, 1'b0, enable_, reset_,
				out[ 8], out[ 9], out[10], out[11],
				out[12], out[13], out[14], out[15], );

	ATSBUFD tristate_7_0   (in[ 0],  in[ 1],  in[ 2],  in[ 3],
				in[ 4],  in[ 5],  in[ 6],  in[ 7],
				clock, 1'b0, 1'b0, enable_, reset_,
				out[ 0], out[ 1], out[ 2], out[ 3],
				out[ 4], out[ 5], out[ 6], out[ 7], );

endmodule
// synopsys translate_off


module Mregister_3 (out, in, clock, enable_l);
output [2:0] out;
input [2:0] in;
input clock;
input enable_l;
    reg [2:0] out;
    reg [2:0] master;
		    
    always @ (posedge clock) begin
	if((enable_l === 1'bx) || (clock === 1'bx))  begin
	    master = 4'bx;
	    #1 out = master;
	end
	else if (~enable_l) begin
	    master = in;
	    #1 out = master;
	end
    end
endmodule

module Mregister_4 (out, in, clock, enable_l);
output [3:0] out;
input [3:0] in;
input clock;
input enable_l;
    reg [3:0] out;
    reg [3:0] master;
		    
    always @ (posedge clock) begin
	if((enable_l === 1'bx) || (clock === 1'bx))  begin
	    master = 4'bx;
	    #1 out = master;
	end
	else if (~enable_l) begin
	    master = in;
	    #1 out = master;
	end
    end
endmodule

module Mregister_8 (out, in, clock, enable_l);
output [7:0] out;
input [7:0] in;
input clock;
input enable_l;
    reg [7:0] out;
    reg [7:0] master;
		    
    always @ (posedge clock) begin
	if((enable_l === 1'bx) || (clock === 1'bx))  begin
	    master = 8'bx;
	    #1 out = master;
	end
	else if (~enable_l) begin
	    master = in;
	    #1 out = master;
	end
    end
endmodule

module Mregister_32 (out, in, clock, enable_l);
output [31:0] out;
input [31:0] in;
input clock;
input enable_l;
    reg [31:0] out;
    reg [31:0] master;
		    
    always @ (posedge clock) begin
	if((enable_l === 1'bx) || (clock === 1'bx))  begin
	    master = 8'bx;
	    #1 out = master;
	end
	else if (~enable_l) begin
	    master = in;
	    #1 out = master;
	end
    end
endmodule
// Used in IU
module MregisterD (out, in, clock, enable_l);
parameter bits = 32;	// number of bits in the register
output [bits-1:0] out;
input [bits-1:0] in;
input clock;
input enable_l; // must be low to enable

	reg [bits-1:0]	out;
	reg [bits-1:0] master;

	always @ (in or clock or enable_l) begin
		if((clock === 1'bx) || (enable_l === 1'bx))
			master = 65'bx;
		else if(~clock & ~enable_l)
	 	     	master = in;
		
		if(clock) #1 out = master;
	end
endmodule

//--------------------------------------------------------------------------
// ENABLED REGISTER with synchronous reset

// module MflipflopR (out, in, clock, enable_l, reset);
//	parameter bits = 1; // number of bits in the register
//	output [bits-1:0] out;
//	input [bits-1:0] in;
//	input clock;
//	input enable_l;
//	input reset;
//	reg [bits-1:0] out;
//	reg [bits-1:0] master;
//	
//	always @ (posedge clock) begin
//	if((enable_l^clock^reset) === 1'bx)  begin
//	        master = {(bits){1'bx}};
//	        #1 out = master;
//	end
//	else if (reset) begin
//	        master = {(bits){1'b0}};
//	        #1 out = master;
//	end
//	    else if (~enable_l) begin
//	        master = in;
//	        #1 out = master;
//	    end
//	end
// endmodule


// synopsys translate_on
//-----------------------------------------------------------------------------
// BASIC 1-BIT FLIP FLOP

module Mflipflop (out, in, clock, enable_l);
output out;
input in;
input clock;
input enable_l; // must be low to allow master to open

	wire logic_0 = 1'b0 ;
	wire logic_1 = 1'b1 ;
	ASFFHA dff (.H(enable_l),.D(in),.Q(out),.CK(clock),.SM(logic_0),.SI(logic_0));

endmodule

//-----------------------------------------------------------------------------
// BASIC 1-BIT RESETABLE FLIP FLOP

module MflipflopR (out, in, clock, enable_l,reset);
output out;
input in;
input clock;
input enable_l; // must be low to allow master to open
input reset;

	wire logic_0 = 1'b0 ;
	wire logic_1 = 1'b1 ;
	wire resetn = ~reset ;
	ASFFRHA dff (.H(enable_l),.D(in),.Q(out),.CK(clock),.SM(logic_0),.SI(logic_0),.R(resetn));

endmodule

//-----------------------------------------------------------------------------
// BASIC 1-BIT SCANNABLE, RESETABLE FLIP FLOP

module Mflipflop_srh (out, in, scanen,sin, enable_l, reset_l, clock);
output out;
input in;
input scanen, sin ;
input clock;
input enable_l; // must be low to allow master to open
input reset_l;

	ASFFRHA dff (
	    .Q(out),
	    .D(in),
	    .SM(scanen),
	    .SI(sin),
	    .H(enable_l),
	    .R(reset_l),
	    .CK(clock)
	);

endmodule

//-----------------------------------------------------------------------------
// ASYNC RESETABLE FLIP FLOP
module cells(out, in, async_reset_l, clock) ;
	output out ;
	input in ;
	input async_reset_l ;
	input clock ;

	JSRFFA dff(.D(in), .CK(clock), .CL(async_reset_l), .PR(1'b1), .Q(out)) ;
endmodule

//-----------------------------------------------------------------------------
module Mflipflop_sh (out, in, scanen,sin, enable_l, clock);
output out;
input in;
input scanen, sin ;
input clock;
input enable_l; // must be low to allow master to open


	ASFFHA dff (
	    .Q(out),
	    .D(in),
	    .SM(scanen),
	    .SI(sin),
	    .H(enable_l),
	    .CK(clock)
	);

endmodule
//-----------------------------------------------------------------------------
//module S1dffrh (q,q_n,din,hold,reset_n,clk);
module Mflipflop_rh (out, in, enable_l, reset_l, clock);
output out;
input in;
input clock;
input enable_l; // must be low to allow master to open
input reset_l;

	ADFFRHA dff (
	    .Q(out),
	    .D(in),
	    .H(enable_l),
	    .R(reset_l),
	    .CK(clock)
	);

endmodule
//---------------------------------------------------------------------------
//module S1dffsr (q,din,hold,reset_n,clk);
module Mflipflop_sr (out, in, scanen, sin, reset_l, clock);
output out;
input in;
input clock;
input reset_l;
input scanen,sin ;

	ASFFRA dff (
	    .Q(out),
	    .D(in),
	    .R(reset_l),
	    .SM(scanen),
	    .SI(sin),
	    .CK(clock)
	);

endmodule

//---------------------------------------------------------------------------
//module S1dffs_d (q,din,hold,reset_n,clk);
module Mflipflop_s (out, in, scanen, sin, clock);
output out ;
input in ;
input clock ;
input sin ;
input scanen ;


	ASFFA dff (
	    .Q(out),
	    .D(in),
	    .SM(scanen),
	    .SI(sin),
	    .CK(clock)
	);

endmodule
//-----------------------------------------------------------------------------
//module S1dffr (q,din,hold,reset_n,clk);
module Mflipflop_r (out, in, reset_l, clock);
output out ;
input in ;
input clock ;
input reset_l ; // must be low to allow master to open


	ADFFRA dff (
	    .Q(out),
	    .D(in),
	    .R(reset_l),
	    .CK(clock)
	);

endmodule
//-----------------------------------------------------------------------------
//module S1dffr (q,din,hold,reset_n,clk);
module Mflipflop_h (out, in, enable_l, clock);
output out ;
input in ;
input clock ;
input enable_l ; // must be low to allow master to open

	ASFFA dff (
	    .Q(out),
	    .D(in),
	    .SM(enable_l),	// use the scan mux to implement hold function
	    .SI(out),
	    .CK(clock)
	);

endmodule
//-----------------------------------------------------------------------------
//module S1dff (q,din,clk);
module Mflipflop_noop (out, in, clock);
output out ;
input in ;
input clock ;

	JDFFA dff (
	    .Q(out),
	    .D(in),
	    .CK(clock)
	);

endmodule

//-----------------------------------------------------------------------------
// tristate driver model

module tristate (out, in, enable);
output out ;
input  in ;
input enable ;

	bufif1  drv_x (out, in, enable);

endmodule
//-----------------------------------------------------------------------------
// tristate driver model
//module S1drvi_h (out,in,g);
module invtristate (out, in, enable);
output out ;
input  in ;
input enable ;
wire W_1;

	not   U_1	(W_1, in);
	bufif1 drvi_x 	(out, W_1, enable);

endmodule
//----------------------------------------------------------------------
module sc_mux2_a_30 (out,in0,in1,select) ;
    output [29:0] out ;
    input  [29:0] in0, in1 ;
    input select ;

    reg [29:0] out ;

	always @ (select or in0 or in1)
		case (select) // synopsys parallel_case full_case
			1'b0:	out = in0;
			1'b1:	out = in1;
			// synopsys translate_off	
			default: out = 65'hx;
			// synopsys translate_on
		endcase
endmodule

module sc_mux3_d_27 (out,in0,sel0,in1,sel1,in2,sel2) ;
    output [26:0] out ;
    input  [26:0] in0,in1,in2 ;
    input sel0,sel1,sel2 ;

    reg [26:0] out ;

	wire [2:0] select = {sel0,sel1,sel2} ;
	always @ (select or in0 or in1 or in2)
		case (select) // synopsys parallel_case full_case
			5'b100:	out = in0;
			5'b010:	out = in1;
			5'b001:	out = in2 ;
			// synopsys translate_off	
			default: out = 65'hx;
			// synopsys translate_on
		endcase
endmodule

module sc_mux3_d_3 (out,in0,sel0,in1,sel1,in2,sel2) ;
    output [2:0] out ;
    input  [2:0] in0,in1,in2 ;
    input sel0,sel1,sel2 ;

    reg [2:0] out ;

	wire [2:0] select = {sel0,sel1,sel2} ;
	always @ (select or in0 or in1 or in2)
		case (select) // synopsys parallel_case full_case
			5'b100:	out = in0;
			5'b010:	out = in1;
			5'b001:	out = in2 ;
			// synopsys translate_off	
			default: out = 65'hx;
			// synopsys translate_on
		endcase
endmodule

//----------------------------------------------------------------------------
module sc_mux5_d_30 (out,in0,sel0,in1,sel1,in2,sel2,in3,sel3,in4,sel4) ;
    output [29:0] out ;
    input  [29:0] in0,in1,in2,in3,in4 ;
    input sel0,sel1,sel2,sel3,sel4 ;

    reg [29:0] out ;

	wire [4:0] select = {sel0,sel1,sel2,sel3,sel4} ;
	always @ ((select) or (in0) or (in1) or (in2) or (in3) or (in4))
		case (select) // synopsys parallel_case full_case
			5'b10000:	out = in0;
			5'b01000:	out = in1;
			5'b00100:	out = in2 ;
			5'b00010:	out = in3 ;
			5'b00001:	out = in4 ;
			// synopsys translate_off	
			default: out = 65'hx;
			// synopsys translate_on
		endcase
endmodule

//-----------------------------------------------------------------------------
// 2 INPUT MUX

module Mmux2 (out, in0, in1, select);
parameter bits = 32;	// bits in mux
output [bits-1:0] out;
input [bits-1:0] in0, in1;
input select;

	reg [bits-1:0] out;

	always @ (select or in0 or in1)
	case (select)	// synopsys parallel_case full_case
		0: out = in0;
		1: out = in1;
		// synopsys translate_off
		default: out = 65'bx ;
		// synopsys translate_on
	endcase
endmodule

//-----------------------------------------------------------------------------
// 4 INPUT MUX

module Mmux4 (out, in0, in1, in2, in3, select);
parameter bits = 32;	// bits in mux
output [bits-1:0] out;
input [bits-1:0] in0, in1, in2, in3;
input [1:0]  select;

	reg [bits-1:0] out;

	always @ (select or in0 or in1 or in2 or in3) begin
		case (select)	// synopsys parallel_case full_case
			2'b00:		out = in0;
			2'b01:		out = in1;
			2'b10:		out = in2;
			2'b11:		out = in3;
			// synopsys translate_off	
			default:	out = 65'hx;
			// synopsys translate_on
		endcase
	end
endmodule

//-----------------------------------------------------------------------------
module Mmux4_1 (out, in0, in1, in2, in3, select);
output out;
input in0, in1, in2, in3;
input [1:0] select;

	reg out;

	always @ (select or in0 or in1 or in2 or in3) begin
		case (select)	// synopsys parallel_case full_case
			2'b00:		out = in0;
			2'b01:		out = in1;
			2'b10:		out = in2;
			2'b11:		out = in3;
			// synopsys translate_off	
			default:	out = 1'hx;
			// synopsys translate_on
		endcase
	end
endmodule


//-----------------------------------------------------------------------------
// 3 INPUT MUX with FULLY DECODED SELECTS

module Mmux3d (out, in0, s0, in1, s1, in2, s2);
parameter bits = 32;	// bits in mux
output [bits-1:0] out;
input [bits-1:0] in0, in1, in2;
input s0, s1, s2;

	reg [bits-1:0] out;

	always @ (s0 or s1 or s2 or in0 or in1 or in2) begin
		case ({s2, s1, s0})	// synopsys parallel_case full_case
			3'b001:		out = in0;
			3'b010:		out = in1;
			3'b100:		out = in2;
			// synopsys translate_off	
			default:	out = 65'hx;
			// synopsys translate_on
		endcase
	end
endmodule

//-----------------------------------------------------------------------------
// 4 INPUT MUX with FULLY DECODED SELECTS

module Mmux4d (out, in0, s0, in1, s1, in2, s2, in3, s3);
parameter bits = 32;	// bits in mux
output [bits-1:0] out;
input [bits-1:0] in0, in1, in2, in3;
input s0, s1, s2, s3;

	reg [bits-1:0] out;

	always @ (s0 or s1 or s2 or s3 or in0 or in1 or in2 or in3) begin
		case ({s3, s2, s1, s0})	// synopsys parallel_case full_case
			4'b0001:	out = in0;
			4'b0010:	out = in1;
			4'b0100:	out = in2;
			4'b1000:	out = in3;
			// synopsys translate_off	
			default:	out = 65'hx;
			// synopsys translate_on
		endcase
	end
endmodule


//-----------------------------------------------------------------------------
// 5 INPUT MUX with FULLY DECODED SELECTS

module Mmux5d (out, in0, s0, in1, s1, in2, s2, in3, s3, in4, s4);
parameter bits = 32;	// bits in mux
output [bits-1:0] out;
input [bits-1:0] in0, in1, in2, in3, in4;
input s0, s1, s2, s3, s4;

	reg [bits-1:0] out;

	always @ (s0 or s1 or s2 or s3 or s4
				or in0 or in1 or in2 or in3 or in4) begin
		case ({s4, s3, s2, s1, s0})	// synopsys parallel_case full_case
			5'b00001:	out = in0;
			5'b00010:	out = in1;
			5'b00100:	out = in2;
			5'b01000:	out = in3;
			5'b10000:	out = in4;
			// synopsys translate_off
			default:	out = 65'hx;
			// synopsys translate_on
		endcase
	end
endmodule


//-----------------------------------------------------------------------------
// 6 INPUT MUX with FULLY DECODED SELECTS

module Mmux6d (out, in0, s0, in1, s1, in2, s2, in3, s3, in4, s4, in5, s5);
parameter bits = 32;	// bits in mux
output [bits-1:0] out;
input [bits-1:0] in0, in1, in2, in3, in4, in5;
input s0, s1, s2, s3, s4, s5;

	reg [bits-1:0] out;

	always @ (s0 or s1 or s2 or s3 or s4 or s5
			or in0 or in1 or in2 or in3 or in4 or in5) begin
		case ({s5, s4, s3, s2, s1, s0})	// synopsys parallel_case full_case
			6'b000001:	out = in0;
			6'b000010:	out = in1;
			6'b000100:	out = in2;
			6'b001000:	out = in3;
			6'b010000:	out = in4;
			6'b100000:	out = in5;
			// synopsys translate_off
			default:	out = 65'hx;
			// synopsys translate_on
		endcase
	end
endmodule


//-----------------------------------------------------------------------------
// 7 INPUT MUX with FULLY DECODED SELECTS

module Mmux7d (out, in0, s0, in1, s1, in2, s2, in3, s3, in4, s4,
						in5, s5, in6, s6);
parameter bits = 32;	// bits in mux
output [bits-1:0] out;
input [bits-1:0] in0, in1, in2, in3, in4, in5, in6;
input s0, s1, s2, s3, s4, s5, s6;

	reg [bits-1:0] out;

	always @ (s0 or s1 or s2 or s3 or s4 or s5 or s6
		or in0 or in1 or in2 or in3 or in4 or in5 or in6) begin
		case ({s6, s5, s4, s3, s2, s1, s0})	// synopsys parallel_case full_case
			7'b0000001:	out = in0;
			7'b0000010:	out = in1;
			7'b0000100:	out = in2;
			7'b0001000:	out = in3;
			7'b0010000:	out = in4;
			7'b0100000:	out = in5;
			7'b1000000:	out = in6;
			// synopsys translate_off
			default:	out = 65'hx;
			// synopsys translate_on
		endcase
	end
endmodule


//-----------------------------------------------------------------------------
//  4-bit, 8-input mux with undecoded selects.

module Mmux8_4 (out,in7,in6,in5,in4,in3,in2,in1,in0,select);
	output [3:0] out;
	input [3:0] in7,in6,in5,in4,in3,in2,in1,in0;
	input [2:0] select;

	reg [3:0] out;

	always @(select or in7 or in6 or in5 or in4 or in3 or in2 or in1 or in0) begin
		case(select)	// synopsys parallel_case full_case
			3'b111: out = in7;
			3'b110: out = in6;
			3'b101: out = in5;
			3'b100: out = in4;
			3'b011: out = in3;
			3'b010: out = in2;
			3'b001: out = in1;
			3'b000: out = in0;
			// synopsys translate_off
			default: out = 65'hx;
			// synopsys translate_on
		endcase
	end
endmodule



