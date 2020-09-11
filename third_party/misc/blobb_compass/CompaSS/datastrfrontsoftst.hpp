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
#ifndef DATASTRFRONTSOFTST_H
#define DATASTRFRONTSOFTST_H

#include "datastrbacksoftst.hpp"

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <float.h>
#include <limits>
using namespace std;

class SoftCluster;
class SoftClusterSet;

const int DEFAULT_CURVE_ACCURACY = 200;
// --------------------------------------------------------
class BlockInfoType
{
public:
   BlockInfoType(istream& ins);
   BlockInfoType(int format, istream& ins);
   BlockInfoType(int format, int accuracy, istream& ins);
   BlockInfoType(const SoftClusterSet& clusterSet);
   BlockInfoType(const BlockInfoType& bit)
      : blocks(bit.blocks), in_total_area(bit.in_total_area) {}

   inline const BoundaryType& operator [](int index) const
      {  return blocks[index]; }

   inline int size() const
      {  return blocks.size(); }

   inline int BLOCK_NUM() const
      {  return blocks.size(); }

   inline double total_area() const
      {  return in_total_area; }

   inline double block_area() const
      {  return in_total_area; }
   
   enum FormatType {SOFT = 0, TXT, TXT_FIXED};
   static int CURVE_ACCURACY;      
   
private:
   vector<BoundaryType> blocks;
   double in_total_area;

   void ReadSoftFormat(int accuracy, istream& ins);
   void ReadTxtFormat(istream& ins);
   void ReadTxtFixedFormat(istream& ins);
};
// --------------------------------------------------------
class SoftSTree 
{
public:
   SoftSTree(const BlockInfoType& blockinfo) 
      : in_balance(0), in_perSize(0), in_deadspace(0),
        in_blkArea(blockinfo.total_area())
      {
         in_expression.reserve(2*blockinfo.BLOCK_NUM());
         in_buffer.reserve(blockinfo.BLOCK_NUM());
         in_storage.reserve(2*blockinfo.BLOCK_NUM());

         int blockinfo_size = blockinfo.size();
         for (int i = 0; i < blockinfo_size; i++)
            BLOCK_NODES.push_back(SoftNode(i, blockinfo[i]));
      }

   enum OprType {STAR = BoundaryType::STAR,
                 PLUS = BoundaryType::PLUS,
                 BOTH = BoundaryType::BOTH,
                 FLIP_OR = BoundaryType::FLIP_OR};

   inline static bool isOperand(int sign)
      {   return (sign >= 0); }

   inline static bool isOperator(int sign)
      {   return ((sign == PLUS) ||
                  (sign == STAR) ||
                  (sign == BOTH)); }

   friend class Debug;
   
   inline int expression_size() const
      {  return in_expression.size(); }
   inline int expression_top() const
      {  return in_expression.back(); }
   inline const vector<int>& expression() const
      {  return in_expression; }
   inline int expression(int i) const
      {  return in_expression[i]; }

   inline int buffer_size() const
      {  return in_buffer.size(); }
   inline const SoftNode& buffer_top() const
      {  return *(in_buffer.back()); }
   inline const SoftNode& buffer(int i) const
      {  return *(in_buffer[i]); }

   inline int storage_size() const
      {  return in_storage.size(); }
   inline const SoftNode& storage_top() const
      {  return *(in_storage.back()); }
   inline const SoftNode& storage(int i) const
      {  return *(in_storage[i]); }

   inline int balance() const
      {  return in_balance; }

   inline int perSize() const
      {  return in_perSize; }

   inline double deadspace() const
      {  return in_deadspace; }

   inline double total_area() const
      {  return in_blkArea + in_deadspace; }
   
   inline void push_operand(int blk)
      {
         in_expression.push_back(blk);
         in_buffer.push_back(&(BLOCK_NODES[blk]));
         in_balance++;
         in_perSize++;
      }
   
   inline void pop_operand()
      {
         in_perSize--;
         in_balance--;
         in_buffer.pop_back();
         in_expression.pop_back();
      }   

   inline bool can_push_operator(int sign) const
      {  return ((in_balance > 1) &&
                 (sign != in_expression.back())); }

   void push_operator(int sign);
   void push_operator(int sign, double width_limit, double height_limit);
   void pop_operator();

private:
   int in_balance;      // # operands - # operators
   int in_perSize;      // # operands
   double in_deadspace; // overall deadspace
   double in_blkArea;   // total block area

   vector<int> in_expression;   // the NPE
   vector<SoftNode*> in_buffer; 
   vector<SoftNode*> in_storage;

   vector<SoftNode> BLOCK_NODES; // used in push_operand()
   
   SoftSTree(const SoftSTree&);
   void operator =(const SoftSTree);   
};
// --------------------------------------------------------
class SoftSliceRecord
{
public:
   SoftSliceRecord()
      : minArea(0), minDeadspace(0),
        boundary(vector<Point>(3, Point(0,0))) {}
                 
