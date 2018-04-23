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

module regfile2r1w
    (input                    clk

     ,input [7-1:0]           waddr0

     ,input                   we0

     ,input [64-1:0]          din0

     ,input [7-1:0]           raddr0
     ,input [7-1:0]           raddr1

     ,output [64-1:0]         q0
     ,output [64-1:0]         q1

     );

   reg [64-1:0]                      rf[128-1:0]; // synthesis syn_ramstyle = "block_ram"

   reg [64-1:0] q0_next;
   reg [64-1:0] q1_next;
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

   always @(posedge clk) begin
     q0 <= q0_next;
     q1 <= q1_next;
   end

   always @(posedge clk) begin
     if (we0) begin
       rf[waddr0] <= din0;
     end
   end

endmodule

