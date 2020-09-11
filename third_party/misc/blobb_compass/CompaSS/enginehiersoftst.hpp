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
#ifndef ENGINEHIERSOFTST_H
#define ENGINEHIERSOFTST_H

#include "datastrfrontsoftst.hpp"
#include "datastrhiersoftst.hpp"

#include <vector>
#include <set>
using namespace std;

class SoftHierEngineType
{
public:
   SoftHierEngineType(int instanceType,
                      const BlockInfoType& blockinfo,
                      const set<string> *const userDefinedOptions = NULL);

   ~SoftHierEngineType();
   
   static int HIER_CLUSTER_BASE;
   static double HIER_CLUSTER_AREA_DEV;
   
   static double HIER_AR;
   static int HIER_USE_AR_LEVEL;
   static bool HIER_USE_AR;
   static double HIER_WIDTH_INCRE;
   static double HIER_HEIGHT_INCRE;

   static double HIER_INIT_DEADSPACE;
   static double HIER_DEADSPACE_INCRE;

   static double HIER_SIMILARITY_THRESHOLD;
   
   static const unsigned int HIER_SORT_VEC_MAX_SIZE = 300000;

   enum {HARD_ONLY = 0, SOFT_ONLY, HARD_AND_SOFT};

   SoftSliceRecord* operator ()(int format);

private:
   const int _instanceType;
   
   const BlockInfoType& _blockinfo;
   vector<SoftCluster> clusters;
   vector<SoftClusterSet> clusterSets;
   int current_level;   

   const set<string> *const _userDefinedOptions_cleaner;
   const set<string>& _userDefinedOptions;
   inline bool userSpecified(string option) const;
   
   class DistanceInfo;

   void tuneAndPrintParameters() const;
   void printIntParameter(string option,
                          int value, string comments = "") const;
   void printDoubleParameter(string option,
                             double value, string comments = "") const;
   void printStringParameter(string option,
                             string value, string comments = "") const;
   
   void CoreEngine(int format);

   void GroupClusterSets();
   void BuildSortedVector(vector<DistanceInfo>& distInfoVec) const;
   int getClusterSetNum(int clusterNum) const;
   double getPoints(const SoftCluster& c1,
                    const SoftCluster& c2) const;
   double getMaxArea() const;   

   class DistanceInfo
   {
   public:
      DistanceInfo() {}

      double points;
      int blkOne;
      int blkTwo;
      inline bool operator <(const DistanceInfo& d2) const
         {   return points < d2.points; }      
   };
};

inline bool SoftHierEngineType::userSpecified(string option) const
{
   return (_userDefinedOptions.find(option)
           != _userDefinedOptions.end());
}
// --------------------------------------------------------
inline bool trivial_compare(const BoundaryType& b1,
                            const BoundaryType& b2)
{
   return false;
}
// --------------------------------------------------------
inline bool hard_blk_compare(const BoundaryType& b1,
                             const BoundaryType& b2)
{
   double x1 = b1[1].xCoord();
   double y1 = b1[1].yCoord();

   double x2 = b2[1].xCoord();
   double y2 = b2[1].yCoord();

   return (pow((min(x1, x2) / max(x1, x2)), 2) +
           pow((min(y1, y2) / max(y1, y2)), 2) >=
           SoftHierEngineType::HIER_SIMILARITY_THRESHOLD);
}
// --------------------------------------------------------

#endif
   
      
