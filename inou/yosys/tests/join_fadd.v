
//==============================================================================
//      File:           $URL$
//      Version:        $Revision$
//      Author:         Jose Renau  (http://masc.cse.ucsc.edu/)
//                      Elnaz Ebrahimi
//      Copyright:      Copyright 2011 UC Santa Cruz
//==============================================================================

//==============================================================================
//      Section:        License
//==============================================================================
//      Copyright (c) 2011, Regents of the University of California
//      All rights reserved.
//
//      Redistribution and use in source and binary forms, with or without modification,
//      are permitted provided that the following conditions are met:
//
//              - Redistributions of source code must retain the above copyright notice,
//                      this list of conditions and the following disclaimer.
//              - Redistributions in binary form must reproduce the above copyright
//                      notice, this list of conditions and the following disclaimer
//                      in the documentation and/or other materials provided with the
//                      distribution.
//              - Neither the name of the University of California, Santa Cruz nor the
//                      names of its contributors may be used to endorse or promote
//                      products derived from this software without specific prior
//                      written permission.
//
//      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
//      ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//      WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//      DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
//      ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//      (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//      LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//      ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//      SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//==============================================================================

/****************************************************************************
    Description:

****************************************************************************/

`define FLOP_RETRY_USE_FLOPS 1
`define USE_SELF_W2R1 1

module fflop
  #(parameter Size=0)
    (input                     clk
     ,input                    reset

     ,input  logic [Size-1:0]  din
     ,input  logic             dinValid
     ,output logic             dinRetry

     ,output logic [Size-1:0]  q
     ,input  logic             qRetry
     ,output logic             qValid
     );

`ifdef USE_SELF_W2R1
  // uses W2R1 implementation from
  // http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.99.9778&rep=rep1&type=pdf

  // {{{1 SELF IMPLEMENTATION

  logic [Size-1:0] shadowq;
  logic c1;
  logic c2;
  logic shadowValid;

  logic          priv_qValid;
  always_comb begin
    qValid = priv_qValid;
  end

  logic          priv_dinRetry;
  always_comb begin
    dinRetry = priv_dinRetry;
  end

  // Inputs private signals
  logic          priv_qRetry;
  always_comb begin
    priv_qRetry = qRetry;
  end
  logic          priv_dinValid;
  always_comb begin
    priv_dinValid = dinValid;
  end
  // 1}}}

  // {{{1 cond1 and cond2
  always_comb begin
    c1 = (priv_qValid & priv_qRetry); // resend (even with failure, we can have a resend) 
    c2 = priv_dinValid | shadowValid; // pending
  end
  // 1}}}
  

  // {{{1 shadowValid
  always @(posedge clk) begin
    if (reset) begin
      shadowValid <= 'b0;
    end else begin
      shadowValid <= (c1 & c2);
    end 
  end 
  // 1}}}

  // {{{1 shadowq
  logic s_enable;
  always_comb begin
    s_enable = !shadowValid;
  end

  always@ (posedge clk) begin
    if (s_enable) begin
      shadowq <= din;
    end
  end 
  // 1}}}

   // {{{1 q

  always @(posedge clk) begin
    if (c1) begin
      q <= q;
    end else if (s_enable) begin
      q <= din;
    end else begin
      q <= shadowq;
    end
  end 
  // 1}}}

  // {{{1 priv_qValid (qValid internal value)
  logic priv_qValidla2;
  always @(posedge clk) begin
    if (reset) begin
      priv_qValidla2 <='b0;
    end else begin
      priv_qValidla2 <= (c1 | c2);
    end 
  end 

  always_comb begin
    priv_qValid = priv_qValidla2;
  end
  // 1}}}

  // {{{1 priv_dinRetry (dinRetry internal value)

  always_comb begin
   priv_dinRetry = shadowValid | reset;
  end
  // 1}}}

  // 1}}}

`else
  // {{{1 Private variable priv_*i, failure, and shadowq declaration
  // Output private signals
  logic [Size-1:0] shadowq;
  logic c1;
  logic c2;
  logic shadowValid;

  logic          priv_qValid;
  always @(*) begin
    qValid = priv_qValid;
  end
  logic          priv_dinRetry;
  always @(*) begin
    dinRetry = priv_dinRetry;
  end

  // Inputs private signals
  logic          priv_qRetry;
  always @(*) begin
    priv_qRetry = qRetry;
  end
  logic          priv_dinValid;
  always @(*) begin
    priv_dinValid = dinValid;
  end
  // 1}}}

  // {{{1 cond1 and cond2
  always @(*) begin
    c1 = (priv_qValid & priv_qRetry); // resend (even with failure, we can have a resend) 
    c2 = priv_dinValid | shadowValid; // pending
  end
  // 1}}}

  // {{{1 shadowValid
