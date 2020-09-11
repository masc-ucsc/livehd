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
#ifndef BOUNDFIXED_H
#define BOUNDFIXED_H

// optimized version for free orientation non-slicing block-packing
// extra package in addition to bound.h

#include "datastr.hpp"
#include "stackqueue.hpp"

// ========================================================
bool fixed_BranchBoundProceed(const double minEdge[],
			      const bool same[][MAX_BLOCK_NUM],
			      const int blkBefore[],
			      const OwnQueue<int>& bCont,
			      const FloorPlan& best,
			      Dimension block[][ORIENT_NUM],
			      OTree& ot);
// --------------------------------------------------------
bool fixed_BacktrackProceed(const double minEdge[],
			    const bool same[][MAX_BLOCK_NUM],
			    const int blkBefore[],
			    const OwnQueue<int>& bCont,
			    const FloorPlan& best,
			    Dimension block[][ORIENT_NUM],
			    OTree& ot);
// --------------------------------------------------------
bool fixed_EnumerateProceed(const double minEdge[],
			    const bool same[][MAX_BLOCK_NUM],
			    const int blkBefore[],
			    const OwnQueue<int>& bCont,
			    const FloorPlan& best,
			    Dimension block[][ORIENT_NUM],
			    OTree& ot);
// ========================================================
double fixed_extDspace(const OTree& ot, 
		       const OwnQueue<int>& queue,
		       const Dimension block[][ORIENT_NUM]);

double fixed_minMinBound(const OwnQueue<int>& bCont,
			 const FloorPlan& best, 
			 Dimension block[][ORIENT_NUM],
			 OTree& ot);

double fixed_maxMinBound(const Dimension block[][ORIENT_NUM],
			 const double minEdge[],
			 const OwnQueue<int>& bCont, 
			 const OTree& ot, 
			 const FloorPlan& best);
// ========================================================

#endif
