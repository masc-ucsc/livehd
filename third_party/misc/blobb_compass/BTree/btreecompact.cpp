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
#include "btreecompact.hpp"

#include <vector>
using namespace std;

// --------------------------------------------------------
void BTreeCompactor::build_orth_tree()
{
   clean_contour(in_contour);
   clean_tree(in_orth_tree);
   
   int tree_prev = NUM_BLOCKS;
   int tree_curr = in_tree[NUM_BLOCKS].left; // start with first block
   while (tree_curr != NUM_BLOCKS) // until reach the root again
   {
      if (tree_prev == in_tree[tree_curr].parent)
      {
         build_orth_tree_add_block(tree_curr);
         tree_prev = tree_curr;
         if (in_tree[tree_curr].left != UNDEFINED)
            tree_curr = in_tree[tree_curr].left;
         else if (in_tree[tree_curr].right != UNDEFINED)
            tree_curr = in_tree[tree_curr].right;
         else
            tree_curr = in_tree[tree_curr].parent;
      }
      else if (tree_prev == in_tree[tree_curr].left)
      {
         tree_prev = tree_curr;
         if (in_tree[tree_curr].right != UNDEFINED)
            tree_curr = in_tree[tree_curr].right;
         else
            tree_curr = in_tree[tree_curr].parent;
      }
      else
      {
         tree_prev = tree_curr;
         tree_curr = in_tree[tree_curr].parent;
      }
   }
   in_totalWidth = in_contour[NUM_BLOCKS+1].begin;

   int contour_ptr = in_contour[NUM_BLOCKS].next;
   in_totalHeight = 0;
   while (contour_ptr != NUM_BLOCKS+1)
   {
      in_totalHeight = max(in_totalHeight, in_contour[contour_ptr].CTL);
      contour_ptr = in_contour[contour_ptr].next;
   }
   in_totalArea = in_totalWidth * in_totalHeight;

   fix_orth_tree(in_orth_tree);
}
// --------------------------------------------------------
void BTreeCompactor::build_orth_tree_add_block(int tree_ptr)
{
   int tree_parent = in_tree[tree_ptr].parent;
   int orth_tree_parent = NUM_BLOCKS;
   
   double maxCTL = -1;
   int contour_ptr = UNDEFINED;
   int contour_prev = UNDEFINED;
   
   if (tree_ptr == in_tree[in_tree[tree_ptr].parent].left)
   {
      in_contour[tree_ptr].begin = in_contour[tree_parent].end;
      contour_ptr = in_contour[tree_parent].next;
   }
   else
   {
      in_contour[tree_ptr].begin = in_contour[tree_parent].begin;
      contour_ptr = tree_parent;
   }
   contour_prev = in_contour[contour_ptr].prev; // begins of cPtr/tPtr match
   orth_tree_parent = contour_ptr;       // initialize necessary
   maxCTL = in_contour[contour_ptr].CTL; // since width/height may be 0

   int block = in_tree[tree_ptr].block_index;
   int theta = in_tree[tree_ptr].orient;
   in_contour[tree_ptr].end =
      in_contour[tree_ptr].begin + in_blockinfo[block].width[theta];

   while (in_contour[contour_ptr].end <=
          in_contour[tree_ptr].end + TOLERANCE)
   {
      if (in_contour[contour_ptr].CTL > maxCTL)
      {
         maxCTL = in_contour[contour_ptr].CTL;
         orth_tree_parent = contour_ptr;
      }
      contour_ptr = in_contour[contour_ptr].next;
   }

   if (in_contour[contour_ptr].begin + TOLERANCE <
       in_contour[tree_ptr].end)
   {
      if (in_contour[contour_ptr].CTL > maxCTL)
      {
         maxCTL = in_contour[contour_ptr].CTL;
         orth_tree_parent = contour_ptr;
      }
   }
   
   in_xloc[tree_ptr] = in_contour[tree_ptr].begin;
   in_yloc[tree_ptr] = maxCTL;
   in_width[tree_ptr] = in_blockinfo[block].width[theta];
   in_height[tree_ptr] = in_blockinfo[block].height[theta];
      
   in_contour[tree_ptr].CTL =  maxCTL + in_blockinfo[block].height[theta];
   in_contour[tree_ptr].next = contour_ptr;
   in_contour[contour_ptr].prev = tree_ptr;
   in_contour[contour_ptr].begin = in_contour[tree_ptr].end;

   in_contour[tree_ptr].prev = contour_prev;
   in_contour[contour_prev].next = tree_ptr;
   in_contour[tree_ptr].begin = in_contour[contour_prev].end;

   // edit "in_orth_tree", the orthogonal tree
   if (in_orth_tree[orth_tree_parent].left == UNDEFINED)
   {
      // left-child of the parent
      in_orth_tree[orth_tree_parent].left = tree_ptr;
      in_orth_tree[tree_ptr].parent = orth_tree_parent;
   }
   else
   {
      // sibling fo the left-child of the parent
      int orth_tree_ptr = in_orth_tree[orth_tree_parent].left;
      while (in_orth_tree[orth_tree_ptr].right != UNDEFINED)
         orth_tree_ptr = in_orth_tree[orth_tree_ptr].right;

      in_orth_tree[orth_tree_ptr].right = tree_ptr;
      in_orth_tree[tree_ptr].parent = orth_tree_ptr;
   }
}
// --------------------------------------------------------
