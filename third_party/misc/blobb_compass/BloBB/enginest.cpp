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
#include "enginest.hpp"
#include "stackqueue.hpp"
#include "datastrst.hpp"
#include "boundst.hpp"

#include "parameters.hpp"
#include "utilities.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <queue>
#include <algorithm>
#include <float.h>
using namespace std;

// ========================================================
void FindSlice(const OperandBoundType operandBound,
               const OperatorBoundType operatorBound,
               const OperatorBoundType compare,
               const Dimension block[][ORIENT_NUM],
               const bool same[][MAX_BLOCK_NUM],
               const int blkBefore[],
               STree& st,
               OwnQueue<int>& bContainer,
               SliceRecord& best)
{
   static const int PLUS = st.BLOCK_NUM;
   static const int STAR = st.BLOCK_NUM + 1;

   int qSize = bContainer.size();
   if ((qSize == 0) && (st.balance == 1))
   {
      if (compare(st, best))
      {
	 if (st.deadspace < best.deadspace)
	    counter[0][1] = counter[0][0];
	 
         best = st;
	 if (INF_FN_PREFIX != "")
	 {
	    ofstream outs;
	    stringstream filename;
	    FloorPlan fp;
	    filename << INF_FN_PREFIX << counter[0][0] << INF_FN_SUFFIX;
	    
	    outs.open(filename.str().c_str());
	    Evaluate(block, best, fp);
	    outputfp(block, fp, outs);
	    outs.close();
	 }
	 
	 if (INF_SHOW_INTERMEDIATES)
	 {
	    cout.setf(ios::fixed);
	    cout.precision(2);
	    cout << "[" << counter[0][0] << "] "  
		 << "area: " << setw(15) << best.area << " (" 
		 << ((best.deadspace / st.TOTAL_AREA) * 100) << "%) "
		 << setw(10) << getTotalTime() << "s" << endl;
	 }
	 counter[0][0]++;
      }
      return;
   }

   for (int i = 0; i < qSize; i++)
   {
      int rect = bContainer.dequeue();
      for (int k = 0; k < ENG_ORIENT_CONSIDERED; k++)
      {
         st.push_orient(k);
         st.push_operand(rect, block);

         counter[st.expression.size()][0]++;
	 counter[st.expression.size()][1]++;
         if (operandBound(same, blkBefore, st, best))
         {
            counter[st.expression.size()][2]++;
            FindSlice(operandBound, operatorBound, compare,
                      block, same, blkBefore, 
                      st, bContainer, best); 
         }
         st.pop_operand();
         st.pop_orient();
      }
      bContainer.enqueue(rect);
   }

   for (int j = PLUS; j <= STAR; j++)
      if (st.can_push_operator(j))
      {
         st.push_operator(j);

	 counter[st.expression.size()][0]++;
         counter[st.expression.size()][3]++;
         if (operatorBound(st, best))
         {
            counter[st.expression.size()][4]++;
            FindSlice(operandBound, operatorBound, compare,
                      block, same, blkBefore, 
                      st, bContainer, best); 
         }
         st.pop_operator();
      }
}
// ========================================================
bool BranchBoundOperandBound(const bool same[][MAX_BLOCK_NUM],
                             const int blkBefore[],
                             const STree& st,
                             const SliceRecord& best)
{
   if (!blockSym(st))
      return false;
   if (!sameBlockBound(same, blkBefore, st))
      return false;
   if (st.deadspace + extDeadspace(st) >= best.deadspace)
      return false;
   return true;
}
// --------------------------------------------------------
bool BacktrackOperandBound(const bool same[][MAX_BLOCK_NUM],
                           const int blkBefore[],
                           const STree& st,
                           const SliceRecord& best)
{
   if (!blockSym(st))
      return false;
   if (!sameBlockBound(same, blkBefore, st))
      return false;
   if (max(st.deadspace + extDeadspace(st), BT_MIN_DEADSPACE) >= best.deadspace)
      return false;
   return true;
}
// --------------------------------------------------------
bool EnumerateOperandBound(const bool same[][MAX_BLOCK_NUM],
                           const int blkBefore[],
                           const STree& st,
                           const SliceRecord& best)
{
   if (!blockSym(st))
      return false;
   if (!sameBlockBound(same, blkBefore, st))
      return false;
   if (st.deadspace + extDeadspace(st) > best.deadspace)
      return false;
   return true;
}
// ========================================================
bool BranchBoundOperatorBound(const STree& st,
                              const SliceRecord& best)
{
   if (st.deadspace >= best.deadspace)
      return false;
   if (!abutSym(st))
      return false;
   if (st.deadspace + extDeadspace(st) >= best.deadspace)
      return false;
   return true;
}
// --------------------------------------------------------
bool BacktrackOperatorBound(const STree& st,
                            const SliceRecord& best)
{
   if (max(st.deadspace, BT_MIN_DEADSPACE) >= best.deadspace)
      return false;
   if (!abutSym(st))
      return false;
   if (max(st.deadspace + extDeadspace(st), BT_MIN_DEADSPACE)
       >= best.deadspace)
      return false;
   return true;
}
// --------------------------------------------------------
bool EnumerateOperatorBound(const STree& st,
                            const SliceRecord& best)
{
   if (st.deadspace > best.deadspace)
      return false;
   if (!abutSym(st))
      return false;
   if (st.deadspace + extDeadspace(st) > best.deadspace)
      return false;
   return true;
}
// ========================================================
bool BranchBoundCompare(const STree& st,
                        const SliceRecord& best)
{
   return (st.deadspace < best.deadspace);
}
// --------------------------------------------------------
bool BacktrackCompare(const STree& st,
                      const SliceRecord& best)
{
   return (st.deadspace < best.deadspace);
}
// --------------------------------------------------------
bool EnumerateCompare(const STree& st,
                      const SliceRecord& best)
{
   return (st.deadspace <= best.deadspace);
}
// ========================================================
