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
#ifndef DATASTRST_H
#define DATASTRST_H

#include "stackqueue.hpp"
#include "datastr.hpp"

class STree;
typedef STree* STreePtr;

// --------------------------------------------------------
class TreeNode
{
public:
   TreeNode();

   double x;
   double y;
   double width;
   double height;

   int sign;
   int left;
   int right;
   int parent;
};
// --------------------------------------------------------
class Node
{
public:
   Node();
   Node(int nSize, int nBLBlock, int nTRblblock,
        double nWidth, double nHeight, double nArea,
        double nDeadspace);
   Node(const Node& node);
   void operator =(const Node& node);

   int sign;
   int BLBlock;
   int TRblblock;
   double width;
   double height;
   double area;
   double deadspace;
};
// --------------------------------------------------------
class STree
{
public:
   STree(int blockNum, double totalArea);

   OwnStack<int> expression;
   OwnStack<int> orient;

   OwnStack<Node> buffer;
   OwnStack<Node> storage;

   const int BLOCK_NUM;
   const double TOTAL_AREA;
   const int PLUS;
   const int STAR;
   int balance;
   int perSize;
   double deadspace;

   void push_orient(int theta)
   {  orient.push(theta);  }

   void pop_orient()
   {  orient.pop();  }

   void push_operand(int rect, const Dimension block[][ORIENT_NUM]);
   int pop_operand();

   void push_operator(int sign);
   inline bool can_push_operator(int sign)
   {  return ((balance > 1) && (sign != expression.top())); }

   void pop_operator();

private: 
   STree(const STree&);
   STree();
};                              
// --------------------------------------------------------
class SliceRecord
{
public: 
   SliceRecord();

   int blockNum;
   double area;
   double deadspace;
   double width;
   double height;

   int expression[MAX_SIZE];
   int orient[MAX_SIZE];

   void operator =(const STree& st);

private:
   SliceRecord(const SliceRecord&);
};
void outputSRecord(const SliceRecord& slice, ostream& outs);
void outputNPE(const SliceRecord& slice, ostream& outs);
// --------------------------------------------------------
void InitializeSlice(Dimension block[][ORIENT_NUM],
                     STreePtr& st_ptr,
                     int& blockNum,
                     istream& ins);
                     
void Evaluate(const Dimension block[][ORIENT_NUM],
              const SliceRecord& sRecord,
              FloorPlan& fp);

int ConstructTree(const Dimension block[][ORIENT_NUM],
                  const int BLOCK_NUM,
                  OwnStack<int>& expression,
                  OwnStack<int>& orient,
                  TreeNode tree[],
                  int& next,
                  FloorPlan& fp);
void EvaluateTree(const int BLOCK_NUM, TreeNode tree[],
                  int ptr, FloorPlan& fp);
#endif
