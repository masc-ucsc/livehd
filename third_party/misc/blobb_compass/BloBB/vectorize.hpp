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
#ifndef VECTORIZE_H
#define VECTORIZE_H

#include "datastrst.hpp"
#include "datastrhierst.hpp"

#include <vector>
#include <stack>
#include <iostream>
using namespace std;

struct FloorPlanVec
{
   vector<double> xLoc;
   vector<double> yLoc;
   vector<int> orient;
   int blockNum;

   double width;
   double height;
   double area;
};
// --------------------------------------------------------

// -----Initializing functions-----
void InitializeSlice(vector< vector<Dimension> >& block,
                     int& blockNum,
                     istream& ins);
void InitializeCluster(const vector< vector<Dimension> >& block,
                       int blockNum,
                       vector<Cluster>& clusters);

// -----Evaluating floorplan functions-----
void Evaluate(const vector< vector<Dimension> >& block,
              const Cluster& finalCluster,
              FloorPlanVec& fp);
int ConstructTree(const vector< vector<Dimension> >& block,
                  const int BLOCK_NUM,
                  stack<int>& expression,
                  stack<int>& orient,
                  vector<TreeNode>& tree,
                  int& next,
                  FloorPlanVec& fp);
void EvaluateTree(const int BLOCK_NUM,
                  vector<TreeNode>& tree,
                  int ptr,
                  FloorPlanVec& fp);
void outputfp(const vector< vector<Dimension> >& block,
              const FloorPlanVec& fp,
              ostream& outs);
#endif
