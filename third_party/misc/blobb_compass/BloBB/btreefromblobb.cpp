/**************************************************************************
***    
*** Copyright (c) 2004 Regents of the University of Michigan,
***               Hayward H. Chan and Igor L. Markov
***
***  Contact author(s): hhchan@umich.edu, imarkov@umich.edu
***  Original Affiliation:   EECS Department, 
***                          The University of Michigan,
***                          Ann Arbor, MI 48109-2122
***
***  Permission is hereby granted, free of charge, to any person obtaining 
***  a copy of this software and associated documentation files (the
***  "Software"), to deal in the Software without restriction, including
***  without limitation 
***  the rights to use, copy, modify, merge, publish, distribute, sublicense, 
***  and/or sell copies of the Software, and to permit persons to whom the 
***  Software is furnished to do so, subject to the following conditions:
***
***  The above copyright notice and this permission notice shall be included
***  in all copies or substantial portions of the Software.
***
*** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
*** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
*** OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
*** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
*** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
*** OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
*** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***
***
***************************************************************************/
#include "btreefromblobb.hpp"

#include "datastr.hpp"
#include "datastrfrontsoftst.hpp"

#include <vector>
using namespace std;

// --------------------------------------------------------
SoftPackingFromBloBB::SoftPackingFromBloBB(
   const vector< vector<Dimension> >& block,
   const Cluster& cluster,
   const FloorPlanVec& fp)
   : SoftPacking()
{
   xloc.resize(cluster.BLOCK_NUM);
   yloc.resize(cluster.BLOCK_NUM);
   width.resize(cluster.BLOCK_NUM);
   height.resize(cluster.BLOCK_NUM);
   expression.resize(cluster.expression.size());

   int orient_ptr = 0;
   for (unsigned int i = 0; i < expression.size(); i++)
   {
      int sign = cluster.expression[i];
      if (sign == cluster.PLUS)
         expression[i] = SoftSTree::PLUS;
      else if (sign == cluster.STAR)
         expression[i] = SoftSTree::STAR;
      else
      {
         int theta = cluster.orient[orient_ptr];
         expression[i] = sign;
         width[sign] = block[sign][theta].width;
         height[sign] = block[sign][theta].height;
         orient_ptr++;
      }
   }

   for (int i = 0; i < cluster.BLOCK_NUM; i++)
   {
      xloc[i] = fp.xLoc[i];
      yloc[i] = fp.yLoc[i];
   }

   deadspace = cluster.deadspace;
   blockArea = cluster.area;
   totalWidth = cluster.width;
   totalHeight = cluster.height;
}
// --------------------------------------------------------   
