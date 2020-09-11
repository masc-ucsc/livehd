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
#ifndef BTREE_H
#define BTREE_H

#include "basepacking.hpp"

#include <iostream>
#include <vector>
using namespace std;

// --------------------------------------------------------
class BTree
{
public:
   BTree(const HardBlockInfoType& blockinfo);
   BTree(const HardBlockInfoType& blockinfo, double nTolerance);
   inline BTree(const BTree& newBTree);
   inline bool operator =(const BTree& newBTree); // true if succeeds

   class BTreeNode;
   class ContourNode;
   const vector<BTreeNode>& tree;
   const vector<ContourNode>& contour;

   inline const vector<double>& xloc() const;
   inline const vector<double>& yloc() const;
   inline double xloc(int index) const;
   inline double yloc(int index) const;
   inline double width(int index) const;
   inline double height(int index) const;
   
   inline double blockArea() const;
   inline double totalArea() const;
   inline double totalWidth() const;
   inline double totalHeight() const;

   inline void setTree(const vector<BTreeNode>& ntree);
   void evaluate(const vector<BTreeNode>& ntree); 
   void evaluate(const vector<int>& tree_bits,    // assume the lengths of
                 const vector<int>& perm,         // these 3 are compatible 
                 const vector<int>& orient);      // with size of old tree
   static void bits2tree(const vector<int>& tree_bits, // assume lengths of 
                         const vector<int>& perm,      // these 3 compatible
                         const vector<int>& orient,
                         vector<BTreeNode>& ntree);
   inline static void clean_tree(vector<BTreeNode>& otree);
   inline static void clean_contour(vector<ContourNode>& oContour);

   // perturb the tree, evaluate contour from scratch
   enum MoveType {SWAP, ROTATE, MOVE};
   
   void swap(int indexOne, int indexTwo); 
   inline void rotate(int index, int newOrient);
   void move(int index, int target, bool leftChild); // target = 0..n

   const int NUM_BLOCKS;
   static const int UNDEFINED; // = basepacking_h::Dimension::UNDEFINED;

   class BTreeNode
   {
   public:
      int parent;
      int left;
      int right;
      
      int block_index;
      int orient;
   };

   class ContourNode
   {
   public:
      int next;
      int prev;
      
      double begin;
      double end;
      double CTL;
   };

   // -----output functions-----
   void save_bbb(const string& filename) const;
   
protected:
   const HardBlockInfoType& in_blockinfo;
   vector<BTreeNode> in_tree;
   vector<ContourNode> in_contour;

   // blah[i] refers the attribute of in_tree[i].
   vector<double> in_xloc;
   vector<double> in_yloc;
   vector<double> in_width;
   vector<double> in_height;

   double in_blockArea;
   double in_totalArea;
   double in_totalWidth;
   double in_totalHeight;

   const double TOLERANCE;

   void contour_evaluate();
   void contour_add_block(int treePtr);

   void swap_parent_child(int parent, bool isLeft);
   void remove_left_up_right_down(int index);
};   
// --------------------------------------------------------
class BTreeOrientedPacking : public OrientedPacking
{
public:
   inline BTreeOrientedPacking() {}
   inline BTreeOrientedPacking(const BTree& bt);

   inline void operator =(const BTree& bt);
};
// --------------------------------------------------------

