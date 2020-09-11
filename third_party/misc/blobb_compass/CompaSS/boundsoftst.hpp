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
#ifndef BOUNDSOFTST_H
#define BOUNDSOFTST_H

#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"
#include "utilities.hpp"

const double DEFAULT_OPTIMAL_ACCURACY = 1.0001;
const double DEFAULT_HIER_TXT_ACCURACY = 1.005;
const double DEFAULT_HIER_SOFT_ACCURACY = 1.02;

extern double OPTIMAL_ACCURACY;
extern double HIER_ACCURACY;

typedef bool (*SoftCompareType)(const SoftSTree& sst,
                                const SoftSliceRecord& best);
typedef bool (*SoftOperandProceed)(const vector< vector<bool> >& same,
                                   const vector<int>& blkBefore,
                                   const SoftSTree& sst,
                                   const SoftSliceRecord& best);
typedef bool (*SoftOperatorProceed)(const SoftSTree& sst,
                                    const SoftSliceRecord& best);

// ========================================================
// Lower bounds and symmetry-breaking functions (back-end)
// ========================================================
inline bool buffer_sym(const SoftSTree& sst)
{
   int bSize = sst.buffer_size();
   return ((bSize < 2) ||
           (sst.buffer(bSize-2).BLBlock < sst.buffer(bSize-1).BLBlock));
}
// --------------------------------------------------------
inline bool abut_sym(const SoftSTree& sst)
{
   int sSize = sst.storage_size();
   return ((sst.buffer_top().sign != sst.storage_top().sign) ||
           (sst.storage(sSize-1).TRblblock < sst.storage(sSize-2).BLBlock));
}
// --------------------------------------------------------
inline bool sameBlockProceed(const vector< vector<bool> >& same,
                             const vector<int>& blkBefore,
                             const SoftSTree& sst)
{
   int rect = sst.expression_top();
   int rect_pos = sst.expression_size() - 1;
   int blkCount = blkBefore[rect];

   for (int i = 0; i < rect_pos; i++)
   {
      int sign = sst.expression(i);
      if (blkCount == 0)
         return true;
      else if (sign >= 0 &&
               same[rect][sign])
         blkCount--;
   }
   return (blkCount == 0);
}
// --------------------------------------------------------
double extDeadspace(const SoftSTree& sst);
// --------------------------------------------------------
// ========================================================
// Interface functions used by the engines (front-end)
// ========================================================
inline bool BranchBoundCompare(const SoftSTree& sst,
                               const SoftSliceRecord& best)
{  return sst.deadspace() < best.minDeadspace; }
// --------------------------------------------------------
inline bool BruteProceed(const vector< vector<bool> >& same,
                         const vector<int>& blkBefore,
                         const SoftSTree& sst,
                         const SoftSliceRecord& best)
{   return true; }
// --------------------------------------------------------
inline bool BruteProceed(const SoftSTree& sst,
                         const SoftSliceRecord& best)
{   return true; }
// --------------------------------------------------------
inline bool BranchBoundProceed(const vector< vector<bool> >& same,
                               const vector<int>& blkBefore,
                               const SoftSTree& sst,
                               const SoftSliceRecord& best)
{
   if (!buffer_sym(sst))
      return false;   
   if (!sameBlockProceed(same, blkBefore, sst))
      return false;
   counter[sst.expression_size()][2]++;

   return true;
}
// --------------------------------------------------------
inline bool BranchBoundExplicitProceed(const SoftSTree& sst,
                                       const SoftSliceRecord& best)
{
   if (sst.total_area() * OPTIMAL_ACCURACY >= best.minArea)
      return false;

   if (!abut_sym(sst))
      return false;

   if (sst.total_area() + extDeadspace(sst) >= best.minArea)
      return false;
   counter[sst.expression_size()][4]++;

   return true;
}
// --------------------------------------------------------
inline bool BranchBoundImplicitProceed(const SoftSTree& sst,
                                       const SoftSliceRecord& best)
{
   if (sst.total_area() * OPTIMAL_ACCURACY >= best.minArea)
      return false;
   counter[sst.expression_size()][5]++;

   if (sst.total_area() + extDeadspace(sst) >= best.minArea)
      return false;
   counter[sst.expression_size()][6]++;

   return true;
}                               
// --------------------------------------------------------
inline bool BranchBoundHierarchicalProceed(const SoftSTree& sst,
                                           const SoftSliceRecord& best)
{
   if (sst.total_area() * HIER_ACCURACY >= best.minArea)
      return false;

   if (!abut_sym(sst))
      return false;
   counter[sst.expression_size()][5]++;

   if (sst.total_area() + extDeadspace(sst) >= best.minArea)
      return false;
   counter[sst.expression_size()][6]++;

   return true;
}
// --------------------------------------------------------

#endif
