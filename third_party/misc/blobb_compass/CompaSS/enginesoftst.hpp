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
#ifndef ENGINESOFTST_H
#define ENGINESOFTST_H

#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"
#include "boundsoftst.hpp"

#include <queue>
#include <vector>
using namespace std;


// --------------------------------------------------------
typedef bool (*BlockCompareType)(const BoundaryType& b1,
                                 const BoundaryType& b2);
// --------------------------------------------------------
class SoftEngineType
{
public:
   SoftEngineType(const BlockInfoType& new_blockinfo);
   SoftEngineType(const BlockInfoType& new_blockinfo,
                  const BlockCompareType similar);

   SoftSliceRecordList* operator ()(int algo, int mode);
   enum AlgoType {B_BOUND, B_TRACK, ENUM, BRUTE, HIER};
   enum ModeType {EXPLICIT, IMPLICIT};

   static double INIT_DEADSPACE;
   static double DEADSPACE_INCRE;
   
private:
   SoftSTree sst;
   queue<int> bCont;
   BlockInfoType blockinfo;

   vector< vector<bool> > same;
   vector<int> blkBefore;
   SoftSliceRecordList  *record_list_ptr;

   SoftCompareType compare;
   SoftOperandProceed operandProceed;
   SoftOperatorProceed operatorProceed;

   void FindSoftSliceExplicit();
   void FindSoftSliceImplicit();
   
   void HandleBrute(int mode);
   void HandleBranchBound(int mode);

   // defined in enginehiersoftst.cpp 
   void FindSoftSliceHierarchical(const double AR_LIMIT,
                                  double width_limit,
                                  double height_limit);
   void HandleHierarchical(int mode); 

   SoftEngineType(const SoftEngineType&);
   void operator =(const SoftEngineType&);
};
// --------------------------------------------------------

#endif