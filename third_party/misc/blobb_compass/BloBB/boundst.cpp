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
#include "boundst.hpp"

#include <algorithm>
using namespace std;

double extDeadspace(const STree& st)
{
   int bSize = st.buffer.size();
   if (bSize < 2)
      return 0;

   double deadspace = 0;
   double maxWidth = st.buffer.top().width;
   double maxHeight = st.buffer.top().height;

   for (int i = bSize-2; i >= 0; i--)
   {
      Node currRect = st.buffer[i];
      double currWidth = currRect.width;
      double currHeight = currRect.height;

      if ((currWidth >= maxWidth) ||
          (currHeight >= maxHeight))
      {
         if (currWidth > maxWidth)
            maxWidth = currWidth;

         if (currHeight > maxHeight)
            maxHeight = currHeight;
      }
      else
      {
         deadspace += min(currWidth * (maxHeight - currHeight),
                          (maxWidth - currWidth) * currHeight);
      }
   }
   return deadspace;
}
// --------------------------------------------------------
bool blockSym(const STree& st)
{
   int sSize = st.buffer.size();
   if (sSize < 2)
      return true;

   int LBBlock = st.buffer[sSize-2].BLBlock;
   int RTBlock = st.buffer[sSize-1].BLBlock;

   return (LBBlock < RTBlock);
}
// --------------------------------------------------------
bool abutSym(const STree& st)
{
   int sign = st.expression.top();

   if (sign == st.storage.top().sign)
   {
      int sSize = st.storage.size();
      int BLcluster = st.storage[sSize-1].TRblblock;
      int TRcluster = st.storage[sSize-2].BLBlock;

      return (BLcluster < TRcluster);
   }
   else
      return true;
}
// --------------------------------------------------------
bool sameBlockBound(const bool same[][MAX_BLOCK_NUM],
                    const int blkBefore[],
                    const STree& st)
{
   int rect = st.expression.top();
   int rectpos = st.expression.size()-1;
   int blkCount = blkBefore[rect];

   for (int i = 0; i < rectpos; i++)
      if (blkCount == 0)
         return true;
      else if ((st.expression[i] < st.BLOCK_NUM) &&
               same[rect][st.expression[i]])
         blkCount--;         
   return (blkCount == 0);   
}
// --------------------------------------------------------
void InitializeBound(const Dimension block[][ORIENT_NUM],
                     const STree& st,
                     bool same[][MAX_BLOCK_NUM],
                     int blkBefore[])
{
   for (int t = 0; t < st.BLOCK_NUM; t++)
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
}
// --------------------------------------------------------
