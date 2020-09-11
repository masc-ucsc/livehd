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
#include "boundsoftst.hpp"
#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"

#include <algorithm>
using namespace std;

double OPTIMAL_ACCURACY = DEFAULT_OPTIMAL_ACCURACY;
double HIER_ACCURACY = DEFAULT_HIER_TXT_ACCURACY;

// --------------------------------------------------------
double extDeadspace(const SoftSTree& sst)
{
   int bSize = sst.buffer_size();
   if (bSize < 2)
      return 0;

   double ext_deadspace = 0;

   int cul_bdySize = sst.buffer_top().boundary.size();
   double cul_minWidth = sst.buffer_top().boundary[0].xCoord();
   double cul_minHeight = sst.buffer_top().boundary[cul_bdySize-1].yCoord();

   for (int i = bSize-2; i >= 0; i--)
   {
      int curr_bdySize = sst.buffer(i).boundary.size();
      double curr_maxWidth = sst.buffer(i).boundary[curr_bdySize-2].xCoord();
      double curr_maxHeight = sst.buffer(i).boundary[1].yCoord();
      
      double curr_minWidth = sst.buffer(i).boundary[1].xCoord();
      double curr_minHeight = sst.buffer(i).boundary[curr_bdySize-2].yCoord();

      if ((curr_maxWidth < cul_minWidth) &&
          (curr_maxHeight < cul_minHeight))
      {
         // have extended deadspace
         ext_deadspace += min(curr_minWidth * (cul_minHeight-curr_maxHeight),
                              curr_minHeight * (cul_minWidth-curr_maxWidth));
      }
      else
      {
         cul_minWidth = max(cul_minWidth, curr_minWidth);
         cul_minHeight = max(cul_minHeight, curr_minHeight);
      }
   }
   return ext_deadspace;
}
// --------------------------------------------------------
