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
#ifndef DATASTRHIERSOFTST_H
#define DATASTRHIERSOFTST_H

#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"

#include <vector>
using namespace std;

class SoftClusterSet;
// --------------------------------------------------------
class SoftCluster
{
public:
   SoftCluster(int block, const BoundaryType& newBoundary)
      : in_expression(1, block),
        in_boundary(newBoundary),
        in_blkArea(newBoundary.min_area()),
        in_deadspace(0) {}
   SoftCluster(int instr,
               const SoftClusterSet& clusterSet,
               const SoftSliceRecord& ssr);
   SoftCluster(const SoftCluster& sc)
      : in_expression(sc.in_expression),
        in_boundary(sc.in_boundary),
        in_blkArea(sc.in_blkArea),
        in_deadspace(sc.in_deadspace) {}

   void operator =(const SoftCluster& sc)
      {
         in_expression = sc.in_expression;
         in_boundary = sc.in_boundary;
         in_blkArea = sc.in_blkArea;
         in_deadspace = sc.in_deadspace;
      }

   inline const vector<int>& expression() const
      {  return in_expression; }
   inline int expression_size() const
      {  return in_expression.size(); }
   inline int expression(int i) const
      {  return in_expression[i]; }

   inline const BoundaryType& boundary() const
      {  return in_boundary; }

   inline double blkArea() const
      {  return in_blkArea; }

   inline double deadspace() const
      {  return in_deadspace; }
   
private:
   vector<int> in_expression;
   BoundaryType in_boundary;
   double in_blkArea;
   double in_deadspace;
};
// --------------------------------------------------------
class SoftClusterSet
{
public:
   SoftClusterSet() {}
   SoftClusterSet(const BlockInfoType& blockinfo)
      {         
         for (int i = 0; i < blockinfo.size(); i++)
            in_set.push_back(SoftCluster(i, blockinfo[i]));
      }
   SoftClusterSet(const SoftClusterSet& scs)
      : in_set(scs.in_set) {}
   void operator =(const SoftClusterSet& scs)
      {  in_set = scs.in_set; }

   inline int size() const
      {  return in_set.size(); }
   inline const SoftCluster& operator[](int i) const
      {  return in_set[i]; };

   inline void push_cluster(const SoftCluster& sc)
      {  in_set.push_back(sc); }

private:
   vector<SoftCluster> in_set;
};
// --------------------------------------------------------

#endif
