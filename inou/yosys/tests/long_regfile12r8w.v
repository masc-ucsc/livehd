/****************************************************************************
   SCOORE: Santa Cruz Out-of-order Risk Engine
   Copyright (C) 2004 University of California, Santa Cruz.


This file is part of SCOORE.

SCOORE is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2, or (at your option) any later version.

SCOORE is distributed in the  hope that  it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public License along with
SCOORE; see the file COPYING.  If not, write to the  Free Software Foundation,
59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
WITH THE SOFTWARE.

****************************************************************************/

/****************************************************************************
    BUGS Found and/or Corrected:

****************************************************************************/

module regfile12r8w
    (input                    clk

     ,input [7-1:0]           waddr0
     ,input [7-1:0]           waddr1
     ,input [7-1:0]           waddr2
     ,input [7-1:0]           waddr3
     ,input [7-1:0]           waddr4
     ,input [7-1:0]           waddr5
     ,input [7-1:0]           waddr6
     ,input [7-1:0]           waddr7

     ,input                   we0
     ,input                   we1
     ,input                   we2
     ,input                   we3
     ,input                   we4
     ,input                   we5
     ,input                   we6
     ,input                   we7

     ,input [64-1:0]          din0
     ,input [64-1:0]          din1
     ,input [64-1:0]          din2
     ,input [64-1:0]          din3
     ,input [64-1:0]          din4
     ,input [64-1:0]          din5
     ,input [64-1:0]          din6
     ,input [64-1:0]          din7

     ,input [7-1:0]           raddr0
     ,input [7-1:0]           raddr1
     ,input [7-1:0]           raddr2
     ,input [7-1:0]           raddr3
     ,input [7-1:0]           raddr4
     ,input [7-1:0]           raddr5
     ,input [7-1:0]           raddr6
     ,input [7-1:0]           raddr7
     ,input [7-1:0]           raddr8
     ,input [7-1:0]           raddr9
     ,input [7-1:0]           raddr10
     ,input [7-1:0]           raddr11

     ,output [64-1:0]         q0
     ,output [64-1:0]         q1
     ,output [64-1:0]         q2
     ,output [64-1:0]         q3
     ,output [64-1:0]         q4
     ,output [64-1:0]         q5
     ,output [64-1:0]         q6
     ,output [64-1:0]         q7
     ,output [64-1:0]         q8
     ,output [64-1:0]         q9
     ,output [64-1:0]         q10
     ,output [64-1:0]         q11

     );

   reg [64-1:0]                      rf[128-1:0]; // synthesis syn_ramstyle = "block_ram"

   reg [64-1:0] q0_next;
   reg [64-1:0] q1_next;
   reg [64-1:0] q2_next;
   reg [64-1:0] q3_next;
   reg [64-1:0] q4_next;
   reg [64-1:0] q5_next;
   reg [64-1:0] q6_next;
   reg [64-1:0] q7_next;
   reg [64-1:0] q8_next;
   reg [64-1:0] q9_next;
   reg [64-1:0] q10_next;
   reg [64-1:0] q11_next;

   always @(*) begin
     casez({(we0 && raddr0 == waddr0) ,(we1 && raddr0 == waddr1) ,(we2 && raddr0 == waddr2) ,(we3 && raddr0 == waddr3),
            (we0 && raddr0 == waddr4) ,(we1 && raddr0 == waddr5) ,(we2 && raddr0 == waddr6) ,(we3 && raddr0 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q0_next = din0;
       8'b01??????: q0_next = din1;
       8'b001?????: q0_next = din2;
       8'b0001????: q0_next = din3;
       8'b0001????: q0_next = din4;
       8'b00001???: q0_next = din5;
       8'b000001??: q0_next = din7;
       8'b0000001?: q0_next = din7;
       8'b00000000: q0_next = rf[raddr0];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr1 == waddr0) ,(we1 && raddr1 == waddr1) ,(we2 && raddr1 == waddr2) ,(we3 && raddr1 == waddr3),
            (we0 && raddr1 == waddr4) ,(we1 && raddr1 == waddr5) ,(we2 && raddr1 == waddr6) ,(we3 && raddr1 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q1_next = din0;
       8'b01??????: q1_next = din1;
       8'b001?????: q1_next = din2;
       8'b0001????: q1_next = din3;
       8'b0001????: q1_next = din4;
       8'b00001???: q1_next = din5;
       8'b000001??: q1_next = din7;
       8'b0000001?: q1_next = din7;
       8'b00000000: q1_next = rf[raddr1];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr2 == waddr0) ,(we1 && raddr2 == waddr1) ,(we2 && raddr2 == waddr2) ,(we3 && raddr2 == waddr3),
            (we0 && raddr2 == waddr4) ,(we1 && raddr2 == waddr5) ,(we2 && raddr2 == waddr6) ,(we3 && raddr2 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q2_next = din0;
       8'b01??????: q2_next = din1;
       8'b001?????: q2_next = din2;
       8'b0001????: q2_next = din3;
       8'b0001????: q2_next = din4;
       8'b00001???: q2_next = din5;
       8'b000001??: q2_next = din7;
       8'b0000001?: q2_next = din7;
       8'b00000000: q2_next = rf[raddr2];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr3 == waddr0) ,(we1 && raddr3 == waddr1) ,(we2 && raddr3 == waddr2) ,(we3 && raddr3 == waddr3),
            (we0 && raddr3 == waddr4) ,(we1 && raddr3 == waddr5) ,(we2 && raddr3 == waddr6) ,(we3 && raddr3 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q3_next = din0;
       8'b01??????: q3_next = din1;
       8'b001?????: q3_next = din2;
       8'b0001????: q3_next = din3;
       8'b0001????: q3_next = din4;
       8'b00001???: q3_next = din5;
       8'b000001??: q3_next = din7;
       8'b0000001?: q3_next = din7;
       8'b00000000: q3_next = rf[raddr3];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr4 == waddr0) ,(we1 && raddr4 == waddr1) ,(we2 && raddr4 == waddr2) ,(we3 && raddr4 == waddr3),
            (we0 && raddr4 == waddr4) ,(we1 && raddr4 == waddr5) ,(we2 && raddr4 == waddr6) ,(we3 && raddr4 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q4_next = din0;
       8'b01??????: q4_next = din1;
       8'b001?????: q4_next = din2;
       8'b0001????: q4_next = din3;
       8'b0001????: q4_next = din4;
       8'b00001???: q4_next = din5;
       8'b000001??: q4_next = din7;
       8'b0000001?: q4_next = din7;
       8'b00000000: q4_next = rf[raddr4];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr5 == waddr0) ,(we1 && raddr5 == waddr1) ,(we2 && raddr5 == waddr2) ,(we3 && raddr5 == waddr3),
            (we0 && raddr5 == waddr4) ,(we1 && raddr5 == waddr5) ,(we2 && raddr5 == waddr6) ,(we3 && raddr5 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q5_next = din0;
       8'b01??????: q5_next = din1;
       8'b001?????: q5_next = din2;
       8'b0001????: q5_next = din3;
       8'b0001????: q5_next = din4;
       8'b00001???: q5_next = din5;
       8'b000001??: q5_next = din7;
       8'b0000001?: q5_next = din7;
       8'b00000000: q5_next = rf[raddr5];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr6 == waddr0) ,(we1 && raddr6 == waddr1) ,(we2 && raddr6 == waddr2) ,(we3 && raddr6 == waddr3),
            (we0 && raddr6 == waddr4) ,(we1 && raddr6 == waddr5) ,(we2 && raddr6 == waddr6) ,(we3 && raddr6 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q6_next = din0;
       8'b01??????: q6_next = din1;
       8'b001?????: q6_next = din2;
       8'b0001????: q6_next = din3;
       8'b0001????: q6_next = din4;
       8'b00001???: q6_next = din5;
       8'b000001??: q6_next = din7;
       8'b0000001?: q6_next = din7;
       8'b00000000: q6_next = rf[raddr6];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr7 == waddr0) ,(we1 && raddr7 == waddr1) ,(we2 && raddr7 == waddr2) ,(we3 && raddr7 == waddr3),
            (we0 && raddr7 == waddr4) ,(we1 && raddr7 == waddr5) ,(we2 && raddr7 == waddr6) ,(we3 && raddr7 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q7_next = din0;
       8'b01??????: q7_next = din1;
       8'b001?????: q7_next = din2;
       8'b0001????: q7_next = din3;
       8'b0001????: q7_next = din4;
       8'b00001???: q7_next = din5;
       8'b000001??: q7_next = din7;
       8'b0000001?: q7_next = din7;
       8'b00000000: q7_next = rf[raddr7];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr8 == waddr0) ,(we1 && raddr8 == waddr1) ,(we2 && raddr8 == waddr2) ,(we3 && raddr8 == waddr3),
            (we0 && raddr8 == waddr4) ,(we1 && raddr8 == waddr5) ,(we2 && raddr8 == waddr6) ,(we3 && raddr8 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q8_next = din0;
       8'b01??????: q8_next = din1;
       8'b001?????: q8_next = din2;
       8'b0001????: q8_next = din3;
       8'b0001????: q8_next = din4;
       8'b00001???: q8_next = din5;
       8'b000001??: q8_next = din7;
       8'b0000001?: q8_next = din7;
       8'b00000000: q8_next = rf[raddr8];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr9 == waddr0) ,(we1 && raddr9 == waddr1) ,(we2 && raddr9 == waddr2) ,(we3 && raddr9 == waddr3),
            (we0 && raddr9 == waddr4) ,(we1 && raddr9 == waddr5) ,(we2 && raddr9 == waddr6) ,(we3 && raddr9 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q9_next = din0;
       8'b01??????: q9_next = din1;
       8'b001?????: q9_next = din2;
       8'b0001????: q9_next = din3;
       8'b0001????: q9_next = din4;
       8'b00001???: q9_next = din5;
       8'b000001??: q9_next = din7;
       8'b0000001?: q9_next = din7;
       8'b00000000: q9_next = rf[raddr9];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr10 == waddr0) ,(we1 && raddr10 == waddr1) ,(we2 && raddr10 == waddr2) ,(we3 && raddr10 == waddr3),
            (we0 && raddr10 == waddr4) ,(we1 && raddr10 == waddr5) ,(we2 && raddr10 == waddr6) ,(we3 && raddr10 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q10_next = din0;
       8'b01??????: q10_next = din1;
       8'b001?????: q10_next = din2;
       8'b0001????: q10_next = din3;
       8'b0001????: q10_next = din4;
       8'b00001???: q10_next = din5;
       8'b000001??: q10_next = din7;
       8'b0000001?: q10_next = din7;
       8'b00000000: q10_next = rf[raddr10];
     endcase
   end
   always @(*) begin
     casez({(we0 && raddr11 == waddr0) ,(we1 && raddr11 == waddr1) ,(we2 && raddr11 == waddr2) ,(we3 && raddr11 == waddr3),
            (we0 && raddr11 == waddr4) ,(we1 && raddr11 == waddr5) ,(we2 && raddr11 == waddr6) ,(we3 && raddr11 == waddr7)}) // Synopsys full_case parallel_case
       8'b1???????: q11_next = din0;
       8'b01??????: q11_next = din1;
       8'b001?????: q11_next = din2;
       8'b0001????: q11_next = din3;
       8'b0001????: q11_next = din4;
       8'b00001???: q11_next = din5;
       8'b000001??: q11_next = din7;
       8'b0000001?: q11_next = din7;
       8'b00000000: q11_next = rf[raddr11];
     endcase
   end

   always @(posedge clk) begin
     q0 <= q0_next;
     q1 <= q1_next;
     q2 <= q2_next;
     q3 <= q3_next;
     q4 <= q4_next;
     q5 <= q5_next;
     q6 <= q6_next;
     q7 <= q7_next;
     q8 <= q8_next;
     q9 <= q9_next;
     q10 <= q10_next;
     q11 <= q11_next;
   end

   always @(posedge clk) begin
     if (we0) begin
       rf[waddr0] <= din0;
     end
   end

endmodule

