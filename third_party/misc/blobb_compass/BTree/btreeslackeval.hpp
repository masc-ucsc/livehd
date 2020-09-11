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
#ifndef BTREESLACKEVAL_H
#define BTREESLACKEVAL_H

#include "basepacking.hpp"
#include "btree.hpp"
#include "btreecompact.hpp"

#include <vector>
using namespace std;

// --------------------------------------------------------
class BTreeSlackEval 
{
public:
   inline BTreeSlackEval(const BTree& newBTree);

   inline void evaluateSlacks(const BTree& newBTree);
   const vector<double>& evaluateXSlacks(const BTree& orig_btree);
   const vector<double>& evaluateYSlacks(const BTree& orig_btree);

   inline const vector<double>& xSlack() const;
   inline const vector<double>& ySlack() const;

   inline const vector<double>& xlocRight() const; // xloc from Right 
   inline const vector<double>& ylocTop() const;   // yloc from Top
   
   inline const BTreeCompactor& btree() const;
   inline const vector<BTree::BTreeNode>& rev_tree() const;
   inline const vector<BTree::BTreeNode>& rev_orth_tree() const;

   static void reverse_tree(const vector<BTree::BTreeNode>& tree,
                            vector<BTree::BTreeNode>& rev_tree);   

private:
   BTreeCompactor _btree;
   vector<double> _xSlack;
   vector<double> _ySlack;

   vector<double> _xlocRight;
   vector<double> _ylocTop;
   
   vector<BTree::BTreeNode> _rev_tree;
   vector<BTree::BTreeNode> _rev_orth_tree;

   BTreeSlackEval(const BTreeSlackEval&); // copy forbidden
};
// --------------------------------------------------------

// ===============
// IMPLEMENTATIONS
// ===============
BTreeSlackEval::BTreeSlackEval(const BTree& newBTree)
   : _btree(newBTree),
     _xSlack(newBTree.NUM_BLOCKS),
     _ySlack(newBTree.NUM_BLOCKS),
     _xlocRight(newBTree.NUM_BLOCKS),
     _ylocTop(newBTree.NUM_BLOCKS),
     _rev_tree(newBTree.tree.size()),
     _rev_orth_tree(newBTree.tree.size())
{
   BTree::clean_tree(_rev_tree);
   BTree::clean_tree(_rev_orth_tree);   
}
// --------------------------------------------------------
void BTreeSlackEval::evaluateSlacks(const BTree& newBTree)
{
   _btree.slimAssign(newBTree);
   evaluateXSlacks(newBTree); // mess with "_btree" only
   evaluateYSlacks(newBTree);
}
// --------------------------------------------------------
const vector<double>& BTreeSlackEval::xSlack() const
{   return _xSlack; }
// --------------------------------------------------------
const vector<double>& BTreeSlackEval::ySlack() const
{   return _ySlack; }
// --------------------------------------------------------
const BTreeCompactor& BTreeSlackEval::btree() const
{   return _btree; }
// --------------------------------------------------------
const vector<BTree::BTreeNode>& BTreeSlackEval::rev_tree() const
{   return _rev_tree; }
// --------------------------------------------------------
const vector<BTree::BTreeNode>& BTreeSlackEval::rev_orth_tree() const
{   return _rev_orth_tree; }
// --------------------------------------------------------

#endif
