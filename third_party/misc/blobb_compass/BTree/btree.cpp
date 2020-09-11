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
#include "btree.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <float.h>
#include <vector>
#include <algorithm>
using namespace std;
using namespace basepacking_h;

const int BTree::UNDEFINED = Dimension::UNDEFINED;
// ========================================================
BTree::BTree(const HardBlockInfoType& blockinfo)
   : tree(in_tree),
     contour(in_contour),
     NUM_BLOCKS(blockinfo.blocknum()),
     in_blockinfo(blockinfo),
     in_tree(blockinfo.blocknum()+2),
     in_contour(blockinfo.blocknum()+2),
     
     in_xloc(blockinfo.blocknum()+2, UNDEFINED),
     in_yloc(blockinfo.blocknum()+2, UNDEFINED),
     in_width(blockinfo.blocknum()+2, UNDEFINED),
     in_height(blockinfo.blocknum()+2, UNDEFINED),
     
     in_blockArea(blockinfo.blockArea()),
     in_totalArea(0),
     in_totalWidth(0),
     in_totalHeight(0),

     TOLERANCE(0)
{
   int vec_size = NUM_BLOCKS+2;
   for (int i = 0; i < vec_size; i++)
   {
      in_tree[i].parent = UNDEFINED;
      in_tree[i].left = UNDEFINED;
      in_tree[i].right = UNDEFINED;
      in_tree[i].block_index = i;
      in_tree[i].orient = UNDEFINED;

      in_contour[i].next = UNDEFINED;
      in_contour[i].prev = UNDEFINED;
      in_contour[i].begin = UNDEFINED;
      in_contour[i].end = UNDEFINED;
      in_contour[i].CTL = UNDEFINED;
   }

   in_contour[NUM_BLOCKS].next = NUM_BLOCKS+1;
   in_contour[NUM_BLOCKS].prev = UNDEFINED;
   in_contour[NUM_BLOCKS].begin = 0;
   in_contour[NUM_BLOCKS].end = 0;
   in_contour[NUM_BLOCKS].CTL = Dimension::INFTY;

   in_xloc[NUM_BLOCKS] = 0;
   in_yloc[NUM_BLOCKS] = 0;
   in_width[NUM_BLOCKS] = 0;
   in_height[NUM_BLOCKS] = Dimension::INFTY;
   
   in_contour[NUM_BLOCKS+1].next = UNDEFINED;
   in_contour[NUM_BLOCKS+1].prev = NUM_BLOCKS;
   in_contour[NUM_BLOCKS+1].begin = 0;
   in_contour[NUM_BLOCKS+1].end = Dimension::INFTY;
   in_contour[NUM_BLOCKS+1].CTL = 0;

   in_xloc[NUM_BLOCKS+1] = 0;
   in_yloc[NUM_BLOCKS+1] = 0;
   in_width[NUM_BLOCKS+1] = Dimension::INFTY;
   in_height[NUM_BLOCKS+1] = 0;
}
// --------------------------------------------------------
BTree::BTree(const HardBlockInfoType& blockinfo,
             double nTolerance)
   : tree(in_tree),
     contour(in_contour),
     NUM_BLOCKS(blockinfo.blocknum()),
     in_blockinfo(blockinfo),
     in_tree(blockinfo.blocknum()+2),
     in_contour(blockinfo.blocknum()+2),
     
     in_xloc(blockinfo.blocknum()+2, UNDEFINED),
     in_yloc(blockinfo.blocknum()+2, UNDEFINED),
     in_width(blockinfo.blocknum()+2, UNDEFINED),
     in_height(blockinfo.blocknum()+2, UNDEFINED),
     
     in_blockArea(blockinfo.blockArea()),
     in_totalArea(0),
     in_totalWidth(0),
     in_totalHeight(0),

     TOLERANCE(nTolerance)
{
   int vec_size = NUM_BLOCKS+2;
   for (int i = 0; i < vec_size; i++)
   {
      in_tree[i].parent = UNDEFINED;
      in_tree[i].left = UNDEFINED;
      in_tree[i].right = UNDEFINED;
      in_tree[i].block_index = i;
      in_tree[i].orient = UNDEFINED;

      in_contour[i].next = UNDEFINED;
      in_contour[i].prev = UNDEFINED;
      in_contour[i].begin = UNDEFINED;
      in_contour[i].end = UNDEFINED;
      in_contour[i].CTL = UNDEFINED;
   }

   in_contour[NUM_BLOCKS].next = NUM_BLOCKS+1;
   in_contour[NUM_BLOCKS].prev = UNDEFINED;
   in_contour[NUM_BLOCKS].begin = 0;
   in_contour[NUM_BLOCKS].end = 0;
   in_contour[NUM_BLOCKS].CTL = Dimension::INFTY;

   in_xloc[NUM_BLOCKS] = 0;
   in_yloc[NUM_BLOCKS] = 0;
   in_width[NUM_BLOCKS] = 0;
   in_height[NUM_BLOCKS] = Dimension::INFTY;
   
   in_contour[NUM_BLOCKS+1].next = UNDEFINED;
   in_contour[NUM_BLOCKS+1].prev = NUM_BLOCKS;
   in_contour[NUM_BLOCKS+1].begin = 0;
   in_contour[NUM_BLOCKS+1].end = Dimension::INFTY;
   in_contour[NUM_BLOCKS+1].CTL = 0;

   in_xloc[NUM_BLOCKS+1] = 0;
   in_yloc[NUM_BLOCKS+1] = 0;
   in_width[NUM_BLOCKS+1] = Dimension::INFTY;
   in_height[NUM_BLOCKS+1] = 0;
}
// --------------------------------------------------------
void BTree::evaluate(const vector<BTreeNode>& ntree)
{
   if (ntree.size() != in_tree.size())
   {
      cout << "ERROR: size of btree's doesn't match." << endl;
      exit(1);
   }

   in_tree = ntree;
   contour_evaluate();
}  
// --------------------------------------------------------
void BTree::evaluate(const vector<int>& tree_bits,
                     const vector<int>& perm,
                     const vector<int>& orient)
{
   if (int(perm.size()) != NUM_BLOCKS)
   {
      cout << "ERROR: the permutation length doesn't match with "
           << "size of the tree." << endl;
      exit(1);
   }
   bits2tree(tree_bits, perm, orient, in_tree);
//   OutputBTree(cout, in_tree);
   contour_evaluate();
}
// --------------------------------------------------------
void BTree::bits2tree(const vector<int>& tree_bits,
                       const vector<int>& perm,
                       const vector<int>& orient,
                       vector<BTreeNode>& ntree)
{
   int perm_size = perm.size();
   ntree.resize(perm_size+2);
   clean_tree(ntree);

   int treePtr = perm_size;
   int bitsPtr = 0;

   int lastAct = -1;
   for (int i = 0; i < perm_size; i++)
   {
      int currAct = tree_bits[bitsPtr];
      while (currAct == 1)
      {
         // move up a level/sibling
         if (lastAct == 1)
            treePtr = ntree[treePtr].parent;

         // move among siblings
         while (ntree[treePtr].right != UNDEFINED)
            treePtr = ntree[treePtr].parent;
         bitsPtr++;
         lastAct = 1;
         currAct = tree_bits[bitsPtr];
      }

      if (lastAct != 1)
         ntree[treePtr].left = perm[i];
      else // lastAct == 1
         ntree[treePtr].right = perm[i];
      
      ntree[perm[i]].parent = treePtr;
      ntree[perm[i]].block_index = perm[i];
      ntree[perm[i]].orient = orient[i];

      treePtr = perm[i];
      lastAct = 0;
      bitsPtr++;
   }         
}
// --------------------------------------------------------
void BTree::swap(int indexOne,
                 int indexTwo)
{
   int indexOne_left = in_tree[indexOne].left;
   int indexOne_right = in_tree[indexOne].right;
   int indexOne_parent = in_tree[indexOne].parent;

   int indexTwo_left = in_tree[indexTwo].left;
   int indexTwo_right = in_tree[indexTwo].right;
   int indexTwo_parent = in_tree[indexTwo].parent;

   if (indexOne == indexTwo_parent)
      swap_parent_child(indexOne, (indexTwo == in_tree[indexOne].left));
   else if (indexTwo == indexOne_parent)
      swap_parent_child(indexTwo, (indexOne == in_tree[indexTwo].left));
   else
   {
      // update around indexOne
      in_tree[indexOne].parent = indexTwo_parent;
      in_tree[indexOne].left = indexTwo_left;
      in_tree[indexOne].right = indexTwo_right;

      if (indexOne == in_tree[indexOne_parent].left)
         in_tree[indexOne_parent].left = indexTwo;
      else
         in_tree[indexOne_parent].right = indexTwo;

      if (indexOne_left != UNDEFINED)
         in_tree[indexOne_left].parent = indexTwo;
      
      if (indexOne_right != UNDEFINED)
         in_tree[indexOne_right].parent = indexTwo;
      
      // update around indexTwo
      in_tree[indexTwo].parent = indexOne_parent;
      in_tree[indexTwo].left = indexOne_left;
      in_tree[indexTwo].right = indexOne_right;
      
      if (indexTwo == in_tree[indexTwo_parent].left)
         in_tree[indexTwo_parent].left = indexOne;
      else
         in_tree[indexTwo_parent].right = indexOne;
      
      if (indexTwo_left != UNDEFINED)
         in_tree[indexTwo_left].parent = indexOne;
      
      if (indexTwo_right != UNDEFINED)
         in_tree[indexTwo_right].parent = indexOne;
   }
   contour_evaluate();   
}
// --------------------------------------------------------
void BTree::swap_parent_child(int parent,
                              bool isLeft)
{
   int parent_parent = in_tree[parent].parent;
   int parent_left = in_tree[parent].left;
   int parent_right = in_tree[parent].right;

   int child = (isLeft)? in_tree[parent].left : in_tree[parent].right;
   int child_left = in_tree[child].left;
   int child_right = in_tree[child].right;

   if (isLeft)
   {
      in_tree[parent].parent = child;
      in_tree[parent].left = child_left;
      in_tree[parent].right = child_right;

      if (parent == in_tree[parent_parent].left)
         in_tree[parent_parent].left = child;
      else
         in_tree[parent_parent].right = child;

      if (parent_right != UNDEFINED)
         in_tree[parent_right].parent = child;

      in_tree[child].parent = parent_parent;
      in_tree[child].left = parent;
      in_tree[child].right = parent_right;

      if (child_left != UNDEFINED)
         in_tree[child_left].parent = parent;

      if (child_right != UNDEFINED)
         in_tree[child_right].parent = parent;
   }
   else
   {
      in_tree[parent].parent = child;
      in_tree[parent].left = child_left;
      in_tree[parent].right = child_right;

      if (parent == in_tree[parent_parent].left)
         in_tree[parent_parent].left = child;
      else
         in_tree[parent_parent].right = child;

      if (parent_left != UNDEFINED)
         in_tree[parent_left].parent = child;

      in_tree[child].parent = parent_parent;
      in_tree[child].left = parent_left;
      in_tree[child].right = parent;

      if (child_left != UNDEFINED)
         in_tree[child_left].parent = parent;

      if (child_right != UNDEFINED)
         in_tree[child_right].parent = parent;
   }
}
// --------------------------------------------------------
void BTree::move(int index,
                 int target,
                 bool leftChild)
{
   int index_parent = in_tree[index].parent;
   int index_left = in_tree[index].left;
   int index_right = in_tree[index].right;

   // remove "index" from the tree
   if ((index_left != UNDEFINED) && (index_right != UNDEFINED))
      remove_left_up_right_down(index);
   else if (index_left != UNDEFINED)
   {
      in_tree[index_left].parent = index_parent;
      if (index == in_tree[index_parent].left)
         in_tree[index_parent].left = index_left;
      else
         in_tree[index_parent].right = index_left;
   }
   else if (index_right != UNDEFINED)
   {
      in_tree[index_right].parent = index_parent;
      if (index == in_tree[index_parent].left)
         in_tree[index_parent].left = index_right;
      else
         in_tree[index_parent].right = index_right;
   }
   else
   {
      if (index == in_tree[index_parent].left)
         in_tree[index_parent].left = UNDEFINED;
      else
         in_tree[index_parent].right = UNDEFINED;
   }

   int target_left = in_tree[target].left;
   int target_right = in_tree[target].right;
   
   // add "index" to the required location
   if (leftChild)
   {
      in_tree[target].left = index;
      if (target_left != UNDEFINED)
         in_tree[target_left].parent = index;
         
      in_tree[index].parent = target;
      in_tree[index].left = target_left;
      in_tree[index].right = UNDEFINED;
   }
   else
   {
      in_tree[target].right = index;
      if (target_right != UNDEFINED)
         in_tree[target_right].parent = index;

      in_tree[index].parent = target;
      in_tree[index].left = UNDEFINED;
      in_tree[index].right = target_right;
   }
   
   contour_evaluate();
}
// --------------------------------------------------------
void BTree::remove_left_up_right_down(int index)
{
   int index_parent = in_tree[index].parent;
   int index_left = in_tree[index].left;
   int index_right = in_tree[index].right;

   in_tree[index_left].parent = index_parent;
   if (index == in_tree[index_parent].left)
      in_tree[index_parent].left = index_left;
   else
      in_tree[index_parent].right = index_left;

   int ptr = index_left;
   while (in_tree[ptr].right != UNDEFINED)
      ptr = in_tree[ptr].right;

   in_tree[ptr].right = index_right;
   in_tree[index_right].parent = ptr;
}
// --------------------------------------------------------
void BTree::contour_evaluate() // assume the tree is set
{
   clean_contour(in_contour);
   
   int tree_prev = NUM_BLOCKS;
   int tree_curr = in_tree[NUM_BLOCKS].left; // start with first block
   while (tree_curr != NUM_BLOCKS) // until reach the root again
   {
//      cout << "tree_curr: " << tree_curr << endl;
      if (tree_prev == in_tree[tree_curr].parent)
      {
         contour_add_block(tree_curr);
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
}
// --------------------------------------------------------
void BTree::contour_add_block(const int tree_ptr)
{
   int tree_parent = in_tree[tree_ptr].parent;
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
   maxCTL = in_contour[contour_ptr].CTL;

   int block = in_tree[tree_ptr].block_index;
   int theta = in_tree[tree_ptr].orient;
   in_contour[tree_ptr].end =
      in_contour[tree_ptr].begin + in_blockinfo[block].width[theta];

   while (in_contour[contour_ptr].end <=
          in_contour[tree_ptr].end + TOLERANCE)
   {
      maxCTL = max(maxCTL, in_contour[contour_ptr].CTL);
      contour_ptr = in_contour[contour_ptr].next;
   }

   if (in_contour[contour_ptr].begin + TOLERANCE < in_contour[tree_ptr].end)
      maxCTL = max(maxCTL, in_contour[contour_ptr].CTL);
   
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
}
// --------------------------------------------------------
void BTree::save_bbb(const string& filename) const
{
   ofstream outfile;
   outfile.open(filename.c_str());
   if (!outfile.good())
   {
      cout << "ERROR: cannot open file" << filename << endl;
      exit(1);
   }

   outfile.setf(ios::fixed);
   outfile.precision(3);

   outfile << in_totalWidth << endl;
   outfile << in_totalHeight << endl;
   outfile << NUM_BLOCKS << endl;
   for (int i = 0; i < NUM_BLOCKS; i++)
      outfile << in_width[i] << " " << in_height[i] << endl;
   outfile << endl;

   for (int i = 0; i < NUM_BLOCKS; i++)
      outfile << in_xloc[i] << " " << in_yloc[i] << endl;
   outfile << endl;
}
// --------------------------------------------------------
      

   
