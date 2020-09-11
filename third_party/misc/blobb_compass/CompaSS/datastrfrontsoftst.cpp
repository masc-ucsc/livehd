/**************************************************************************
***    
*** Copyright (c) 2004 Regents of the University of Michigan,
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
#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
using namespace std;

// ========================================================
int BlockInfoType::CURVE_ACCURACY = DEFAULT_CURVE_ACCURACY;
// --------------------------------------------------------
BlockInfoType::BlockInfoType(istream& ins)
   : in_total_area(0)
{
   ReadTxtFormat(ins);
}       
// --------------------------------------------------------   
BlockInfoType::BlockInfoType(int format,
                             istream& ins)
   : in_total_area(0)
{
   switch (format)
   {
   case SOFT:
      ReadSoftFormat(CURVE_ACCURACY, ins);
      break;

   case TXT:
      ReadTxtFormat(ins);
      break;

   case TXT_FIXED:
      ReadTxtFixedFormat(ins);
      break;
      
   default:
      cout << "ERROR: invalid input file format. " << endl;
      exit(1);
      break;
   }
   sort(blocks.begin(), blocks.end());
}       
// --------------------------------------------------------
BlockInfoType::BlockInfoType(int format,
                             int accuracy,
                             istream& ins)
   : in_total_area(0)
{
   CURVE_ACCURACY = accuracy;
   switch (format)
   {
   case SOFT:
      ReadSoftFormat(accuracy, ins);
      break;

   case TXT:
      ReadTxtFormat(ins);
      break;

   case TXT_FIXED:
      ReadTxtFixedFormat(ins);
      break;

   default:
      cout << "ERROR: invalid input file format. " << endl;
      exit(1);
      break;
   }
   sort(blocks.begin(), blocks.end());
}
// ========================================================
void SoftSTree::push_operator(int sign)
{
   in_expression.push_back(sign);

   SoftNode *TRNodePtr = in_buffer.back();
   in_buffer.pop_back();
   
   SoftNode *BLNodePtr = in_buffer.back();
   in_buffer.pop_back();

   in_storage.push_back(TRNodePtr);
   in_storage.push_back(BLNodePtr);

   SoftNode *temp = new SoftNode(sign, *BLNodePtr, *TRNodePtr);
   in_buffer.push_back(temp);

   in_deadspace += (in_buffer.back())->minDeadspace -
      TRNodePtr->minDeadspace - BLNodePtr->minDeadspace;

   in_balance--;
}
// --------------------------------------------------------
void SoftSTree::push_operator(int sign,
                              double width_limit,
                              double height_limit)
{
   in_expression.push_back(sign);

   SoftNode *TRNodePtr = in_buffer.back();
   in_buffer.pop_back();
   
   SoftNode *BLNodePtr = in_buffer.back();
   in_buffer.pop_back();

   in_storage.push_back(TRNodePtr);
   in_storage.push_back(BLNodePtr);

   SoftNode *temp = new SoftNode(sign, *BLNodePtr, *TRNodePtr,
                                 width_limit, height_limit);
   in_buffer.push_back(temp);

   in_deadspace += (in_buffer.back())->minDeadspace -
      TRNodePtr->minDeadspace - BLNodePtr->minDeadspace;

   in_balance--;
}
// --------------------------------------------------------
void SoftSTree::pop_operator()
{
   in_balance++;

   double oldDeadspace = (in_buffer.back())->minDeadspace;
   delete in_buffer.back();
   in_buffer.pop_back();

   double BLDeadspace = (in_storage.back())->minDeadspace;
   in_buffer.push_back(in_storage.back());
   in_storage.pop_back();

   double TRDeadspace = (in_storage.back())->minDeadspace;
   in_buffer.push_back(in_storage.back());
   in_storage.pop_back();

   in_expression.pop_back();
   in_deadspace += BLDeadspace + TRDeadspace - oldDeadspace;
}
// ========================================================
void SoftPacking::Evaluate(const BlockInfoType& blockinfo)
{
   int expr_size = expression.size();
   vector<TreeNode> tree(expr_size);
   tree.resize(expr_size);

   int tree_ptr = 0;
   int expr_ptr = expr_size - 1;

   ConstructTree(tree, tree_ptr, expr_ptr, -1);
   AssignBoundaries(blockinfo, tree);

   double boxWidth = (tree[0].bdy_ptr)->min_point().xCoord();
   double boxHeight = (tree[0].bdy_ptr)->min_point().yCoord();
   tree[0].xloc = 0;
   tree[0].yloc = 0;
   tree[0].width = 0;
   tree[0].height = 0;

   if (tree[0].sign == SoftSTree::BOTH)
      tree[0].sign = getSign(tree, 0, boxWidth, boxHeight);
      
   switch (tree[0].sign)
   {
   case SoftSTree::PLUS:
      EvaluateTree(tree, tree[0].left, boxWidth);

      tree[0].height = tree[tree[0].left].height;
      EvaluateTree(tree, tree[0].right, boxWidth);
      break;

   case SoftSTree::STAR:
      EvaluateTree(tree, tree[0].left, boxHeight);

      tree[0].width = tree[tree[0].left].width;
      EvaluateTree(tree, tree[0].right, boxHeight);
      break;
   }      

   tree[0].width = boxWidth;
   tree[0].height = boxHeight;

   totalWidth = boxWidth;
   totalHeight = boxHeight;
   blockArea = blockinfo.total_area();
   deadspace = (totalWidth * totalHeight) - blockArea;

   ExpressionFromTree(tree);
}
// --------------------------------------------------------
void SoftPacking::ConstructTree(vector<TreeNode>& tree,
                                int& tree_ptr,
                                int& expr_ptr,
                                int parent)
{
   int sign = expression[expr_ptr];
   int curr_pos = tree_ptr;

   tree[curr_pos].sign = sign;
   tree[curr_pos].parent = parent;
   tree_ptr++;
   expr_ptr--;

   if (SoftSTree::isOperator(sign))
   {
      tree[curr_pos].right = tree_ptr;
      ConstructTree(tree, tree_ptr, expr_ptr, curr_pos);   
      
      tree[curr_pos].left = tree_ptr;
      ConstructTree(tree, tree_ptr, expr_ptr, curr_pos);
   }
   else
   {
      tree[curr_pos].left = -1;
      tree[curr_pos].right = -1;
   }
}
// --------------------------------------------------------
void SoftPacking::AssignBoundaries(const BlockInfoType& blockinfo,
                                   vector<TreeNode>& tree)
{
   for (int tree_ptr = tree.size()-1 ; tree_ptr >= 0; tree_ptr--)
   {
      int sign = tree[tree_ptr].sign;
      if (SoftSTree::isOperator(sign))
      {
         const BoundaryType *left_bd = tree[tree[tree_ptr].left].bdy_ptr;
         const BoundaryType *right_bd = tree[tree[tree_ptr].right].bdy_ptr;
         
         tree[tree_ptr].bdy_ptr = new BoundaryType(sign,
                                                   *left_bd, *right_bd);
      }
      else
         tree[tree_ptr].bdy_ptr = &(blockinfo[sign]);
   }
}
// --------------------------------------------------------
void SoftPacking::EvaluateTree(vector<TreeNode>& tree,
                               int tree_ptr,
                               double bound_length)
{
   int sign = tree[tree_ptr].sign;
   int parent = tree[tree_ptr].parent;
   int parent_sign = tree[parent].sign;
   double boxWidth = -1;
   double boxHeight = -1;

   tree[tree_ptr].xloc = tree[parent].xloc + tree[parent].width;
   tree[tree_ptr].yloc = tree[parent].yloc + tree[parent].height;

   switch (parent_sign)
   {
   case SoftSTree::PLUS:
      boxWidth = bound_length; 
      boxHeight = (tree[tree_ptr].bdy_ptr)->getY(boxWidth);
      boxWidth = (tree[tree_ptr].bdy_ptr)->getX(boxHeight);
      break;
      
   case SoftSTree::STAR:
      boxHeight = bound_length;
      boxWidth = (tree[tree_ptr].bdy_ptr)->getX(boxHeight);
      boxHeight = (tree[tree_ptr].bdy_ptr)->getY(boxWidth);
      break;
   }

   if (sign == SoftSTree::BOTH)
   {
      sign = getSign(tree, tree_ptr, boxWidth, boxHeight);
      tree[tree_ptr].sign = sign;
   }
   
   switch (sign)
   {
   case SoftSTree::PLUS:
      tree[tree_ptr].height = 0;      
      EvaluateTree(tree, tree[tree_ptr].left, boxWidth);

      tree[tree_ptr].height = tree[tree[tree_ptr].left].height;
      EvaluateTree(tree, tree[tree_ptr].right, boxWidth);
      break;

   case SoftSTree::STAR:
      tree[tree_ptr].width = 0;
      EvaluateTree(tree, tree[tree_ptr].left, boxHeight);

      tree[tree_ptr].width = tree[tree[tree_ptr].left].width;
      EvaluateTree(tree, tree[tree_ptr].right, boxHeight);
      break;
   }

   tree[tree_ptr].width = boxWidth;
   tree[tree_ptr].height = boxHeight;

   if ((sign != SoftSTree::PLUS) &&
       (sign != SoftSTree::STAR))
   {
      xloc[sign] = tree[tree_ptr].xloc;
      yloc[sign] = tree[tree_ptr].yloc;
      width[sign] = tree[tree_ptr].width;
      height[sign] = tree[tree_ptr].height;
   }
}
// --------------------------------------------------------      
int SoftPacking::getSign(const vector<TreeNode>& tree,
                         int tree_ptr,
                         double boxWidth,
                         double boxHeight) const
{
   int left_ptr = tree[tree_ptr].left;
   int right_ptr = tree[tree_ptr].right;

   double star_width
      = ((tree[left_ptr].bdy_ptr)->getX(boxHeight) +
         (tree[right_ptr].bdy_ptr)->getX(boxHeight));
   double star_error = star_width / boxWidth;

   double plus_height
      = ((tree[left_ptr].bdy_ptr)->getY(boxWidth) +
         (tree[right_ptr].bdy_ptr)->getY(boxWidth));
   double plus_error = plus_height / boxHeight;

   if (star_error < plus_error)
      return SoftSTree::STAR;
   else
      return SoftSTree::PLUS;
}
// --------------------------------------------------------
void SoftPacking::ExpressionFromTree(const vector<TreeNode>& tree)
{
   int tree_curr = tree[0].left;
   int tree_prev = 0;

   // post-order traversal of the tree
   int expr_ptr = 0;
   while (tree_curr != -1)
   {
      int sign = tree[tree_curr].sign;
      if (SoftSTree::isOperand(sign))
      {
         expression[expr_ptr] = sign;
         expr_ptr++;

         tree_prev = tree_curr;
         tree_curr = tree[tree_curr].parent;
      }
      else if (tree_prev == tree[tree_curr].right)
      {
         expression[expr_ptr] = sign;
         expr_ptr++;

         tree_prev = tree_curr;
         tree_curr = tree[tree_curr].parent;
      }
      else if (tree_prev == tree[tree_curr].left)
      {
         tree_prev = tree_curr;
         tree_curr = tree[tree_curr].right;
      }
      else if (tree_prev == tree[tree_curr].parent)
      {
         tree_prev = tree_curr;
         tree_curr = tree[tree_curr].left;
      }
   }
}
// ========================================================