// =========================
//      Implementations
// =========================
inline BTree::BTree(const BTree& newBtree)
   : tree(in_tree),
     contour(in_contour),
     NUM_BLOCKS(newBtree.NUM_BLOCKS),
     in_blockinfo(newBtree.in_blockinfo),
     in_tree(newBtree.in_tree),
     in_contour(newBtree.in_contour),
     
     in_xloc(newBtree.in_xloc),
     in_yloc(newBtree.in_yloc),
     in_width(newBtree.in_width),
     in_height(newBtree.in_height),
     
     in_blockArea(newBtree.in_blockArea),
     in_totalArea(newBtree.in_totalArea),
     in_totalWidth(newBtree.in_totalWidth),
     in_totalHeight(newBtree.in_totalHeight),

     TOLERANCE(newBtree.TOLERANCE)
{}
// --------------------------------------------------------
inline bool BTree::operator =(const BTree& newBtree)
{
   if (NUM_BLOCKS == newBtree.NUM_BLOCKS)
   {
      in_tree = newBtree.in_tree;
      in_contour = newBtree.in_contour;

      in_xloc = newBtree.in_xloc;
      in_yloc = newBtree.in_yloc;
      in_width = newBtree.in_width;
      in_height = newBtree.in_height;

      in_blockArea = newBtree.in_blockArea;
      in_totalArea = newBtree.in_totalArea;
      in_totalWidth = newBtree.in_totalWidth;
      in_totalHeight = newBtree.in_totalHeight;
      
      return true;
   }
   else
      return false;
}
// --------------------------------------------------------
inline const vector<double>& BTree::xloc() const
{  return in_xloc; }
// --------------------------------------------------------
inline const vector<double>& BTree::yloc() const
{  return in_yloc; }
// --------------------------------------------------------
inline double BTree::xloc(int index) const
{  return in_xloc[index]; }
// --------------------------------------------------------
inline double BTree::yloc(int index) const
{  return in_yloc[index]; }
// --------------------------------------------------------
inline double BTree::width(int index) const
{  return in_width[index]; }
// --------------------------------------------------------
inline double BTree::height(int index) const
{  return in_height[index]; }
// --------------------------------------------------------
inline double BTree::blockArea() const
{  return in_blockArea; }
// --------------------------------------------------------
inline double BTree::totalArea() const
{  return in_totalArea; }
// --------------------------------------------------------
inline double BTree::totalWidth() const
{  return in_totalWidth; }
// --------------------------------------------------------
inline double BTree::totalHeight() const
{  return in_totalHeight; }
// --------------------------------------------------------
inline void BTree::setTree(const vector<BTreeNode>& ntree)
{   in_tree = ntree; }
// --------------------------------------------------------
inline void BTree::clean_tree(vector<BTreeNode>& otree)
{
   int vec_size = otree.size();
   for (int i = 0; i < vec_size; i++)
   {
      otree[i].parent = UNDEFINED;
      otree[i].left = UNDEFINED;
      otree[i].right = UNDEFINED;
   }
   otree[vec_size-2].block_index = vec_size-2;
   otree[vec_size-1].block_index = vec_size-1;

   otree[vec_size-2].orient = UNDEFINED;
   otree[vec_size-1].orient = UNDEFINED;
}
// --------------------------------------------------------
inline void BTree::clean_contour(vector<ContourNode>& oContour)
{
   int vec_size = oContour.size();
   int Ledge = vec_size-2;
   int Bedge = vec_size-1;

   oContour[Ledge].next = Bedge;
   oContour[Ledge].prev = UNDEFINED;
   oContour[Ledge].begin = 0;
   oContour[Ledge].end = 0;
   oContour[Ledge].CTL = basepacking_h::Dimension::INFTY;

   oContour[Bedge].next = UNDEFINED;
   oContour[Bedge].prev = Ledge;
   oContour[Bedge].begin = 0;
   oContour[Bedge].end = basepacking_h::Dimension::INFTY;
   oContour[Bedge].CTL = 0;
}
// --------------------------------------------------------
inline void BTree::rotate(int index,
                          int newOrient)
{
   in_tree[index].orient = newOrient;
   contour_evaluate();
}
// ========================================================
inline BTreeOrientedPacking::BTreeOrientedPacking(const BTree& bt)
{
   int blocknum = bt.NUM_BLOCKS;

   xloc.resize(blocknum);
   yloc.resize(blocknum);
   width.resize(blocknum);
   height.resize(blocknum);
   orient.resize(blocknum);
   for (int i = 0; i < blocknum; i++)
   {
      xloc[i] = bt.xloc(i);
      yloc[i] = bt.yloc(i);
      width[i] = bt.width(i);
      height[i] = bt.height(i);
      orient[i] = OrientedPacking::ORIENT(bt.tree[i].orient);
   }
}
// --------------------------------------------------------
inline void BTreeOrientedPacking::operator =(const BTree& bt)
{
   int blocknum = bt.NUM_BLOCKS;

   xloc.resize(blocknum);
   yloc.resize(blocknum);
   width.resize(blocknum);
   height.resize(blocknum);
   orient.resize(blocknum);
   for (int i = 0; i < blocknum; i++)
   {
      xloc[i] = bt.xloc(i);
      yloc[i] = bt.yloc(i);
      width[i] = bt.width(i);
      height[i] = bt.height(i);
      orient[i] = OrientedPacking::ORIENT(bt.tree[i].orient);
   }
}  
// ========================================================

#endif
