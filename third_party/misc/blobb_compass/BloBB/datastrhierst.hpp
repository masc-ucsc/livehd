/**************************************************************************
***    
*** Copyright (c) 2003 Regents of the University of Michigan,
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
#ifndef DATASTRHIERST_H
#define DATASTRHIERST_H

#include "datastrst.hpp"

#include <iostream>
#include <vector>
using namespace std;

class Cluster;
typedef vector<Cluster> ClusterSet;
// --------------------------------------------------------
class DistanceInfo
{
public:
   DistanceInfo();

   DistanceInfo(const DistanceInfo& d2);
   void operator =(const DistanceInfo& d2);

   double dist;
   int blockOne;
   int blockTwo;
};
// --------------------------------------------------------
inline bool operator <(const DistanceInfo& d1,
                       const DistanceInfo& d2)
{  return d1.dist < d2.dist; }
// --------------------------------------------------------
class Cluster
{
public:
   Cluster(int blockNum);
   Cluster(const Cluster& c2);
   void operator =(const Cluster& c2);

   vector<int> expression;
   vector<int> orient;

   const int BLOCK_NUM;
   const int PLUS;
   const int STAR;

   int BLBlock;
   double width;
   double height;
   double area;
   double deadspace;

   void initialize(const Dimension block[][ORIENT_NUM],
                   int blk);
   void initialize(const SliceRecord& best,
                   ClusterSet& clusterSet);
   void complement();
};
// --------------------------------------------------------
void setSliceRecord(SliceRecord& sRecord, const Cluster& cluster);
// --------------------------------------------------------
void InitializeCluster(const Dimension block[][ORIENT_NUM],
                       int blockNum,
                       vector<Cluster>& clusters); // assumed empty
#endif