   SoftSliceRecord(const SoftSTree& sst)
      : minArea(sst.total_area()), minDeadspace(sst.deadspace()),
        expression(sst.expression()),
        boundary(sst.buffer(0).boundary) {}

   SoftSliceRecord(double area, double deadspace)
      : minArea(area), minDeadspace(deadspace),
        boundary(vector<Point>(3, Point(0, 0))) {}

   SoftSliceRecord(const SoftCluster& cluster);      
      
   SoftSliceRecord(const SoftSliceRecord& ssr)
      : minArea(ssr.minArea), minDeadspace(ssr.minDeadspace),
        expression(ssr.expression),
        boundary(ssr.boundary) {}

   double minArea;
   double minDeadspace;

   vector<int> expression;
   BoundaryType boundary;

   inline void operator =(const SoftSliceRecord& ssr)
      {
         minArea = ssr.minArea;
         minDeadspace = ssr.minDeadspace;
         expression = ssr.expression;
         boundary = ssr.boundary;
      }
};
// --------------------------------------------------------
class SoftSliceRecordList
{
public:
   SoftSliceRecordList(double deft_area, double deft_deadspace)
      : in_list(1, SoftSliceRecord(deft_area, deft_deadspace)) {}

   inline bool empty() const
      {  return in_list.size() == 1; }

   inline void add_record(const SoftSTree& sst)
      {  in_list.push_back(SoftSliceRecord(sst)); }

   inline void set_deadspace(double deadspace)
      {
         double original = in_list[0].minArea - in_list[0].minDeadspace;
         in_list[0].minArea = original + deadspace;
         in_list[0].minDeadspace = deadspace;
      }
         
   inline const SoftSliceRecord& operator [](int index) const
      {  return in_list[index]; }

   inline const SoftSliceRecord& last() const
      {  return in_list[in_list.size()-1]; }

private:
   vector<SoftSliceRecord> in_list;
};
// --------------------------------------------------------
class SoftPacking
{
public:
   SoftPacking() {}
   SoftPacking(const SoftSliceRecord& ssr,
               const BlockInfoType& blockinfo)
      : xloc((ssr.expression.size()+1) / 2),
        yloc((ssr.expression.size()+1) / 2),
        width((ssr.expression.size()+1) / 2),
        height((ssr.expression.size()+1) / 2),
        expression(ssr.expression)
      {   Evaluate(blockinfo); }
         
   SoftPacking(const SoftPacking& spk)
      : deadspace(spk.deadspace), blockArea(spk.blockArea),
        totalWidth(spk.totalWidth), totalHeight(spk.totalHeight),
        xloc(spk.xloc), yloc(spk.yloc),
        width(spk.width), height(spk.height),
        expression(spk.expression) {}

   inline void operator =(const SoftPacking& spk)
      {
         deadspace = spk.deadspace;
         blockArea = spk.blockArea;
         totalWidth = spk.totalWidth;
         totalHeight = spk.totalHeight;
         xloc = spk.xloc;
         yloc = spk.yloc;
         width = spk.width;
         height = spk.height;
         expression = spk.expression;
      }

   double deadspace;
   double blockArea;
   double totalWidth;
   double totalHeight;

   vector<double> xloc;
   vector<double> yloc;
   vector<double> width;
   vector<double> height;
   vector<int> expression;

   void output(ostream& outs) const;
   
protected:
   struct TreeNode
   {
      TreeNode()
         : width(0), height(0), xloc(-1), yloc(-1),
           sign(-1), parent(-1), left(-1), right(-1),
           bdy_ptr(NULL) {}
      
      TreeNode(const TreeNode& tn)
         : width(tn.width), height(tn.height),
           xloc(tn.xloc), yloc(tn.yloc),
           sign(tn.sign), parent(tn.parent),
           left(tn.left), right(tn.right),
           bdy_ptr(NULL)
         {
            if (SoftSTree::isOperator(sign))
               bdy_ptr = new BoundaryType(*tn.bdy_ptr);
            else
               bdy_ptr = tn.bdy_ptr;
         }
      
      ~TreeNode()
         {
            if (SoftSTree::isOperator(sign))
               delete bdy_ptr;
         }
      
      double width;
      double height;
      double xloc;
      double yloc;

      int sign;
      int parent;
      int left;
      int right;
      const BoundaryType *bdy_ptr;
   };
   
   void Evaluate(const BlockInfoType& blockinfo);
   void ConstructTree(vector<TreeNode>& tree,
                      int& tree_ptr, int& expr_ptr, int parent);
   void AssignBoundaries(const BlockInfoType& blockinfo,
                         vector<TreeNode>& tree);
   void EvaluateTree(vector<TreeNode>& tree, int tree_ptr,
                     double bound_length);
   int getSign(const vector<TreeNode>& tree, int tree_ptr,
               double boxWidth, double boxHeight) const;
   void ExpressionFromTree(const vector<TreeNode>& tree);
};
// --------------------------------------------------------

#endif

