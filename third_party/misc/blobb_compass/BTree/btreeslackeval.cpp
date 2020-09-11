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
#include "btreeslackeval.hpp"
// #include "debug.h"

#include <vector>
#include <algorithm>
using namespace std;

// ---------------------------------------------------------
const vector<double>& BTreeSlackEval::evaluateXSlacks(const BTree& orig_btree)
{
   const int NUM_BLOCKS = orig_btree.NUM_BLOCKS;
   _btree.build_orth_tree();   

   reverse_tree(_btree.orth_tree, _rev_orth_tree);
   _btree.evaluate(_rev_orth_tree);

   double width = orig_btree.totalWidth();
   for (int i = 0; i < NUM_BLOCKS; i++)
   {
      _xlocRight[i] = _btree.yloc(i);
      _xSlack[i] = width -
         orig_btree.width(i) - _xlocRight[i] - orig_btree.xloc(i);
   }
   return _xSlack;
}
// --------------------------------------------------------
const vector<double>& BTreeSlackEval::evaluateYSlacks(const BTree& orig_btree)
{
   reverse_tree(orig_btree.tree, _rev_tree);
   _btree.evaluate(_rev_tree);

   const int NUM_BLOCKS = _btree.NUM_BLOCKS;
   double height = orig_btree.totalHeight();
   for (int i = 0; i < NUM_BLOCKS; i++)
   {
      _ylocTop[i] = _btree.yloc(i);
      _ySlack[i] = height -
         orig_btree.height(i) - _ylocTop[i] - orig_btree.yloc(i);
   }
   return _ySlack;
}
// ---------------------------------------------------------
void BTreeSlackEval::reverse_tree(const vector<BTree::BTreeNode>& tree,
                                  vector<BTree::BTreeNode>& rev_tree)
{
   // assume "rev_tree" has the same size as "tree"
   BTree::clean_tree(rev_tree);
   
   static const int UNDEFINED =  BTree::UNDEFINED;   
   const int NUM_BLOCKS = tree.size() - 2;
   int tree_prev = NUM_BLOCKS;
   int tree_curr = tree[NUM_BLOCKS].left; // start with the first child of root
   vector<int> true_parent(tree.size(), UNDEFINED); // book-keeping variable
   while (tree_curr != NUM_BLOCKS)
   {
      if (tree_prev == tree[tree_curr].parent)
      {
         if (tree_curr == tree[tree_prev].left)
         {
            // left-child
            rev_tree[tree_prev].left = tree_curr;
            rev_tree[tree_curr].parent = tree_prev;
            rev_tree[tree_curr].block_index = tree[tree_curr].block_index;
            rev_tree[tree_curr].orient = tree[tree_curr].orient;

            true_parent[tree_curr] = tree_prev;
         }
         else
         {
            // right-child
            int tree_parent = true_parent[tree_prev];
            rev_tree[tree_parent].left = tree_curr; // prob. overwrite 
            rev_tree[tree_curr].parent = tree_parent;
            rev_tree[tree_curr].block_index = tree[tree_curr].block_index;
            rev_tree[tree_curr].orient = tree[tree_curr].orient;
               
            rev_tree[tree_prev].parent = tree_curr;
            rev_tree[tree_curr].right = tree_prev;

            true_parent[tree_curr] = tree_parent;
         }
         tree_prev = tree_curr;
         if (tree[tree_curr].right != UNDEFINED)
            tree_curr = tree[tree_curr].right;
         else if (tree[tree_curr].left != UNDEFINED)
            tree_curr = tree[tree_curr].left;
         else
            tree_curr = tree[tree_curr].parent;
      }
      else if (tree_prev == tree[tree_curr].right)
      {
         tree_prev = tree_curr;
         tree_curr = (tree[tree_curr].left != UNDEFINED)?
            tree[tree_curr].left : tree[tree_curr].parent; 
      }
      else
      {
         tree_prev = tree_curr;
         tree_curr = tree[tree_curr].parent;
      }
   }
}
// --------------------------------------------------------
