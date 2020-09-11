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
#include "datastrst.hpp"
#include "stackqueue.hpp"
#include "parameters.hpp"

#include <algorithm>
#include <float.h>
#include <iomanip>
using namespace std;

TreeNode::TreeNode() 
   : x(0),
     y(0),
     width(0),
     height(0),
     sign(-1),
     left(-1),
     right(-1),
     parent(-1)
{}
// --------------------------------------------------------
Node::Node()
   : sign(-1),
     BLBlock(-1),
     TRblblock(-1),
     width(0),
     height(0),
     area(0),
     deadspace(0)
{}
// --------------------------------------------------------
Node::Node(int nSign,
           int nBLBlock,
           int nTRblblock,
           double nWidth,
           double nHeight,
           double nArea,
           double nDeadspace)
           : sign(nSign),
             BLBlock(nBLBlock),
             TRblblock(nTRblblock),
             width(nWidth),
             height(nHeight),
             area(nArea),
             deadspace(nDeadspace)
{}
// --------------------------------------------------------
Node::Node(const Node& node)
   : sign(node.sign),
     BLBlock(node.BLBlock),
     TRblblock(node.TRblblock),
     width(node.width),
     height(node.height),
     area(node.area),
     deadspace(node.deadspace)
{}
// --------------------------------------------------------
void Node::operator =(const Node& node)
{
   sign = node.sign;
   BLBlock = node.BLBlock;
   TRblblock = node.TRblblock;
   width = node.width;
   height = node.height;
   area = node.area;
   deadspace = node.deadspace;
}
// --------------------------------------------------------
STree::STree(int blockNum,
             double totalArea)
   : BLOCK_NUM(blockNum),
     TOTAL_AREA(totalArea),
     PLUS(blockNum),
     STAR(blockNum+1),
     balance(0),
     perSize(0),
     deadspace(0)
{}
// --------------------------------------------------------
void STree::push_operand(int rect,
                         const Dimension block[][ORIENT_NUM])
{
   int theta = orient.top();
   double nWidth = block[rect][theta].width;
   double nHeight = block[rect][theta].height;
   Node newNode(rect, rect, rect,
                nWidth, nHeight,
                nWidth * nHeight, 0);

   expression.push(rect);
   buffer.push(newNode);
   balance++;
   perSize++;                
}
// --------------------------------------------------------
int STree::pop_operand()
{
   perSize--;
   balance--;
   buffer.pop();
   return expression.pop();
}
// --------------------------------------------------------
void STree::push_operator(int sign)
{
   expression.push(sign);
   Node RightTop(buffer.pop());
   Node LeftBottom(buffer.pop());

   storage.push(RightTop);
   storage.push(LeftBottom);

   int newSign, newBLBlock, newTRblblock;
   double newWidth, newHeight, newArea;
   double addDeadspace, newDeadspace;
   if (sign == PLUS)
   {
      newSign = PLUS;
      newWidth = max(RightTop.width, LeftBottom.width);
      newHeight = RightTop.height + LeftBottom.height;
   }
   else
   {
      newSign = STAR;
      newWidth = RightTop.width + LeftBottom.width;
      newHeight = max(RightTop.height, LeftBottom.height);
   }
   newBLBlock = LeftBottom.BLBlock;
   newTRblblock = RightTop.BLBlock;
   newArea = newWidth * newHeight;

   addDeadspace = newArea - RightTop.area - LeftBottom.area;
   newDeadspace = RightTop.deadspace + LeftBottom.deadspace
                  + addDeadspace;

   Node newNode(newSign, newBLBlock, newTRblblock,
                newWidth, newHeight, newArea, newDeadspace);
   buffer.push(newNode);
   balance--;
   deadspace += addDeadspace;
}
// --------------------------------------------------------
void STree::pop_operator()
{
   Node oNode = buffer.pop();
   Node nNode1 = storage.pop();
   Node nNode2 = storage.pop();

   balance++;
   deadspace -= oNode.area - nNode1.area - nNode2.area;

   buffer.push(nNode1);
   buffer.push(nNode2);

   expression.pop();
}
// --------------------------------------------------------
SliceRecord::SliceRecord()
   : blockNum(0),
     area(DBL_MAX),
     deadspace(DBL_MAX),
     width(DBL_MAX),
     height(DBL_MAX)
{}
// --------------------------------------------------------
void SliceRecord::operator =(const STree& st)
{
   blockNum = st.BLOCK_NUM;
   area = st.TOTAL_AREA + st.deadspace;
   deadspace = st.deadspace;
   width = st.buffer[0].width;
   height = st.buffer[0].height;

   int exprSize = st.expression.size();
   int orientSize = st.orient.size();

   for (int i = 0; i < exprSize; i++)
      expression[i] = st.expression[i];

   for (int i = 0; i < orientSize; i++)
      orient[i] = st.orient[i];
}
// --------------------------------------------------------
void outputSRecord(const SliceRecord& slice, ostream& outs)
{
   outs << "area: " << slice.area << endl;
   outs << "dspace: " << slice.deadspace << endl;
   outs << "width: " << slice.width << endl;
   outs << "height: " << slice.height << endl;

   for (int i = 0; i < 2*slice.blockNum - 1; i++)
      if (slice.expression[i] == slice.blockNum)
         outs << "  +";
      else if (slice.expression[i] == slice.blockNum+1)
         outs << "  *";
      else
         outs << setw(3) << slice.expression[i];
   outs << endl;

   for (int i = 0; i < slice.blockNum; i++)
      outs << setw(3) << slice.orient[i];
   outs << endl;
}
// --------------------------------------------------------
void outputNPE(const SliceRecord& slice, ostream& outs)
{
   outs << "expression: ";
   for (int i = 0; i < 2*slice.blockNum - 1; i++)
      if (slice.expression[i] == slice.blockNum)
         outs << "  +";
      else if (slice.expression[i] == slice.blockNum+1)
         outs << "  *";
      else
         outs << setw(3) << slice.expression[i];
   outs << endl;

   outs << "orientatn:  ";
   int blockCount = 0;
   for (int i = 0; i < 2*slice.blockNum - 1; i++)
      if (slice.expression[i] < slice.blockNum) // if is a block
	 outs << setw(3) << slice.orient[blockCount++];
      else  // if not
	 outs << "   "; 
   outs << endl;
}
// --------------------------------------------------------
void InitializeSlice(Dimension block[][ORIENT_NUM],
                   STreePtr& st_ptr,
                   int& blockNum,
                   istream& ins)
{
   double area = 0;
   ins >> blockNum;
   if (blockNum > MAX_BLOCK_NUM)
   {
      cout << "ERROR: The number of blocks (" << blockNum << ") "
	   << "exceeds maximum allowed (" << MAX_BLOCK_NUM << ")." << endl;
      exit(1);
   }

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
	    cout << "ERROR: invalid value of \"ENG_ORIENT_CONSIDERED\" ("
		 << ENG_ORIENT_CONSIDERED << ")." << endl;
	    exit(1);
	 }

      area += width * height;
   }
   st_ptr = new STree(blockNum, area);
}
// --------------------------------------------------------
void Evaluate(const Dimension block[][ORIENT_NUM],
              const SliceRecord& sRecord,
              FloorPlan& fp)
{
   const int EXPR_SIZE = 2*sRecord.blockNum - 1;
   const int BLOCK_NUM = sRecord.blockNum;

   OwnStack<int> temp_expr;
   OwnStack<int> temp_orient;
   TreeNode tree[MAX_SIZE];
   int next = 0;

   for (int i = 0; i < EXPR_SIZE; i++)
      temp_expr.push(sRecord.expression[i]);

   for (int i = 0; i < BLOCK_NUM; i++)
      temp_orient.push(sRecord.orient[i]);

   ConstructTree(block, BLOCK_NUM, temp_expr, temp_orient, tree, next, fp);
   EvaluateTree(BLOCK_NUM, tree, 0, fp);
   fp.blockNum = BLOCK_NUM;
   fp.width = tree[0].width;
   fp.height = tree[0].height;
   fp.area = fp.width * fp.height;   
}
// --------------------------------------------------------
int ConstructTree(const Dimension block[][ORIENT_NUM],
                  const int BLOCK_NUM,
                  OwnStack<int>& expression,
                  OwnStack<int>& orient,
                  TreeNode tree[],
                  int& next,
                  FloorPlan& fp)
{
   int sign = expression.pop();
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

      int theta = orient.pop();
      tree[here].width = block[sign][theta].width;
      tree[here].height = block[sign][theta].height;

      fp.orient[sign] = theta;
   }
   return here;
}
// --------------------------------------------------------
void EvaluateTree(const int BLOCK_NUM,
                  TreeNode tree[],
                  int ptr,
                  FloorPlan& fp)
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



   

