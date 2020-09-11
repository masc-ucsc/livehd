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
#ifndef ENGINEST_H
#define ENGINEST_H

#include "datastrst.hpp"
#include "boundst.hpp"
#include "stackqueue.hpp"

#include <vector>
#include <queue>
using namespace std;

#define STATISTICS

typedef bool (*OperandBoundType)(const bool[][MAX_BLOCK_NUM], const int[],
				 const STree&, const SliceRecord&);
typedef bool (*OperatorBoundType)(const STree&, const SliceRecord&);

// ========================================================
void FindSlice(const OperandBoundType operandBound,
	       const OperatorBoundType operatorBound,
	       const OperatorBoundType compare,
	       const Dimension block[][ORIENT_NUM],
               const bool same[][MAX_BLOCK_NUM],
               const int blkBefore[],
               STree& st,
               OwnQueue<int>& bContainer,
               SliceRecord& best);
// ========================================================
bool BranchBoundOperandBound(const bool same[][MAX_BLOCK_NUM],
			     const int blkBefore[],
			     const STree& st,
			     const SliceRecord& best);
bool BacktrackOperandBound(const bool same[][MAX_BLOCK_NUM],
			   const int blkBefore[],
			   const STree& st,
			   const SliceRecord& best);
bool EnumerateOperandBound(const bool same[][MAX_BLOCK_NUM],
			   const int blkBefore[],
			   const STree& st,
			   const SliceRecord& best);
// ========================================================
bool BranchBoundOperatorBound(const STree& st,
			      const SliceRecord& best);
bool BacktrackOperatorBound(const STree& st,
			    const SliceRecord& best);
bool EnumerateOperatorBound(const STree& st,
			    const SliceRecord& best);
// ========================================================
bool BranchBoundCompare(const STree& st,
			const SliceRecord& best);
bool BacktrackCompare(const STree& st,
		      const SliceRecord& best);
bool EnumerateCompare(const STree& st,
		      const SliceRecord& best);
// ========================================================

#endif // end ENGINEST_H