`ifdef FLOP_RETRY_USE_FLOPS
  always @(posedge clk) begin
    if (reset) begin
      shadowValid <= 'b0;
    end else begin
      shadowValid <= (c1 & c2);
    end 
  end 
`else 
  logic shadowValid_nclk;
  always_latch begin
    if (~clk) begin
      if (reset) begin
        shadowValid_nclk<= 'b0;
      end else begin
        shadowValid_nclk<= (c1 & c2); 
      end
    end
  end
  always_latch begin
    if (clk) begin
      shadowValid <= shadowValid_nclk;
    end
  end
`endif 
  // 1}}}

  // {{{1 shadowq
  logic s_enable;
  always @(*) begin
    s_enable = !shadowValid;
  end

  always@ (posedge clk) begin
    if (s_enable) begin
      shadowq <= din;
    end
  end 
  // 1}}}

   // {{{1 q
  logic q_enable;
`ifdef FLOP_RETRY_USE_FLOPS
  always @(negedge clk) begin
    q_enable <= !c1; 
  end
`else
  always @(*) begin
    if (!clk) begin
       q_enable <= !c1; 
    end
  end
`endif

  always @ (negedge clk) begin
      if (q_enable) begin
        q <= shadowq;
      end 
  end 
  // 1}}}

  // {{{1 priv_qValid (qValid internal value)
  logic priv_qValidla2;
`ifdef FLOP_RETRY_USE_FLOPS
  always @(posedge clk) begin
    if (reset) begin
      priv_qValidla2 <='b0;
    end else begin
      priv_qValidla2 <= (c1 | c2);
    end 
  end 

`else 
  logic priv_qValidla;
  always_latch begin
    if (~clk) begin
      if (reset) begin
        priv_qValidla    <= 'b0;
      end else begin
        priv_qValidla    <= (c1 | c2);
      end
    end
  end

  always_latch begin
    if (clk) begin
      priv_qValidla2 <= priv_qValidla;
    end
  end
`endif 

  always @(*) begin
    priv_qValid = priv_qValidla2;
  end
  // 1}}}

  // {{{1 priv_dinRetry (dinRetry internal value)

  always @(*) begin
   priv_dinRetry = shadowValid | reset;
  end
  // 1}}}

`endif

endmodule 

module join_fadd(
  input       clk,
  input       reset,
  input [7:0] inp_a,
  input       inp_aValid,
  output      inp_aRetry,

  input [7:0] inp_b,
  input       inp_bValid,
  output      inp_bRetry,

  output [7:0] sum,
  output       sumValid,
  input        sumRetry
);

  logic [7:0] sum_next;

  always_comb begin
    sum_next = inp_a + inp_b;
  end

  logic   inpValid;
  logic   inpRetry;

  always_comb begin
    inpValid = inp_aValid && inp_bValid;
  end

  always_comb begin
`ifdef LAZY_OPTION
    inp_bRetry = inpRetry || !inpValid;
    inp_aRetry = inpRetry || !inpValid;
`else
    inp_bRetry = inpRetry || (!inpValid && inp_bValid);
    inp_aRetry = inpRetry || (!inpValid && inp_aValid);

    //inp_bRetry = inpRetry || (!inpValid && inp_bValid) || (!inpValid && inp_aValid);
    //inp_aRetry = inpRetry || (!inpValid && (inp_bValid || inp_aValid));
`endif
  end

  logic [7:0] sum2;
  logic       sum2Valid;
  logic       sum2Retry;

  fflop #(.Size(8)) f1 (
    .clk      (clk),
    .reset    (reset),

    .din      (sum_next),
    .dinValid (inpValid),
    .dinRetry (inpRetry),

    .q        (sum2),
    .qValid   (sum2Valid),
    .qRetry   (sum2Retry)
  );

  fflop #(.Size(8)) f2 (
    .clk      (clk),
    .reset    (reset),

    .din      (sum2),
    .dinValid (sum2Valid),
    .dinRetry (sum2Retry),

    .q        (sum),
    .qValid   (sumValid),
    .qRetry   (sumRetry)
  );

endmodule

