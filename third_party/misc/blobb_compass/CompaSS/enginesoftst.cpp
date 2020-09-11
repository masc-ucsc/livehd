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
#include "enginesoftst.hpp"
#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"
#include "boundsoftst.hpp"
//#include "debug.hpp"

#include <queue>
#include <float.h>
#include <iomanip>
#include <cmath>
using namespace std;

const double DEFAULT_OPT_INIT_DEADSPACE = 0.05;
const double DEFAULT_OPT_DEADSPACE_INCRE = 1.1;

double SoftEngineType::INIT_DEADSPACE = DEFAULT_OPT_INIT_DEADSPACE;
double SoftEngineType::DEADSPACE_INCRE = DEFAULT_OPT_DEADSPACE_INCRE;
// --------------------------------------------------------
SoftEngineType::SoftEngineType(const BlockInfoType& new_blockinfo)
   : sst(new_blockinfo),
     blockinfo(new_blockinfo),
     record_list_ptr(NULL),
     compare(NULL),
     operandProceed(NULL),
     operatorProceed(NULL)
{
   int blocknum = new_blockinfo.BLOCK_NUM();

   blkBefore.resize(blocknum);
   same.resize(blocknum);
   for (int i = 0; i < blocknum; i++)
   {
      blkBefore[i] = 0;
      same[i].resize(blocknum);
      for (int j = 0; j < i; j++)
         if (blockinfo[j] == blockinfo[i])
         {
            same[i][j] = true;
            same[j][i] = true;
            blkBefore[i]++;
         }
         else
         {
            same[i][j] = false;
            same[j][i] = false;
         }
      same[i][i] = true;
   }
   
   cout << "simiarity table: " << endl;
   for (int i = 0; i < blockinfo.BLOCK_NUM(); i++)
   {
      cout << setw(11) << ' ';
      for (int j = 0; j < blockinfo.BLOCK_NUM(); j++)
         cout << ((same[i][j])? "T " : "- ");
      cout << endl;
   }

   cout << "blkBefore: ";
   for (int i = 0; i < blockinfo.BLOCK_NUM(); i++)
      cout << blkBefore[i] << " ";
   cout << endl << endl;   
}
// --------------------------------------------------------
SoftEngineType::SoftEngineType(const BlockInfoType& new_blockinfo,
                               const BlockCompareType similar)
   : sst(new_blockinfo),
     blockinfo(new_blockinfo),
     record_list_ptr(NULL),
     compare(NULL),
     operandProceed(NULL),
     operatorProceed(NULL)
{
   int blocknum = new_blockinfo.BLOCK_NUM();

   blkBefore.resize(blocknum);
   same.resize(blocknum);
   for (int i = 0; i < blocknum; i++)
   {
      blkBefore[i] = 0;
      same[i].resize(blocknum);
      for (int j = 0; j < i; j++)
         if (similar(blockinfo[j], blockinfo[i]))
         {
            same[i][j] = true;
            same[j][i] = true;
            blkBefore[i]++;
         }
         else
         {
            same[i][j] = false;
            same[j][i] = false;
         }
      same[i][i] = true;
   }
}
// --------------------------------------------------------
SoftSliceRecordList* SoftEngineType::operator ()(int algo,
                                                 int mode)
{
   if (mode != SoftEngineType::EXPLICIT &&
       mode != SoftEngineType::IMPLICIT)
   {
      cout << "ERROR: the requested mode is not available." << endl;
      exit(0);
   }
   
   switch (algo)
   {
   case BRUTE:
      HandleBrute(mode);
      break;

   case B_BOUND:
      HandleBranchBound(mode);
      break;

   case HIER:
      HandleHierarchical(mode);
      break;      

   default:
      cout << "ERROR: the requested algo is not available." << endl;
      exit(0);
      break;
   }
   
   return record_list_ptr;
}
// --------------------------------------------------------
void SoftEngineType::FindSoftSliceExplicit()
{
   int qSize = bCont.size();

   if ((qSize == 0) && (sst.balance() == 1))
   {
      if (compare(sst, record_list_ptr->last()))
      {
         record_list_ptr->add_record(sst);

         static int count = 0;
         double area = record_list_ptr->last().minArea;
         double deadspace = record_list_ptr->last().minDeadspace;
         cout << "[" << count << "]: ";
         for (int i = 0; i < sst.expression_size(); i++)
            if (sst.expression(i) == SoftSTree::PLUS)
               cout << "+ ";
            else if (sst.expression(i) == SoftSTree::STAR)
               cout << "* ";
            else
               cout << sst.expression(i) << " ";
         cout.setf(ios::fixed);
         cout.precision(2);
         cout << " area: " << area;
         cout << " (" << ((deadspace / (area-deadspace)) * 100) << "%)";
         cout << " " << getTotalTime();
         cout << " size: " << sst.buffer(0).boundary.size();
         cout << endl;
         count++;
      }              
      return;
   }

   for (int i = 0; i < qSize; i++)
   {
      int blk = bCont.front();
      bCont.pop();

      sst.push_operand(blk);

      counter[sst.expression_size()][0]++;
      counter[sst.expression_size()][1]++;
      if (operandProceed(same, blkBefore, sst, record_list_ptr->last()))
         FindSoftSliceExplicit();

      sst.pop_operand();
      bCont.push(blk);
   }

   for (int j = SoftSTree::PLUS; j >= SoftSTree::STAR; j--)
      if (sst.can_push_operator(j))
      {
         sst.push_operator(j);

         counter[sst.expression_size()][0]++;
         counter[sst.expression_size()][3]++;
         if (operatorProceed(sst, record_list_ptr->last()))
            FindSoftSliceExplicit();

         sst.pop_operator();
      }         
}
// --------------------------------------------------------
void SoftEngineType::FindSoftSliceImplicit()
{
   int qSize = bCont.size();
   if ((qSize == 0) && (sst.balance() == 1))
   {
      if (compare(sst, record_list_ptr->last()))
      {
         record_list_ptr->add_record(sst);

         static int count = 0;
         double area = record_list_ptr->last().minArea;
         double deadspace = record_list_ptr->last().minDeadspace;
         cout << "[" << count << "]: ";
         for (int i = 0; i < sst.expression_size(); i++)
            if (sst.expression(i) == SoftSTree::PLUS)
               cout << "+ ";
            else if (sst.expression(i) == SoftSTree::STAR)
               cout << "* ";
            else if (sst.expression(i) == SoftSTree::BOTH)
               cout << "- ";
            else
               cout << sst.expression(i) << " ";
         cout.setf(ios::fixed);
         cout.precision(2);
         cout << " area: " << area;
         cout << " (" << ((deadspace / (area-deadspace)) * 100) << "%)";
         cout << " " << getTotalTime();
         cout << " size: " << sst.buffer(0).boundary.size();
         cout << endl;
         count++;
      }              
      return;
   }

   for (int i = 0; i < qSize; i++)
   {
      int blk = bCont.front();
      bCont.pop();

      sst.push_operand(blk);

      counter[sst.expression_size()][0]++;
      counter[sst.expression_size()][1]++;
      if (operandProceed(same, blkBefore, sst, record_list_ptr->last()))
         FindSoftSliceImplicit();

      sst.pop_operand();
      bCont.push(blk);
   }

   if (sst.balance() > 1)
   {
      sst.push_operator(SoftSTree::BOTH);
      
      counter[sst.expression_size()][0]++;
      counter[sst.expression_size()][4]++;
      if (operatorProceed(sst, record_list_ptr->last()))
         FindSoftSliceImplicit();

      sst.pop_operator();
   }
}
// --------------------------------------------------------
void SoftEngineType::HandleBrute(int mode)
{
   for (int i = 1; i < blockinfo.BLOCK_NUM(); i++)
         bCont.push(i);
   
   compare = BranchBoundCompare;
   operandProceed = BruteProceed;
   operatorProceed = BruteProceed;
   record_list_ptr = new SoftSliceRecordList(DBL_MAX, DBL_MAX);

   sst.push_operand(0);
   if (mode == SoftEngineType::EXPLICIT)
      FindSoftSliceExplicit();
   else if (mode == SoftEngineType::IMPLICIT)
      FindSoftSliceImplicit();        
   sst.pop_operand();
}      
// --------------------------------------------------------
void SoftEngineType::HandleBranchBound(int mode)
{
   double init_totalArea = -1;
   double init_deadspace = -1;
   
   for (int i = 1; i < blockinfo.BLOCK_NUM(); i++)
      bCont.push(i);
      
   compare = BranchBoundCompare;
   operandProceed = BranchBoundProceed;

   init_deadspace = sst.total_area() * INIT_DEADSPACE;
   init_totalArea = sst.total_area() + init_deadspace;
   record_list_ptr = new SoftSliceRecordList(init_totalArea, init_deadspace);

   InitializeCounter();

   while (record_list_ptr->empty())
   {
      cout << "Considering solutions with no more than "
           << ((*record_list_ptr)[0].minDeadspace / sst.total_area()) * 100
           << "% deadspace" << endl;
      sst.push_operand(0);         
      if (mode == SoftEngineType::EXPLICIT)
      {
         operatorProceed = BranchBoundExplicitProceed;
         FindSoftSliceExplicit();
      }
      else if (mode == SoftEngineType::IMPLICIT)
      {
         operatorProceed = BranchBoundImplicitProceed;
         FindSoftSliceImplicit();
      }
      sst.pop_operand();
      record_list_ptr->set_deadspace((*record_list_ptr)[0].minDeadspace
                                     * DEADSPACE_INCRE);
   }

   OutputCounter(cout, 5, 2*blockinfo.BLOCK_NUM());
}
// --------------------------------------------------------
