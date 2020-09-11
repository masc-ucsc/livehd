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
#include "enginehierst.hpp"
#include "datastrst.hpp"
#include "boundst.hpp"
#include "stackqueue.hpp"
#include "datastrhierst.hpp"
#include "parameters.hpp"

#include <iomanip>
#include <cmath>
#include <queue>
#include <algorithm>
#include <float.h>
using namespace std;

// ========================================================
int HIER_CLUSTER_BASE = HIER_UNDEFINED_SENTINEL;
bool HIER_USE_AR = true;
double HIER_AR = HIER_UNDEFINED_SENTINEL;
double HIER_AR_INCRE = HIER_UNDEFINED_SENTINEL;
double HIER_BEST_AREA_INCRE = HIER_UNDEFINED_SENTINEL;
double HIER_CLUSTER_AREA_DEV = HIER_UNDEFINED_SENTINEL;
double HIER_SIDE_RESOLUTION = HIER_UNDEFINED_SENTINEL;
bool HIER_COMPACT = true;
bool HIER_OPTOPR = true;
// ========================================================
void FindSlice(const Dimension block[][ORIENT_NUM],
               const bool same[][MAX_BLOCK_NUM],
               const int blkBefore[],
               STree& st,
               OwnQueue<int>& bContainer,
               SliceRecord& best)
{
   static const int PLUS = st.BLOCK_NUM;
   static const int STAR = st.BLOCK_NUM + 1;

   const double MAX_LENGTH = (HIER_USE_AR)? 
      sqrt(best.area * HIER_AR) : DBL_MAX; 
   
   int qSize = bContainer.size();
   if ((qSize == 0) && (st.balance == 1))
   {
      if (st.deadspace < best.deadspace)
         best = st;
      return;
   }

   for (int i = 0; i < qSize; i++)
   {
      int rect = bContainer.dequeue();
      for (int k = 0; k < ENG_ORIENT_CONSIDERED; k++)
      {
         st.push_orient(k);
         st.push_operand(rect, block);
         
         if (blockSym(st))
            if (sameBlockBound(same, blkBefore, st))
               if (st.deadspace + extDeadspace(st) < best.deadspace)
                  FindSlice(block, same, blkBefore, 
                            st, bContainer, best); 
         st.pop_operand();
         st.pop_orient();
      }
      bContainer.enqueue(rect);
   }

   for (int j = PLUS; j <= STAR; j++)
      if (st.can_push_operator(j))
      {
         st.push_operator(j);

         if (st.deadspace < best.deadspace)
            if (abutSym(st))
               if (st.deadspace + extDeadspace(st) < best.deadspace)
                  if (st.buffer.top().width < MAX_LENGTH &&
                      st.buffer.top().height < MAX_LENGTH) 
                     FindSlice(block, same, blkBefore, 
                               st, bContainer, best); 

         st.pop_operator();
      }
}
// ========================================================
void CoreEngine(int blockNum,
                vector<ClusterSet>& clusterSets,   // full --> empty
                vector<Cluster>& clusters)         // empty --> full
{
   static const int BLOCK_NUM = blockNum; // TOTAL number of blks
   static const double ORIG_HIER_AR = HIER_AR;

   int clusterSetNum = clusterSets.size();
   Dimension block[MAX_SIZE][ORIENT_NUM];    // <-|
   bool same[MAX_BLOCK_NUM][MAX_BLOCK_NUM];  //   |-global info
   int blkBefore[MAX_SIZE];                  // <-| for FindSlice

   for (int i = 0; i < clusterSetNum; i++)
   {
      int clusterNum = clusterSets[i].size();
      double grandArea = 0;
      OwnQueue<int> bContainer;

      // each supermodule is identified by its index
      // in its cluster set
      SetGlobalInfo(clusterSets[i], block, same, blkBefore, 
                    grandArea, bContainer);      
      
      STree st(BLOCK_NUM, grandArea);
      SliceRecord best;      
      
      int firstBlock = bContainer.dequeue();
      best.area = st.TOTAL_AREA;
      best.deadspace = 0;
      HIER_AR = ORIG_HIER_AR;
      while (best.blockNum == 0)
      {
         best.area *= HIER_BEST_AREA_INCRE;
         best.deadspace = best.area - st.TOTAL_AREA;

         st.push_orient(0);
         st.push_operand(firstBlock, block);

         FindSlice(block, same, blkBefore, st, bContainer, best);

         st.pop_operand();
         st.pop_orient();
         HIER_AR *= HIER_AR_INCRE; 
      }
      
      Cluster merged(BLOCK_NUM);
      best.blockNum = clusterNum;   // <--"correct" best.blockNum
      merged.initialize(best, clusterSets[i]);

      clusters.push_back(merged);
   }

   for (int i = 0; i < clusterSetNum; i++)
      clusterSets.pop_back();
}
// --------------------------------------------------------
void SetGlobalInfo(const ClusterSet& clusterSet,
                   Dimension block[][ORIENT_NUM],
                   bool same[][MAX_BLOCK_NUM],
                   int blkBefore[],
                   double& grandArea,
                   OwnQueue<int>& bContainer)
{
   CompareSidesType CompareSides = (ENG_ORIENT_CONSIDERED == 1)?
      fixed_CompareSides : free_CompareSides;
   
   int clusterNum = clusterSet.size();   
   for (int j = 0; j < clusterNum; j++)
   {
      int currBlk = j;
      double width = clusterSet[j].width;
      double height = clusterSet[j].height;

      // set block[][ORIENT_NUM]
      for (int k = 0; k < ORIENT_NUM; k++)
         if (k % ENG_ORIENT_CONSIDERED == 0)
         {
            block[currBlk][k].width = width;
            block[currBlk][k].height = height;
         }
         else if (k % ENG_ORIENT_CONSIDERED == 1)
         {
            block[currBlk][k].width = height;
            block[currBlk][k].height = width;
         }
	 else
	 {
	    cout << "ERROR: invalid value for \"ENG_ORIENT_CONSIDERED\" ("
		 << ENG_ORIENT_CONSIDERED << ")." << endl;
	    exit(1);
	 }

      // set blkBefore[] and same[][MAX_BLOCK_NUM]
      blkBefore[currBlk] = 0;
      for (int k = 0; k < j; k++)
      {
         int compBlk = k;
         if (CompareSides(block, currBlk, compBlk))
         {
            same[compBlk][currBlk] = true;
            same[currBlk][compBlk] = true;
            blkBefore[currBlk]++;
         }
         else
         {
            same[compBlk][currBlk] = false;
            same[currBlk][compBlk] = false;
         }        
      }
      same[currBlk][currBlk] = true;
      grandArea += width * height;
      bContainer.enqueue(currBlk);
   }  
}
// --------------------------------------------------------
bool free_CompareSides(const Dimension block[][ORIENT_NUM],
		       int currBlk,
		       int compBlk)
{
   double min1 = min(block[currBlk][0].width, block[currBlk][0].height);
   double max1 = max(block[currBlk][0].width, block[currBlk][0].height);

   double min2 = min(block[compBlk][0].width, block[compBlk][0].height);
   double max2 = max(block[compBlk][0].width, block[compBlk][0].height);

   return (pow(min(min1, min2) / max(min1, min2), 2) 
         + pow(min(max1, max2) / max(max1, max2), 2)) 
         >= HIER_SIDE_RESOLUTION; // play with this figure
}
// --------------------------------------------------------
bool fixed_CompareSides(const Dimension block[][ORIENT_NUM],
			int currBlk,
			int compBlk)
{
   double min1 = block[currBlk][0].width;
   double max1 = block[currBlk][0].height;

   double min2 = block[compBlk][0].width;
   double max2 = block[compBlk][0].height;

   return (pow(min(min1, min2) / max(min1, min2), 2) 
         + pow(min(max1, max2) / max(max1, max2), 2)) 
         >= HIER_SIDE_RESOLUTION; // play with this figure
}
// ========================================================
void GroupClusterSets(vector<Cluster>& clusters,       // full -> empty
                      vector<ClusterSet>& clusterSets) // empty -> full  
{
   // priority_queue<DistanceInfo> distInfoPQ;
   // BuildPriorityQueue(clusters, distInfoPQ);
   vector<DistanceInfo> distInfoVec;
   BuildSortedVector(clusters, distInfoVec);

   const int clusterNum = clusters.size();
   int clusterSetNum = getClusterSetNum(clusterNum);   // <-- may be imposs   
   const int maxClusterSize = HIER_CLUSTER_BASE + 1;
   const double maxClusterArea = (clusterSetNum <= HIER_CLUSTER_BASE)?
      DBL_MAX : getMaxArea(clusters);
   
   // ----- initialize tools -----
   vector<int> identifier;
   vector<int> setSize;
   vector<double> setArea;
   for (int i = 0; i < clusterNum; i++)
   {
      identifier.push_back(i);
      setSize.push_back(1);
      setArea.push_back(clusters[i].width * clusters[i].height);
   }

   // ----- assign sets -----
   int clusterSetCount = clusterNum;
   while ((clusterSetCount > clusterSetNum) &&
          !distInfoVec.empty()) // !distInfoPQ.empty())
   {
      // DistanceInfo closest(distInfoPQ.top());
      DistanceInfo closest(distInfoVec.back());
      int block1 = closest.blockOne;
      int block2 = closest.blockTwo;
      int blockOneID = min(identifier[block1], identifier[block2]);
      int blockTwoID = max(identifier[block1], identifier[block2]);
      double setOneBLBlockArea = 
         clusters[blockOneID].width * clusters[blockOneID].height;

      if ((blockOneID != blockTwoID) &&
          (setSize[blockOneID] + setSize[blockTwoID] <= maxClusterSize) &&
          (setArea[blockOneID] + setArea[blockTwoID] <= maxClusterArea))
      {
         for (int i = 0; i < clusterNum; i++)         
            if (identifier[i] == blockTwoID)
               identifier[i] = blockOneID;                                 
               
         setSize[blockOneID] += setSize[blockTwoID];
         setArea[blockOneID] += setSize[blockTwoID] * setOneBLBlockArea;
         clusterSetCount--;
      }    
      // distInfoPQ.pop();
      distInfoVec.pop_back();
   }
   clusterSetNum = clusterSetCount; // clusterSetNum >= intended

   // ----- in case if clusterSetNum == clusterNum -----
   if (clusterSetNum == clusterNum)
   {
      identifier[1] = 0;
      setSize[0] = 2;
      setArea[0] += setArea[1];
      clusterSetNum--;
   }

   // ----- gather sets -----
   for (int i = 0; i < clusterSetNum; i++)
   {
      int ptr = 0;
      while (identifier[ptr] == -1)
         ptr++;
      
      int setIndex = ptr;
      ClusterSet temp;
      clusterSets.push_back(temp);

      for (int j = ptr; j < clusterNum; j++)
         if (identifier[j] == setIndex)
         {
            identifier[j] = -1;
            clusterSets[i].push_back(clusters[j]);
         }     
   }

   // ----- empty clusters -----
   for (int i = 0; i < clusterNum; i++)
      clusters.pop_back();
}
// --------------------------------------------------------
void BuildSortedVector(const vector<Cluster>& clusters,
                       vector<DistanceInfo>& distInfoVec)
{
   getPointsType getPoints = (ENG_ORIENT_CONSIDERED == 1)?
      fixed_getPoints : free_getPoints;
   
   int clusterNum = clusters.size();
   for (int i = 1; i < clusterNum; i++)
      for (int j = 0; j+i < clusterNum; j++)
      {
         if (distInfoVec.size() >= HIER_SORT_VEC_MAX_SIZE) 
            break;

         DistanceInfo dInfo;
         dInfo.dist = getPoints(clusters[j], clusters[j+i]);
         dInfo.blockOne = j;
         dInfo.blockTwo = j+i;

         distInfoVec.push_back(dInfo);
      }
   sort(distInfoVec.begin(), distInfoVec.end());
}
// --------------------------------------------------------
double getMaxArea(const vector<Cluster>& clusters)
{
   double totalArea = 0;
   int clusterNum = clusters.size();
   for (int i = 0; i < clusterNum; i++)
      totalArea += clusters[i].width * clusters[i].height;

   return (totalArea / clusterNum) * HIER_CLUSTER_AREA_DEV;   
}
// --------------------------------------------------------
double free_getPoints(const Cluster& c1,
		      const Cluster& c2)
{
   double min1 = min(c1.width, c1.height);
   double max1 = max(c1.width, c1.height);
   double min2 = min(c2.width, c2.height);
   double max2 = max(c2.width, c2.height);

   return pow(min(min1, min2) / max(min1, min2), 10) +
          pow(min(max1, max2) / max(max1, max2), 10);   
}
// --------------------------------------------------------
double fixed_getPoints(const Cluster& c1,
		       const Cluster& c2)
{
   double min1 = c1.width;
   double max1 = c1.height;
   double min2 = c2.width;
   double max2 = c2.height;

   return pow(min(min1, min2) / max(min1, min2), 10) +
          pow(min(max1, max2) / max(max1, max2), 10);   
}
// --------------------------------------------------------
int getClusterSetNum(int clusterNum)
{
   double cSetNum = clusterNum;
   int index = 0;
   while (cSetNum > HIER_CLUSTER_BASE)
   {
      index++;
      cSetNum = cSetNum / HIER_CLUSTER_BASE;
   }
   return int(pow(double(HIER_CLUSTER_BASE), double(index)));
}
// --------------------------------------------------------
