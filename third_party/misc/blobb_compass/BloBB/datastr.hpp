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
#ifndef DATASTR_H
#define DATASTR_H

#include "stackqueue.hpp"

#include <iostream>
using namespace std;

class OTree;
typedef OTree* OTreePtr;

const int ORIENT_NUM = 8;
const int MAX_BLOCK_NUM = 100;

// --------------------------------------------------------
struct Dimension
{
   double width;
   double height;
};
// --------------------------------------------------------
struct CTRecord
{
   int next;
   int prev;

   double begin;
   double end;
   double CTL;

   OwnQueue<int> blockBelow;
};
// --------------------------------------------------------
class OTree
{
public:
   OTree(const int blockNum, const double totalArea);

   OwnStack<int> tree;
   OwnStack<int> perm;
   OwnStack<int> orient;

   double xloc[MAX_SIZE];
   double yloc[MAX_SIZE];

   int permpos[MAX_SIZE];
   int treepos[MAX_SIZE];
   int prev[MAX_SIZE];

   double deadspace;
   double pdspace[MAX_SIZE];

   const int BLOCK_NUM;
   const double TOTAL_AREA;

   int balance;
   int numZero;

   int last;
   int curr_contour;
   int perSize;

   CTRecord contour[MAX_SIZE];

   bool push_tree(int action);
   void push_perm(int b);
   void push_orient(int theta);

   void pop_orient();
   int pop_perm();
   void pop_tree();

   void setpos_update(const Dimension block[][ORIENT_NUM], int rect);
   void update_contour(int rect);
   void add_deadspace(int rect);
   
   void remove_block(int rect);
   
   double width() const;
   double height() const;

private:
   OTree();
   OTree(const OTree&);
};
// --------------------------------------------------------
struct FloorPlan
{
   double xLoc[MAX_SIZE];
   double yLoc[MAX_SIZE];
   int orient[MAX_SIZE];   
   int blockNum;

   double width;
   double height;
   double area;

   void operator =(const OTree& ot);
};
// --------------------------------------------------------
void outputfp(const Dimension block[][ORIENT_NUM], 
              const FloorPlan& fp,
              ostream& outs);
// --------------------------------------------------------
void Initialize(Dimension block[][ORIENT_NUM],
                OTreePtr& ot_ptr, istream& ins);
   
#endif
