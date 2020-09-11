/**************************************************************************
***    
*** Copyright (c) 2003 Regents of the University of Michigan,
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
#include "boundfixed.hpp"
#include "bound.hpp"
#include "parameters.hpp"

#include <float.h>
#include <algorithm>
using namespace std;

// ========================================================
bool fixed_BranchBoundProceed(const double minEdge[],
                              const bool same[][MAX_BLOCK_NUM],
                              const int blkBefore[],
                              const OwnQueue<int>& bCont,
                              const FloorPlan& best,
                              Dimension block[][ORIENT_NUM],
                              OTree& ot)
{
    if (ot.TOTAL_AREA + ot.deadspace >= best.area)
       return false;
    if (ot.width() * ot.height() >= best.area)
       return false;
    if (!LBCompact(ot)) 
       return false;
    if (!sameBlockBound(same, ot, blkBefore))
       return false;
    if (!sameEdgeBound(ot))
       return false;    
    if (!notRedundant(ot, bCont))
       return false;
    if (ot.TOTAL_AREA + ot.deadspace + fixed_extDspace(ot, bCont, block)
        >= best.area)
       return false;
    if (fixed_maxMinBound(block, minEdge, bCont, ot, best) >= best.area)
       return false;
    if (fixed_minMinBound(bCont, best, block, ot) >= best.area)
       return false;    
    return true;
}                                       
// --------------------------------------------------------
bool fixed_BacktrackProceed(const double minEdge[],
                            const bool same[][MAX_BLOCK_NUM],
                            const int blkBefore[],
                            const OwnQueue<int>& bCont,
                            const FloorPlan& best,
                            Dimension block[][ORIENT_NUM],
                            OTree& ot)
{
    if (max(ot.TOTAL_AREA + ot.deadspace, BT_MIN_TOTAL) >= best.area)
       return false;
    if (max(ot.width() * ot.height(), BT_MIN_TOTAL) >= best.area)
       return false;
    if (!LBCompact(ot)) 
       return false;
    if (!sameBlockBound(same, ot, blkBefore))
       return false;
    if (!sameEdgeBound(ot))
       return false;    
    if (!notRedundant(ot, bCont))
       return false;
    if (max(ot.TOTAL_AREA + ot.deadspace + fixed_extDspace(ot, bCont, block),
            BT_MIN_TOTAL) >= best.area)
       return false;
    if (max(fixed_maxMinBound(block, minEdge, bCont, ot, best), BT_MIN_TOTAL)
        >= best.area)
       return false;
    if (max(fixed_minMinBound(bCont, best, block, ot), BT_MIN_TOTAL)
        >= best.area)
       return false;    
    return true;
}                                       
// --------------------------------------------------------
bool fixed_EnumerateProceed(const double minEdge[],
                            const bool same[][MAX_BLOCK_NUM],
                            const int blkBefore[],
                            const OwnQueue<int>& bCont,
                            const FloorPlan& best,
                            Dimension block[][ORIENT_NUM],
                            OTree& ot)
{
    if (ot.TOTAL_AREA + ot.deadspace > best.area)
       return false;
    if (ot.width() * ot.height() > best.area)
       return false;
    if (!LBCompact(ot)) 
       return false;
    if (!sameBlockBound(same, ot, blkBefore))
       return false;
    if (!sameEdgeBound(ot))
       return false;    
    if (!notRedundant(ot, bCont))
       return false;
    if (ot.TOTAL_AREA + ot.deadspace + fixed_extDspace(ot, bCont, block)
        > best.area)
       return false;
    if (fixed_maxMinBound(block, minEdge, bCont, ot, best) > best.area)
       return false;
    if (fixed_minMinBound(bCont, best, block, ot) > best.area)
       return false;    
    return true;
}                                       
// ========================================================
double fixed_extDspace(const OTree& ot, 
                       const OwnQueue<int>& queue,
		       const Dimension block[][ORIENT_NUM])
{
   int size = queue.size();
   double minMinEdge = DBL_MAX;
   double edspace = 0;

   for (int i = 0; i < size; i++)
      if (block[queue[i]][0].width < minMinEdge)
         minMinEdge = block[queue[i]][0].width;

   int ptr = ot.contour[ot.BLOCK_NUM].next;
   while (ptr != ot.BLOCK_NUM+1)
   {
      double begin = ot.contour[ptr].begin;
      double end = ot.contour[ptr].end;
      double LCTL = ot.contour[ot.contour[ptr].prev].CTL;
      double RCTL = ot.contour[ot.contour[ptr].next].CTL;
      double CTL = min(LCTL, RCTL);
      
      if ((end-begin < minMinEdge) &&
          (CTL > ot.contour[ptr].CTL))
         edspace += (CTL-ot.contour[ptr].CTL) * (end-begin);
      ptr = ot.contour[ptr].next;
   }
   return edspace;
}
// --------------------------------------------------------
double fixed_minMinBound(const OwnQueue<int>& bCont,
                         const FloorPlan& best,
                         Dimension block[][ORIENT_NUM], // scratch board
                         OTree& ot)
{
   static const int FIRST_IND = ot.BLOCK_NUM+2;
   int qSize = bCont.size();
   int extbNum = ot.numZero - ot.perSize;
   double minWidth = DBL_MAX;
   double minHeight = DBL_MAX;
      
   if (extbNum < 1)
      return 0;   

   // find minWidth and minHeight
   for (int i = 0; i < qSize; i++)
   {
      if (block[bCont[i]][0].width < minWidth)      
	 minWidth = block[bCont[i]][0].width;

      if (block[bCont[i]][0].height < minHeight)
	 minHeight = block[bCont[i]][0].height;
   }

   // push extbNum minimum squares
   for (int j = 0; j < extbNum; j++)
   {
      block[FIRST_IND+j][0].width = minWidth;
      block[FIRST_IND+j][0].height = minHeight;

      ot.push_perm(FIRST_IND+j);
      ot.push_orient(0);
      ot.setpos_update(block, FIRST_IND+j);
   }
   
   double width = ot.width();
   double height = ot.height();
   double minArea = width * height;

   // restore ot
   for (int k = extbNum-1; k >= 0; k--)
   {
      ot.remove_block(FIRST_IND+k);
      ot.pop_orient();
      ot.pop_perm();
   }
   return minArea;
}
// --------------------------------------------------------
double fixed_maxMinBound(const Dimension block[][ORIENT_NUM],
			 const double minEdge[],
                         const OwnQueue<int>& bCont,
                         const OTree& ot,
                         const FloorPlan& best)
{
   double maxMinEdge = 0;
   double maxWidth = 0;
   double maxHeight = 0;
   double minArea = DBL_MAX;
   double otWidth = ot.width();
   double otHeight = ot.height();
   
   int qSize = bCont.size();

   if (qSize < 2)
      return 0;

   // find an appropriate block
   for (int i = 0; i < qSize; i++)
      if (minEdge[bCont[i]] > maxMinEdge)
      {
         maxMinEdge = minEdge[bCont[i]];
         maxWidth = block[bCont[i]][0].width;
         maxHeight = block[bCont[i]][0].height;
      }

   int currBlk = ot.contour[ot.BLOCK_NUM].next;
   int prevBlk = ot.BLOCK_NUM;
   while (currBlk != -1)
   {
      if (ot.contour[currBlk].CTL < ot.contour[prevBlk].CTL)
      {
         double begin = ot.contour[currBlk].begin;
         double end = begin + maxWidth;
         double maxY = 0;
         int ptr = currBlk;

	 // get y-coordinate of that block
         while (ot.contour[ptr].end <= end)
         {
            if (ot.contour[ptr].CTL > maxY)
               maxY = ot.contour[ptr].CTL;
            ptr = ot.contour[ptr].next;
         }

         if (ot.contour[ptr].begin < end)
            if (ot.contour[ptr].CTL > maxY)
               maxY = ot.contour[ptr].CTL;

         double height = max(maxY+maxHeight, otHeight);
         double width = max(end, otWidth);
      
	 minArea = min(minArea, height * width);
      }
      prevBlk = currBlk;
      currBlk = ot.contour[currBlk].next;
   }
   return minArea;
}
// ========================================================
