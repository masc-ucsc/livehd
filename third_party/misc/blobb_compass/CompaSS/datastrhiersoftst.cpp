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
#include "datastrhiersoftst.hpp"
#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"

#include <vector>
using namespace std;

// --------------------------------------------------------
SoftCluster::SoftCluster(int instr,
                         const SoftClusterSet& clusterSet,
                         const SoftSliceRecord& ssr)
   : in_boundary(instr, ssr.boundary),
     in_blkArea(0),
     in_deadspace(ssr.minDeadspace)   
{
   for (unsigned int i = 0; i < ssr.expression.size(); i++)
   {
      int big_sign = ssr.expression[i];
      if (SoftSTree::isOperand(big_sign))
      {
         int expr_size = clusterSet[big_sign].expression_size();
         for (int j = 0; j < expr_size; j++)
         {
            int small_sign = clusterSet[big_sign].expression(j);
            if (SoftSTree::isOperand(small_sign))
               in_expression.push_back(small_sign);
            else
               in_expression.push_back(small_sign);
         }

         double newBlkArea = clusterSet[big_sign].blkArea();
         double newDeadspace = clusterSet[big_sign].deadspace();
         in_blkArea += newBlkArea;
         in_deadspace += newDeadspace;
      }
      else
         in_expression.push_back(SoftSTree::BOTH);
   }
}
// --------------------------------------------------------
   
            
