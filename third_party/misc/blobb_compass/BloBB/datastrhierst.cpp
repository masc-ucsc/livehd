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
#include "datastrhierst.hpp"
#include "datastrst.hpp"
// #include "debug.h" //---

#include <vector>
using namespace std;

DistanceInfo::DistanceInfo()
   : dist(0),
     blockOne(-1),
     blockTwo(-1)
{}
// --------------------------------------------------------
DistanceInfo::DistanceInfo(const DistanceInfo& d2)
   : dist(d2.dist),
     blockOne(d2.blockOne),
     blockTwo(d2.blockTwo)
{}
// --------------------------------------------------------
void DistanceInfo::operator =(const DistanceInfo& d2)
{
   dist = d2.dist;
   blockOne = d2.blockOne;
   blockTwo = d2.blockTwo;
}
// --------------------------------------------------------
Cluster::Cluster(int blockNum)
   : BLOCK_NUM(blockNum),
     PLUS(blockNum),
     STAR(blockNum+1),
     BLBlock(-1),
     width(0),
     height(0),
     area(0),
     deadspace(0)
{}
// --------------------------------------------------------
Cluster::Cluster(const Cluster& c2)
   : expression(c2.expression),
     orient(c2.orient),
     BLOCK_NUM(c2.BLOCK_NUM),
     PLUS(c2.PLUS),
     STAR(c2.STAR),
     BLBlock(c2.BLBlock),
     width(c2.width),
     height(c2.height),
     area(c2.area),
     deadspace(c2.deadspace)     
{}
// --------------------------------------------------------
void Cluster::operator =(const Cluster& c2)
{
   expression = c2.expression;
   orient = c2.orient;
   BLBlock = c2.BLBlock;
   width = c2.width;
   height = c2.height;
   area = c2.area;
   deadspace = c2.deadspace;
}
// --------------------------------------------------------
void Cluster::initialize(const Dimension block[][ORIENT_NUM],
                         int blk)
{
   BLBlock = blk;
   width = block[blk][0].width;
   height = block[blk][0].height;
   area = width * height;
   deadspace = 0;
   
   expression.push_back(blk);
   orient.push_back(0);
}
// --------------------------------------------------------
void Cluster::initialize(const SliceRecord& best,
                         ClusterSet& clusterSet)
{
   int orient_size = best.blockNum;
   int expr_size = 2 * orient_size - 1;
   int next_orient = 0;

   // assume this ->expression, orient empty;
   // each supermodule is identified by its index in 
   // its cluster set
   BLBlock = best.expression[0];
   width = best.width;
   height = best.height;
   area = 0;
   deadspace = best.deadspace;
   for (int i = 0; i < expr_size; i++)
   {
      int sign = best.expression[i];
      if (sign < BLOCK_NUM)
      {
         int index = sign;
         
         if (best.orient[next_orient] == 1)
            clusterSet[index].complement();
         next_orient++;

         int p_expr_size = clusterSet[index].expression.size();
         for (int j = 0; j < p_expr_size; j++)
            expression.push_back(clusterSet[index].expression[j]);

         int p_orient_size = clusterSet[index].orient.size();
         for (int k = 0; k < p_orient_size; k++)
            orient.push_back(clusterSet[index].orient[k]);

         area += clusterSet[index].area;
         deadspace += clusterSet[index].deadspace;         
      }
      else
         expression.push_back(sign);
   }
}
// --------------------------------------------------------
void Cluster::complement()
{
   double tempWidth = width;
   double tempHeight = height;

   width = tempHeight;
   height = tempWidth;

   int exprSize = expression.size();
   for (int i = 0; i < exprSize; i++)
      if (expression[i] == PLUS)
         expression[i] = STAR;
      else if (expression[i] == STAR)
         expression[i] = PLUS;

   int orientSize = orient.size();
   for (int j = 0; j < orientSize; j++)
      if (orient[j] == 0)
         orient[j] = 1;
      else 
         orient[j] = 0;
}
// --------------------------------------------------------
void setSliceRecord(SliceRecord& sRecord, 
                    const Cluster& cluster)
{
   sRecord.blockNum = cluster.BLOCK_NUM;
   sRecord.area = cluster.area + cluster.deadspace;
   sRecord.deadspace = cluster.deadspace;
   sRecord.width = cluster.width;
   sRecord.height = cluster.height;

   int exprSize = cluster.expression.size();
   int orientSize = cluster.orient.size();

   for (int i = 0; i < exprSize; i++)
      sRecord.expression[i] = cluster.expression[i];

   for (int i = 0; i < orientSize; i++)
      sRecord.orient[i] = cluster.orient[i];
}
// --------------------------------------------------------
void InitializeCluster(const Dimension block[][ORIENT_NUM],
                       int blockNum,
                       vector<Cluster>& clusters)
{
   for (int i = 0; i < blockNum; i++)
   {
      Cluster c(blockNum);
      c.initialize(block, i);
      clusters.push_back(c);
   }
}
// --------------------------------------------------------
