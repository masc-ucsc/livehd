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
#include "vectorize.hpp"
#include "datastrst.hpp"
#include "datastrhierst.hpp"
#include "parameters.hpp"

#include <iostream>
using namespace std;

#include <cstdlib>

// --------------------------------------------------------
void InitializeSlice(vector< vector<Dimension> >& block,
                     int& blockNum,
                     istream& ins)
{
   double area = 0;
   ins >> blockNum;

   block.resize(blockNum);
   for (int i = 0; i < blockNum; i++)
      block[i].resize(ORIENT_NUM);

   for (int i = 0; i < blockNum; i++)
   {
      double width, height;
      ins >> width >> height;
      for (int j = 0; j < ORIENT_NUM; j++)
         if (j % ENG_ORIENT_CONSIDERED == 0)
         {
            block[i][j].width = width;
            block[i][j].height = height;
         }
         else if (j % ENG_ORIENT_CONSIDERED == 1)
         {
            block[i][j].width = height;
            block[i][j].height = width;
         }
	 else
	 {
	    cout << "ERROR: invalid \"ENG_ORIENT_CONSIDERED\" ("
		 << ENG_ORIENT_CONSIDERED << ")." << endl;
	    exit(1);
	 }
      area += width * height;
   }   
}
// --------------------------------------------------------
void InitializeCluster(const vector< vector<Dimension> >& block,
                       int blockNum,
                       vector<Cluster>& clusters)
{
   for (int i = 0; i < blockNum; i++)
   {
      Cluster c(blockNum);
      
      c.BLBlock = i;
      c.width = block[i][0].width;
      c.height = block[i][0].height;
      c.area = c.width * c.height;
      c.deadspace = 0;

      c.expression.push_back(i);
      c.orient.push_back(0);

      clusters.push_back(c);
   }
}
// --------------------------------------------------------
void Evaluate(const vector< vector<Dimension> >& block,
              const Cluster& finalCluster,
              FloorPlanVec& fp)
{
   const int EXPR_SIZE = finalCluster.expression.size();
   const int BLOCK_NUM = finalCluster.orient.size();

   stack<int> temp_expr;
   stack<int> temp_orient;
   vector<TreeNode> tree(EXPR_SIZE);
   int next = 0;

   for (int i = 0; i < EXPR_SIZE; i++)
      temp_expr.push(finalCluster.expression[i]);

   for (int i = 0; i < BLOCK_NUM; i++)
      temp_orient.push(finalCluster.orient[i]);

   // ---initialize fp---
   fp.xLoc.resize(BLOCK_NUM);
   fp.yLoc.resize(BLOCK_NUM);
   fp.orient.resize(BLOCK_NUM);

   ConstructTree(block, BLOCK_NUM, temp_expr, temp_orient, tree, next, fp);
   EvaluateTree(BLOCK_NUM, tree, 0, fp);

   fp.blockNum = BLOCK_NUM;
   fp.width = tree[0].width;
   fp.height = tree[0].height;
   fp.area = fp.width * fp.height;   
}
// --------------------------------------------------------
int ConstructTree(const vector< vector<Dimension> >& block,
                  const int BLOCK_NUM,
                  stack<int>& expression,
                  stack<int>& orient,
                  vector<TreeNode>& tree,
                  int& next,
                  FloorPlanVec& fp)
{
   int sign = expression.top();
   expression.pop();
   int here = next;

   next++;
   tree[here].sign = sign;

   if (sign >= BLOCK_NUM)
   {
      tree[here].right = ConstructTree(block, BLOCK_NUM, expression, 
                                       orient, tree, next, fp);
      tree[here].left = ConstructTree(block, BLOCK_NUM, expression,
                                      orient, tree, next, fp);
      tree[tree[here].right].parent = here;
      tree[tree[here].left].parent = here;
   }
   else
   {
      tree[here].right = -1;
      tree[here].left = -1;

      int theta = orient.top();
      orient.pop();
      tree[here].width = block[sign][theta].width;
      tree[here].height = block[sign][theta].height;

      fp.orient[sign] = theta;
   }
   return here;
}
// --------------------------------------------------------
void EvaluateTree(const int BLOCK_NUM,
                  vector<TreeNode>& tree,
                  int ptr,
                  FloorPlanVec& fp)
{
   static const int PLUS = BLOCK_NUM;
   static const int STAR = BLOCK_NUM+1;
   static const int ROOT = -1;

   int parent = tree[ptr].parent;
   int parMode = (parent == -1)? ROOT : tree[parent].sign;
   int hereMode = tree[ptr].sign;

   if (parMode == PLUS)
   {
      tree[ptr].x = tree[parent].x;
      tree[ptr].y = tree[parent].y + tree[parent].height;      
   }
   else if (parMode == STAR)
   {
      tree[ptr].x = tree[parent].x + tree[parent].width;
      tree[ptr].y = tree[parent].y;      
   }
   else if (parMode == ROOT)
   {
      tree[ptr].x = 0;
      tree[ptr].y = 0;
   }   
   
   if (tree[ptr].left == -1)
   {
      int rect = tree[ptr].sign;
      fp.xLoc[rect] = tree[ptr].x;
      fp.yLoc[rect] = tree[ptr].y;      
      return;
   }

   tree[ptr].width = 0;
   tree[ptr].height = 0;

   EvaluateTree(BLOCK_NUM, tree, tree[ptr].left, fp);
   if (hereMode == PLUS)
      tree[ptr].height += tree[tree[ptr].left].height;
   else if (hereMode == STAR)
      tree[ptr].width += tree[tree[ptr].left].width;


   EvaluateTree(BLOCK_NUM, tree, tree[ptr].right, fp);
   if (hereMode == PLUS)
   {
      tree[ptr].height += tree[tree[ptr].right].height;
      tree[ptr].width = max(tree[tree[ptr].left].width,
                            tree[tree[ptr].right].width);
   }
   else if (hereMode == STAR)
   {
      tree[ptr].width += tree[tree[ptr].right].width;
      tree[ptr].height = max(tree[tree[ptr].left].height,
                             tree[tree[ptr].right].height);
   }   
}
// --------------------------------------------------------
void outputfp(const vector< vector<Dimension> >& block,
              const FloorPlanVec& fp,
              ostream& outs)
{
   outs.setf(ios::fixed);
   outs.precision(3);
   outs << fp.width << endl;
   outs << fp.height << endl;
   outs << fp.blockNum << endl;
   for (int i = 0; i < fp.blockNum; i++)
   {
      double width = block[i][fp.orient[i]].width;
      double height = block[i][fp.orient[i]].height;
      outs << width << " " << height << endl;
   }
   outs << endl;
   for (int i = 0; i < fp.blockNum; i++)
   {
      outs << fp.xLoc[i] << " " << fp.yLoc[i] << endl;
   }
   outs << endl;
}
// --------------------------------------------------------
