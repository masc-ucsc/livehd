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
#include "bound.hpp"
#include "parameters.hpp"

#include <float.h>
#include <algorithm>
using namespace std;

// ========================================================
bool BranchBoundCompare(const double area,
                        const double bestArea)
{
   return area < bestArea;
}
// --------------------------------------------------------
bool BacktrackCompare(const double area,
                      const double bestArea)
{
   return area < bestArea;
}
// --------------------------------------------------------
bool EnumerateCompare(const double area,
                      const double bestArea)
{
   return area <= bestArea;
}
// ========================================================
bool BranchBoundProceed(const double minEdge[],
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
    if (ot.TOTAL_AREA + ot.deadspace + extDspace(ot, bCont, minEdge)
        >= best.area)
       return false;
    if (ot.orient[0] != 0)
       return false;
    if (maxMinBound(block, minEdge, bCont, ot, best) >= best.area)
       return false;
    if (minMinBound(minEdge, bCont, best, block, ot) >= best.area)
       return false;    
    return true;
}                                       
// --------------------------------------------------------
bool BacktrackProceed(const double minEdge[],
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
    if (max(ot.TOTAL_AREA + ot.deadspace + extDspace(ot, bCont, minEdge),
            BT_MIN_TOTAL) >= best.area)
       return false;
    if (ot.orient[0] != 0)
       return false;
    if (max(maxMinBound(block, minEdge, bCont, ot, best), BT_MIN_TOTAL)
        >= best.area)
       return false;
    if (max(minMinBound(minEdge, bCont, best, block, ot), BT_MIN_TOTAL)
        >= best.area)
       return false;    
    return true;
}                                       
// --------------------------------------------------------
bool EnumerateProceed(const double minEdge[],
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
    if (ot.TOTAL_AREA + ot.deadspace + extDspace(ot, bCont, minEdge)
        > best.area)
       return false;
    if (ot.orient[0] != 0)
       return false;
    if (maxMinBound(block, minEdge, bCont, ot, best) > best.area)
       return false;
    if (minMinBound(minEdge, bCont, best, block, ot) > best.area)
       return false;    
    return true;
}                                       
// ========================================================
double extDspace(const OTree& ot, 
                 const OwnQueue<int>& queue,
                 const double minEdge[])
{
   int size = queue.size();
   double minMinEdge = DBL_MAX;
   double edspace = 0;

   for (int i = 0; i < size; i++)
      if (minEdge[queue[i]] < minMinEdge)
         minMinEdge = minEdge[queue[i]];
      
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
double minMinBound(const double minEdge[],
                   const OwnQueue<int>& bCont,
                   const FloorPlan& best,
                   Dimension block[][ORIENT_NUM],
                   OTree& ot)
{
   static const int FIRST_IND = ot.BLOCK_NUM+2;
   int qSize = bCont.size();
   int extbNum = ot.numZero - ot.perSize;
   double minMinEdge = DBL_MAX;
   
   if (extbNum < 1)
      return 0;   
   
   for (int i = 0; i < qSize; i++)
      if (minEdge[bCont[i]] < minMinEdge)      
         minMinEdge = minEdge[bCont[i]];              

   for (int j = 0; j < extbNum; j++)
   {
      block[FIRST_IND+j][0].width = minMinEdge;
      block[FIRST_IND+j][0].height = minMinEdge;

      ot.push_perm(FIRST_IND+j);
      ot.push_orient(0);
      ot.setpos_update(block, FIRST_IND+j);
   }
   
   double width = ot.width();
   double height = ot.height();
   double minArea = width * height;
   
   for (int k = extbNum-1; k >= 0; k--)
   {
      ot.remove_block(FIRST_IND+k);
      ot.pop_orient();
      ot.pop_perm();
   }

   return minArea;
}
// --------------------------------------------------------
double maxMinBound(const Dimension block[][ORIENT_NUM],
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
      
         if (height*width < minArea)
            minArea = height * width;
      }
      prevBlk = currBlk;
      currBlk = ot.contour[currBlk].next;
   }

   prevBlk = ot.BLOCK_NUM;
   currBlk = ot.contour[ot.BLOCK_NUM].next;
   while (currBlk != -1)
   {
      if (ot.contour[currBlk].CTL < ot.contour[prevBlk].CTL)
      {
         double begin = ot.contour[currBlk].begin;
         double end = begin + maxHeight;
         double maxY = 0;
         int ptr = currBlk;
         while (ot.contour[ptr].end <= end)
         {
            if (ot.contour[ptr].CTL > maxY)
               maxY = ot.contour[ptr].CTL;
            ptr = ot.contour[ptr].next;
         }

         if (ot.contour[ptr].begin < end)
            if (ot.contour[ptr].CTL > maxY)
               maxY = ot.contour[ptr].CTL;

         double height = max(maxY+maxWidth, otHeight);
         double width = max(end, otWidth);
      
         if (height*width < minArea)
            minArea = height * width;
      }
      prevBlk = currBlk;
      currBlk = ot.contour[currBlk].next;
   }

   return minArea;
}
// --------------------------------------------------------
bool LBCompact(const OTree& ot)
{
   int rect = ot.perm.top();
   int Lrect = ot.prev[rect];

   return (ot.yloc[rect] < ot.contour[Lrect].CTL) &&
          (ot.contour[rect].CTL > ot.yloc[Lrect]);
}
// --------------------------------------------------------
bool notRedundant(const OTree& ot,
                  const OwnQueue<int>& bCont)
{
   int rect = ot.perm.top();
   int LRect = ot.prev[rect];
   
   if (ot.contour[LRect].blockBelow.size() != 0)
   {
      int BRect = ot.contour[LRect].blockBelow.back();
      if ((ot.contour[BRect].end == ot.xloc[rect]) &&
          (ot.contour[BRect].CTL > ot.yloc[rect]))
         return false;
   }

   int ptr = ot.contour[ot.BLOCK_NUM+1].prev;
   while (ot.contour[ptr].begin > ot.xloc[ptr])
      ptr = ot.contour[ptr].prev;

   int TRRect = ptr;
   int TLRect = ot.contour[ot.BLOCK_NUM].next;
   int BRRect;
   int BLRect = ot.perm[0];

   ptr = ot.contour[ot.BLOCK_NUM+1].prev;
   while (ot.yloc[ptr] != 0)
      ptr = ot.contour[ptr].blockBelow.back();
   BRRect = ptr;

   if (flatBottom(ot))
      if (!FlatBottomBound(ot))
         return false;

   if (flatTop(ot))
      return (FlatTopBound(ot, BLRect, BRRect, TLRect, TRRect));

   int count = 0; 
   if (BLRect > BRRect)
      count++;

   if (BLRect > TLRect)
      count++;

   if ((count == 0) && BLRect <= TRRect)
      return true;

   if (count < 2)
      return (BLRect < maxOneAbsent(bCont));
   else
      return (BLRect < maxTwoAbsent(bCont));   
}
// --------------------------------------------------------
int maxOneAbsent(const OwnQueue<int>& queue)
{
   int maxAbs = 0;
   int size = queue.size();

   if (size == 0)
      return -1;

   for (int i = 0; i < size; i++)
      if (queue[i] > maxAbs)
         maxAbs = queue[i];

   return maxAbs;
}
// --------------------------------------------------------
int maxTwoAbsent(const OwnQueue<int>& queue)
{
   int size = queue.size();
   int maxOne = 0;
   int maxTwo = 0;

   if (size == 0)
      return -1;

   for (int i = 0; i < size; i++)
      if (queue[i] > maxOne)
      {
         maxOne = queue[i];
         maxTwo = maxOne;
      }
      else if (queue[i] > maxTwo)
         maxTwo = queue[i];
   return maxTwo;
}
// --------------------------------------------------------
bool FlatBottomBound(const OTree& ot)
{
   int rect = ot.perm.top();
   int blrect = ot.perm[0];
   int brrect, tlrect, trrect;

   if (ot.contour[rect].blockBelow.size() == 0)
      return true;

   int ptr = ot.contour[rect].blockBelow.back();
   while (ot.contour[ptr].begin != ot.xloc[ptr])
      ptr = ot.contour[ptr].prev;
   trrect = ptr;

   while (ot.contour[ptr].prev != ot.BLOCK_NUM)
      ptr = ot.contour[ptr].prev;
   tlrect = ptr;

   ptr = ot.contour[ot.BLOCK_NUM+1].prev;
   while (ot.yloc[ptr] != 0)
      ptr = ot.contour[ptr].blockBelow.back();
   brrect = ptr;

   return ((blrect <= brrect) && (blrect <= tlrect) && 
           (blrect <= trrect));
}
// --------------------------------------------------------
bool flatBottom(const OTree& ot)
{
   int ptr = ot.contour[ot.BLOCK_NUM].next;
   double yloc = ot.yloc[ptr];

   while (ptr != ot.BLOCK_NUM+1)
   {
      if (ot.yloc[ptr] != yloc)
         return false;
      ptr = ot.contour[ptr].next;
   }
   return true;
}
// --------------------------------------------------------
bool FlatTopBound(const OTree& ot,
                  int BLRect,
                  int BRRect,
                  int TLRect,
                  int TRRect)
{
   return ((BLRect <= TLRect) && (BLRect <= TRRect) &&
           (BLRect <= BRRect));
}
// --------------------------------------------------------
bool flatTop(const OTree& ot)
{
   int ptr = ot.contour[ot.BLOCK_NUM].next;
   double CTL = ot.contour[ptr].CTL;

   while (ptr != ot.BLOCK_NUM+1)
   {
      if (ot.contour[ptr].CTL != CTL)
         return false;
      ptr = ot.contour[ptr].next;
   }
   return true;
}
// --------------------------------------------------------
bool sameEdgeBound(const OTree& ot)
{
   int rect = ot.perm.top();
   int Lrect = ot.prev[rect];
   if ((ot.yloc[Lrect] == ot.yloc[rect]) &&
       (ot.contour[Lrect].CTL == ot.contour[rect].CTL) &&
       (Lrect > rect))
       return false;

   if (ot.contour[rect].blockBelow.size() == 0)
      return true;

   int Brect = ot.contour[rect].blockBelow.back();      
   if ((ot.xloc[Brect] == ot.xloc[rect]) &&
       (ot.contour[Brect].end == ot.contour[rect].end) &&
       (Brect > rect))
       return false;
   return true;
}
// --------------------------------------------------------
bool sameBlockBound(const bool same[][MAX_BLOCK_NUM],
                    const OTree& ot,
                    const int blkBefore[])
{
   int rect = ot.perm.top();
   int pSize = ot.permpos[rect];
   int blkCount = blkBefore[rect];

   for (int i = 0; i < pSize; i++)
      if (blkCount == 0)
         return true;
      else if (same[rect][ot.perm[i]])
         blkCount--;         
   return (blkCount == 0);   
}
// --------------------------------------------------------
void InitializeBound(const Dimension block[][ORIENT_NUM],
                     const OTree& ot,
                     double minEdge[],
                     bool same[][MAX_BLOCK_NUM],
                     int blkBefore[])
{
   for (int q = 0; q < ot.BLOCK_NUM; q++)
      minEdge[q] = min(block[q][0].width, block[q][0].height);      

   for (int t = 0; t < ot.BLOCK_NUM; t++)
   {
      blkBefore[t] = 0;
      for (int s = 0; s < t; s++)
      {
         if ((block[s][0].width == block[t][0].width) &&
             (block[s][0].height == block[t][0].height))
         {
            same[s][t] = true;
            same[t][s] = true;
            blkBefore[t]++;
         }
         else if ((block[s][1].width == block[t][0].width) &&
                  (block[s][1].height == block[t][0].height))
         {
            same[s][t] = true;
            same[t][s] = true;
            blkBefore[t]++;
         }
         else
         {
            same[s][t] = false;
            same[t][s] = false;
         }
      }
      same[t][t] = true;
   }

   for (int t = 0; t < ot.BLOCK_NUM; t++)
   {
      same[ot.BLOCK_NUM][t] = false;
      for (int s = 0; s < t; s++)
         same[ot.BLOCK_NUM][t] = same[s][t] || same[ot.BLOCK_NUM][t];

      for (int s = t+1; s < ot.BLOCK_NUM; s++)
         same[ot.BLOCK_NUM][t] = same[s][t] || same[ot.BLOCK_NUM][t];
   }
}
// --------------------------------------------------------
