/**************************************************************************
***    
*** Copyright (c) 2004-2005 Regents of the University of Michigan,
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
#ifndef BTREECOMPACTSSTREE_H
#define BTREECOMPACTSSTREE_H

#include "datastrfrontsoftst.hpp"

#include "btreecompact.hpp"
#include "btreefromsstree.hpp"
#include "basepacking.hpp"

#include <string>
#include <cfloat>
#include <algorithm>
using namespace std;

const double DEFAULT_SIDE_ACCURACY = 10000000;
// --------------------------------------------------------
double BTreeCompactSlice(const SoftPacking& spk,
                         const string& outfilename);
inline double getTolerance(const HardBlockInfoType& blockinfo);
// --------------------------------------------------------

// ========================
//      IMPLEMENTATION
// ========================
inline double getTolerance(const HardBlockInfoType& blockinfo)
{
   double min_side = DBL_MAX;
   for (int i = 0; i < blockinfo.blocknum(); i++)
      min_side = min(min_side,
                     min(blockinfo[i].width[0], blockinfo[i].height[0]));

   return min_side / DEFAULT_SIDE_ACCURACY;
}
// --------------------------------------------------------

#endif
