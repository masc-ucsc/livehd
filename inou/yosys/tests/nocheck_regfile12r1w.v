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

module nocheck_regfile12r1w
    (input                    clk

     ,input [7-1:0]           waddr0

     ,input                   we0

     ,input [64-1:0]          din0

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
     if (we0 && raddr0 == waddr0)
       q0_next = din0;
     else
       q0_next = rf[raddr0];
   end
   always @(*) begin
     if (we0 && raddr1 == waddr0)
       q1_next = din0;
     else
       q1_next = rf[raddr1];
   end
   always @(*) begin
     if (we0 && raddr2 == waddr0)
       q2_next = din0;
     else
       q2_next = rf[raddr2];
   end
   always @(*) begin
     if (we0 && raddr3 == waddr0)
       q3_next = din0;
     else
       q3_next = rf[raddr3];
   end
   always @(*) begin
     if (we0 && raddr4 == waddr0)
       q4_next = din0;
     else
       q4_next = rf[raddr4];
   end
   always @(*) begin
     if (we0 && raddr5 == waddr0)
       q5_next = din0;
     else
       q5_next = rf[raddr5];
   end
   always @(*) begin
     if (we0 && raddr6 == waddr0)
       q6_next = din0;
     else
       q6_next = rf[raddr6];
   end
   always @(*) begin
     if (we0 && raddr7 == waddr0)
       q7_next = din0;
     else
       q7_next = rf[raddr7];
   end
   always @(*) begin
     if (we0 && raddr8 == waddr0)
       q8_next = din0;
     else
       q8_next = rf[raddr8];
   end
   always @(*) begin
     if (we0 && raddr9 == waddr0)
       q9_next = din0;
     else
       q9_next = rf[raddr9];
   end
   always @(*) begin
     if (we0 && raddr10 == waddr0)
       q10_next = din0;
     else
       q10_next = rf[raddr10];
   end
   always @(*) begin
     if (we0 && raddr11 == waddr0)
       q11_next = din0;
     else
       q11_next = rf[raddr11];
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

