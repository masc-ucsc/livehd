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
#ifndef ENGINEHIERST_H
#define ENGINEHIERST_H

#include "datastrst.hpp"
#include "boundst.hpp"
#include "stackqueue.hpp"
#include "datastrhierst.hpp"

#include <vector>
#include <queue>
using namespace std;

typedef bool (*CompareSidesType)(const Dimension[][ORIENT_NUM],
				 int, int);
typedef double (*getPointsType)(const Cluster&, const Cluster&);

// ---------------------------------------------------------
void FindSlice(const Dimension block[][ORIENT_NUM],
               const bool same[][MAX_BLOCK_NUM],
               const int blkBefore[],
               STree& st,
               OwnQueue<int>& bContainer,
               SliceRecord& best);
// ========================================================
void CoreEngine(int blockNum,
                vector<ClusterSet>& clusterSets,
                vector<Cluster>& clusters);  // assumed empty
// --------------------------------------------------------
void SetGlobalInfo(const ClusterSet& clusterSet,
                   Dimension block[][ORIENT_NUM],
                   bool same[][MAX_BLOCK_NUM],
                   int blkBefore[],
                   double& grandArea,
                   OwnQueue<int>& bContainer);
// --------------------------------------------------------
bool free_CompareSides(const Dimension block[][ORIENT_NUM],
		       int currBlk, int compBlk);
bool fixed_CompareSides(const Dimension block[][ORIENT_NUM],
			int currBlk, int compBlk);
// ========================================================
void GroupClusterSets(vector<Cluster>& clusters,
                      vector<ClusterSet>& clusterSets);
// --------------------------------------------------------
void BuildSortedVector(const vector<Cluster>& clusters, 
                       vector<DistanceInfo>& distInfoVec);
// --------------------------------------------------------
double getMaxArea(const vector<Cluster>& clusters);
// --------------------------------------------------------
double fixed_getPoints(const Cluster& c1, const Cluster& c2);
double free_getPoints(const Cluster& cl, const Cluster& c2);
// --------------------------------------------------------
int getClusterSetNum(int clusterNum);
// ========================================================
#endif 
