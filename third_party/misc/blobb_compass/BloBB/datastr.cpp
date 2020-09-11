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
#include "datastr.hpp"
#include "parameters.hpp"

#include <float.h>
#include <algorithm>
using namespace std;

// --------------------------------------------------------
OTree::OTree(const int blockNum,
             const double totalArea)
   :  deadspace(0),
      BLOCK_NUM(blockNum),
      TOTAL_AREA(totalArea),
      balance(0),
      numZero(0),
      last(0),      
      curr_contour(blockNum),
      perSize(0)
{
   for (int k = 0; k < MAX_SIZE; k++)
   {
      permpos[k] = -1;
      treepos[k] = -1;
      prev[k] = -1;
      pdspace[k] = 0;
      xloc[k] = 0;
      yloc[k] = 0;
   }

   contour[blockNum].next = blockNum+1;
   contour[blockNum].prev = -1;
   contour[blockNum].begin = 0;
   contour[blockNum].end = 0;
   contour[blockNum].CTL = DBL_MAX;

   contour[blockNum+1].next = -1;
   contour[blockNum+1].prev = blockNum;
   contour[blockNum+1].begin = 0;
   contour[blockNum+1].end = DBL_MAX;
   contour[blockNum+1].CTL= 0;
}
// --------------------------------------------------------
bool OTree::push_tree(int action)
{
   bool accept;
   int act1 = action / 2;
   int act2 = action % 2;

   tree.push(act1);
   tree.push(act2);

   if (act1 == 0)
   {
      balance++;
      numZero++;
   }
   else
      balance--;

   accept = (balance >= 0);
   if (act2 == 0)
   {
      balance++;
      numZero++;
   }
   else
      balance--;

   accept = accept && (balance >= 0)
                   && (numZero <= BLOCK_NUM);
   return accept;
}
// --------------------------------------------------------
void OTree::push_perm(int b)
{
   perm.push(b);
   permpos[b] = perSize;
   perSize++;
}
// --------------------------------------------------------
void OTree::push_orient(int theta)
{
   orient.push(theta);
}
// --------------------------------------------------------
void OTree::pop_orient()
{
   orient.pop();
}
// --------------------------------------------------------
int OTree::pop_perm()
{
   int b = perm.pop();
   perSize--;
   permpos[b] = -1;
   return b;
}
// --------------------------------------------------------
void OTree::pop_tree()
{
   int act2 = tree.pop();
   int act1 = tree.pop();

   if (act2 == 0)
   {
      balance--;
      numZero--;
   }
   else
      balance++;

   if (act1 == 0)
   {
      balance--;
      numZero--;
   }
   else
      balance++;
}
// --------------------------------------------------------
void OTree::setpos_update(const Dimension block[][ORIENT_NUM],
                          int rect)
{
   int action = last;
   int theta = orient[permpos[rect]];

   while (tree[action] == 1)
   {
      curr_contour = prev[curr_contour];
      action++;
   }
   treepos[rect] = action;
   last = action+1;

   if (curr_contour == BLOCK_NUM)
      xloc[rect] = 0;
   else
      xloc[rect] = xloc[curr_contour] + 
         block[curr_contour][orient[permpos[curr_contour]]].width;
   prev[rect] = curr_contour;

   contour[rect].end = xloc[rect] + block[rect][theta].width;
   update_contour(rect);
   contour[rect].CTL = yloc[rect] + block[rect][theta].height;

   curr_contour = rect;
   add_deadspace(rect);   
}
// --------------------------------------------------------
void OTree::update_contour(int rect)
{
   int Lrect = prev[rect];
   int ptr = contour[Lrect].next;
   double maxCTL = contour[ptr].CTL;

   while (contour[ptr].end <= contour[rect].end)
   {
      if (contour[ptr].CTL > maxCTL)
         maxCTL = contour[ptr].CTL;
      contour[rect].blockBelow.enqueue(ptr);
      ptr = contour[ptr].next;
   }

   if ((contour[ptr].CTL > maxCTL) &&
       (contour[ptr].begin < contour[rect].end))
       maxCTL = contour[ptr].CTL;
   yloc[rect] = maxCTL;

   contour[rect].next = ptr;
   contour[ptr].prev = rect;
   contour[ptr].begin = contour[rect].end;

   contour[rect].prev = Lrect;
   contour[Lrect].next = rect;
   contour[rect].begin = contour[Lrect].end;
};
// --------------------------------------------------------
void OTree::add_deadspace(int rect)
{
   int size = contour[rect].blockBelow.size();
   int ptr, last;
   
   if (size == 0)
      return;

   for (int i = 0; i < size; i++)
   {
      ptr = contour[rect].blockBelow[i];
      double begin = max(contour[ptr].begin, contour[rect].begin);
      double end = contour[ptr].end;
      double CTL = contour[ptr].CTL;

      pdspace[rect] += (yloc[rect] - CTL) * (end-begin);
   }

   ptr = contour[rect].next;
   last = contour[rect].blockBelow.back();
   if (contour[last].end < contour[rect].end)
   {
      double begin = contour[last].end;
      double end = contour[rect].end;
      double CTL = contour[ptr].CTL;

      pdspace[rect] += (yloc[rect] - CTL) * (end-begin);
   }
   deadspace += pdspace[rect];
}
// --------------------------------------------------------
void OTree::remove_block(int rect)
{
   int Lrect = contour[rect].prev;
   int Rrect = contour[rect].next;
   int ptr = Lrect;
   int size = contour[rect].blockBelow.size();
   
   deadspace -= pdspace[rect];
   pdspace[rect] = 0;

   if (size != 0)
   {
      ptr = contour[rect].blockBelow.front();
      contour[Lrect].next = ptr;
      
      for (int i = 0; i < size; i++)
         ptr = contour[rect].blockBelow.dequeue();
   }
   else
      contour[Lrect].next = Rrect;

   contour[Rrect].prev = ptr;
   contour[Rrect].begin = contour[ptr].end;

   size = perm.size();
   if (size >= 2)
      curr_contour = perm[size-2];
   else
      curr_contour = BLOCK_NUM;
   last = treepos[curr_contour]+1;
   treepos[rect] = -1;
   prev[rect] = -1;
}
// --------------------------------------------------------
double OTree::width() const
{
   return contour[BLOCK_NUM+1].begin;
}
// --------------------------------------------------------
double OTree::height() const
{
   int ptr = contour[BLOCK_NUM].next;
   double maxCTL = 0;
   
   while (ptr != -1)
   {
      if (contour[ptr].CTL > maxCTL)
         maxCTL = contour[ptr].CTL;
      ptr = contour[ptr].next;
   }
   return maxCTL;
}
// -------------------------------------------------------
void FloorPlan::operator =(const OTree& ot)
{
   blockNum = ot.perSize;
   for (int i = 0; i < blockNum; i++)
   {
      int b = ot.perm[i];
      int theta = ot.orient[i];

      xLoc[b] = ot.xloc[b];
      yLoc[b] = ot.yloc[b];
      orient[b] = theta;      
   }
   
   width = ot.width();
   height = ot.height();
   area = width * height;
}
// --------------------------------------------------------
void outputfp(const Dimension block[][ORIENT_NUM],
              const FloorPlan& fp,
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
void Initialize(Dimension block[][ORIENT_NUM],
                OTreePtr& ot_ptr,
                istream& ins)
{
   int blockNum;
   double area = 0;
   ins >> blockNum;

   if (blockNum > MAX_BLOCK_NUM)
   {
      cout << "ERROR: The number of blocks (" << blockNum
           << ") exceeds maximum allowed (" << MAX_BLOCK_NUM << ")." << endl;
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
   ot_ptr = new OTree(blockNum, area);   
}
// --------------------------------------------------------
