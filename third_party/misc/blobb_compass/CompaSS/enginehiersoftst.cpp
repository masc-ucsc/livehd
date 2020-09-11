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
#include "enginehiersoftst.hpp"
#include "datastrfrontsoftst.hpp"
#include "datastrhiersoftst.hpp"
#include "enginesoftst.hpp"
//#include "debug.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
using namespace std;

// -----default parameters-----
const double DEFAULT_HIER_INIT_DEADSPACE = 0.05;
const double DEFAULT_HIER_DEADSPACE_INCRE = 1.5;
const double DEFAULT_HIER_WIDTH_INCRE = 1.15;
const double DEFAULT_HIER_HEIGHT_INCRE = 1.15;

const int DEFAULT_HIER_CLUSTER_BASE = 8;
const double DEFAULT_HIER_CLUSTER_AREA_DEV = 1.85;
const double DEFAULT_HIER_AR = 1.2;
const int DEFAULT_HIER_USE_AR_LEVEL = 2;
const bool DEFAULT_HIER_USE_AR = false;

const double DEFAULT_HIER_SIMILARITY_THRESHOLD = 1.9;

// -----adjustable parameters-----
double SoftHierEngineType::HIER_INIT_DEADSPACE = DEFAULT_HIER_INIT_DEADSPACE;
double SoftHierEngineType::HIER_DEADSPACE_INCRE = DEFAULT_HIER_DEADSPACE_INCRE;
double SoftHierEngineType::HIER_WIDTH_INCRE = DEFAULT_HIER_WIDTH_INCRE;
double SoftHierEngineType::HIER_HEIGHT_INCRE = DEFAULT_HIER_HEIGHT_INCRE;

int SoftHierEngineType::HIER_CLUSTER_BASE = DEFAULT_HIER_CLUSTER_BASE;
double SoftHierEngineType::HIER_CLUSTER_AREA_DEV = DEFAULT_HIER_CLUSTER_AREA_DEV;
double SoftHierEngineType::HIER_AR = DEFAULT_HIER_AR;
int SoftHierEngineType::HIER_USE_AR_LEVEL = DEFAULT_HIER_USE_AR_LEVEL;
bool SoftHierEngineType::HIER_USE_AR = false;

double SoftHierEngineType::HIER_SIMILARITY_THRESHOLD
    = DEFAULT_HIER_SIMILARITY_THRESHOLD;
// --------------------------------------------------------
BlockInfoType::BlockInfoType(const SoftClusterSet& clusterSet)
{
   int clusterNum = clusterSet.size();
   in_total_area = 0;
   for (int i = 0; i < clusterNum; i++)
   {
      blocks.push_back(clusterSet[i].boundary());
      in_total_area +=
         clusterSet[i].blkArea() + clusterSet[i].deadspace();
   }
}
// --------------------------------------------------------
SoftSliceRecord::SoftSliceRecord(const SoftCluster& cluster)
   : minArea(cluster.blkArea()), minDeadspace(cluster.deadspace()),
     expression(cluster.expression()),
     boundary(cluster.boundary()) {}
// --------------------------------------------------------
SoftHierEngineType::SoftHierEngineType(int instanceType,
                                       const BlockInfoType& blockinfo,
                                       const set<string> *const userDefinedOptions)
   : _instanceType(instanceType),
     _blockinfo(blockinfo),
     current_level(0),
     _userDefinedOptions_cleaner((userDefinedOptions == NULL)
                                 ? new set<string> : NULL),
     _userDefinedOptions((userDefinedOptions == NULL)
                         ? *_userDefinedOptions_cleaner // use the empty set
                         : *userDefinedOptions)         // use the set spec. in the param.
{
   int blocknum = blockinfo.size();
   for (int i = 0; i < blocknum; i++)
      clusters.push_back(SoftCluster(i, blockinfo[i]));
}
// --------------------------------------------------------
SoftHierEngineType::~SoftHierEngineType()
{
   if (_userDefinedOptions_cleaner != NULL)
      delete _userDefinedOptions_cleaner;
}
// --------------------------------------------------------
SoftSliceRecord* SoftHierEngineType::operator ()(int format)
{
   tuneAndPrintParameters();
   while (clusters.size() > 1)
   {
      cout << "[" << current_level << "]===== Grouping "
           << clusters.size() << " clusters... " << endl;
      GroupClusterSets();

      cout << "[" << current_level << "]===== Packing into "
           << clusterSets.size() << " clusters... " << endl;
      CoreEngine(format);
      current_level++;
      
      int clusterNum = clusters.size();
      double blkArea = 0;
      double deadspace = 0;
      for (int i = 0; i < clusterNum; i++)
      {
         blkArea += clusters[i].blkArea();
         deadspace += clusters[i].deadspace();
      }

      cout.setf(ios::fixed);
      cout.precision(2);
      cout << endl;
      cout << "blkArea: " << setw(11) << blkArea
           << " deadspace: " << setw(11) << deadspace
           << " (" << ((deadspace / blkArea) * 100)
           << "%) time: " << getTotalTime() << endl;
      cout << endl;
   }

   cout << "total area: " << setw(11)
        << clusters[0].blkArea() + clusters[0].deadspace() << endl;
   cout << "block area: " << setw(11)
        << clusters[0].blkArea() << endl;
   cout << "deadspace:  " << setw(11)
        << clusters[0].deadspace() << endl;
   SoftSliceRecord *ssr_ptr = new SoftSliceRecord(clusters[0]);

//    for (unsigned int i = 0; i < ssr_ptr->expression.size(); i++)
//       if (ssr_ptr->expression[i] == SoftSTree::BOTH)
//          cout << " -";
//       else
//          cout << ssr_ptr->expression[i] << " ";
   return ssr_ptr;   
}      
// --------------------------------------------------------   
void SoftHierEngineType::CoreEngine(int format)
{
   int clusterSetNum = clusterSets.size();
   BlockCompareType block_compare = NULL;
   if (format == BlockInfoType::SOFT)
      block_compare = trivial_compare;
   else
      block_compare = hard_blk_compare;
   
   clusters.clear();
   for (int i = 0; i < clusterSetNum; i++)
   {
      BlockInfoType blockinfo(clusterSets[i]);
      SoftEngineType engine(blockinfo, block_compare);

      if (current_level <= HIER_USE_AR_LEVEL &&
          clusterSetNum > HIER_CLUSTER_BASE * HIER_CLUSTER_BASE)
      {
         HIER_USE_AR = true;
         if (i == 0)
            cout << " * Aspect ratio bound used." << endl;
      }
      else
      {
         HIER_USE_AR = false;
         if (i == 0)
            cout << " * Aspect ratio bound not used." << endl;
      }
      SoftSliceRecordList *ssrListPtr = engine(SoftEngineType::HIER,
                                               SoftEngineType::EXPLICIT);

      if (clusterSetNum == HIER_CLUSTER_BASE)
      {
         if (i == 0)
            cout << endl;
         cout << "finished " << (i+1) << " / " << HIER_CLUSTER_BASE
              << ". cluster size: " << clusterSets[i].size()
              << " time: " << getTotalTime() << endl;
      }

      int instr = BoundaryType::FLIP_OR;
      if (format == BlockInfoType::TXT_FIXED)
         instr = BoundaryType::NOOP;
      SoftCluster merged(instr,
                         clusterSets[i], ssrListPtr->last());
      clusters.push_back(merged);

      delete ssrListPtr;
   }
}
// --------------------------------------------------------
void SoftHierEngineType::GroupClusterSets() 
{
   vector<DistanceInfo> distInfoVec;
   BuildSortedVector(distInfoVec);

   const int clusterNum = clusters.size();
   int clusterSetNum = getClusterSetNum(clusterNum);   // <-- may be imposs   
   const int maxClusterSize = HIER_CLUSTER_BASE + 1;
   const double maxClusterArea = (clusterSetNum <= HIER_CLUSTER_BASE)?
      DBL_MAX : getMaxArea();

   // ----- initialize tools -----
   vector<int> identifier;
   vector<int> setSize;
   vector<double> setArea;
   for (int i = 0; i < clusterNum; i++)
   {
      double clusterArea = clusters[i].blkArea() + clusters[i].deadspace();
      identifier.push_back(i);
      setSize.push_back(1);
      setArea.push_back(clusterArea);
   }

   // ----- assign sets -----
   clusterSets.clear();
   int clusterSetCount = clusterNum;
   while ((clusterSetCount > clusterSetNum) &&
          !distInfoVec.empty())
   {
      DistanceInfo closest(distInfoVec.back());
      int block1 = closest.blkOne;
      int block2 = closest.blkTwo;
      int blockOneID = min(identifier[block1], identifier[block2]);
      int blockTwoID = max(identifier[block1], identifier[block2]);
      double setOneBLBlockArea = 
         clusters[blockOneID].blkArea() + clusters[blockOneID].deadspace();

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
      SoftClusterSet temp;
      clusterSets.push_back(temp);

      for (int j = ptr; j < clusterNum; j++)
         if (identifier[j] == setIndex)
         {
            identifier[j] = -1;
            clusterSets[i].push_cluster(clusters[j]);
         }     
   }

   // ----- empty clusters -----
   clusters.clear();
}
// --------------------------------------------------------
void SoftHierEngineType::BuildSortedVector(
   vector<DistanceInfo>& distInfoVec) const
{
   int clusterNum = clusters.size();
   for (int i = 1; i < clusterNum; i++)
      for (int j = 0; j+i < clusterNum; j++)
      {
         if (distInfoVec.size() >= HIER_SORT_VEC_MAX_SIZE)
            break;
         
         DistanceInfo dInfo;
         dInfo.points = getPoints(clusters[j], clusters[j+i]);
         dInfo.blkOne = j;
         dInfo.blkTwo = j+i;

         distInfoVec.push_back(dInfo);
      }
   sort(distInfoVec.begin(), distInfoVec.end());
}
// --------------------------------------------------------
int SoftHierEngineType::getClusterSetNum(int clusterNum) const
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
double SoftHierEngineType::getPoints(const SoftCluster& c1,
                                     const SoftCluster& c2) const
{
//    double x1 = c1.boundary().min_point().xCoord();
//    double y1 = c1.boundary().min_point().yCoord();
//    double x2 = c2.boundary().min_point().xCoord();
//    double y2 = c2.boundary().min_point().yCoord();

   double x1 = c1.boundary()[1].xCoord();
   double y1 = c1.boundary()[1].yCoord();
   double x2 = c2.boundary()[1].xCoord();
   double y2 = c2.boundary()[1].yCoord();
   
   return (pow(min(x1, x2) / max(x1, x2), 10) +
           pow(min(y1, y2) / max(y1, y2), 10));
}
// --------------------------------------------------------
double SoftHierEngineType::getMaxArea() const
{
   double totalArea = 0;
   int clusterNum = clusters.size();
   for (int i = 0; i < clusterNum; i++)
      totalArea += (clusters[i].blkArea() + clusters[i].deadspace());

   return (totalArea / clusterNum) * HIER_CLUSTER_AREA_DEV;
}
// --------------------------------------------------------       
void SoftEngineType::FindSoftSliceHierarchical(const double AR_LIMIT,
                                               double width_limit,
                                               double height_limit)
{
   int qSize = bCont.size();

   if ((qSize == 0) && (sst.balance() == 1))
   {
      if (compare(sst, record_list_ptr->last()))
      {
         record_list_ptr->add_record(sst);
         width_limit = sqrt(sst.total_area() * AR_LIMIT);
         height_limit = sqrt(sst.total_area() * AR_LIMIT);
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
         FindSoftSliceHierarchical(AR_LIMIT, width_limit, height_limit);

      sst.pop_operand();
      bCont.push(blk);
   }

   for (int j = SoftSTree::PLUS; j >= SoftSTree::STAR; j--)
      if (sst.can_push_operator(j))
      {
         sst.push_operator(j, width_limit, height_limit);

         counter[sst.expression_size()][0]++;
         counter[sst.expression_size()][4]++;
         if (operatorProceed(sst, record_list_ptr->last()))
            FindSoftSliceHierarchical(AR_LIMIT, width_limit, height_limit);

         sst.pop_operator();
      }         
}
// --------------------------------------------------------
void SoftEngineType::HandleHierarchical(int mode)
{
   double init_totalArea = -1;
   double init_deadspace = -1;
   double init_ar = Point::INFTY;
   double width_limit = Point::INFTY;
   double height_limit = Point::INFTY;

   for (int i = 1; i < blockinfo.BLOCK_NUM(); i++)
      bCont.push(i);
   
   compare = BranchBoundCompare;
   operandProceed = BranchBoundProceed;
   
   init_deadspace = sst.total_area() * SoftHierEngineType::HIER_INIT_DEADSPACE;
   init_totalArea = sst.total_area() + init_deadspace;
   record_list_ptr = new SoftSliceRecordList(init_totalArea, init_deadspace);
   
   if (SoftHierEngineType::HIER_USE_AR)
   {
      init_ar = SoftHierEngineType::HIER_AR;
      width_limit = sqrt(init_totalArea * init_ar);
      height_limit = sqrt(init_totalArea * init_ar);
   }
   else
   {
      width_limit = Point::INFTY;
      height_limit = Point::INFTY;
   }
         

   while (record_list_ptr->empty())
   {
      sst.push_operand(0);
      if (mode == SoftEngineType::EXPLICIT)
         operatorProceed = BranchBoundHierarchicalProceed;
      else
      {
         cout << "Sorry: implicit mode is not supported with "
              << "hierarchical packing." << endl;
         exit(0);
      }
            
      FindSoftSliceHierarchical(init_ar, width_limit, height_limit);

      sst.pop_operand();
      record_list_ptr->set_deadspace(
         (*record_list_ptr)[0].minDeadspace
         * SoftHierEngineType::HIER_DEADSPACE_INCRE);
      init_totalArea = record_list_ptr->last().minArea;
      width_limit *= SoftHierEngineType::HIER_WIDTH_INCRE;
      height_limit *= SoftHierEngineType::HIER_HEIGHT_INCRE;
   }
}
// --------------------------------------------------------
void SoftHierEngineType::tuneAndPrintParameters() const
{
   // statistics about the blocks 
   const int blockNum = _blockinfo.BLOCK_NUM();
   const double blockArea = _blockinfo.block_area();

   // statistics about the user specified instance
   const double outline_area = Point::X_BOUND * Point::Y_BOUND;
   const double outline_deadspace =
      (outline_area >= Point::INFTY)
      ? Point::INFTY : outline_area / blockArea - 1;
   const double outline_AR =
      (outline_area >= Point::INFTY)
      ? Point::INFTY : sqrt(outline_area / blockArea);

   // print the user specified instance
   cout << "Instance Parameters: " << endl;
   cout << "  - HIER_OUTLINE_AR: ";
   if (outline_AR >= Point::INFTY)
      cout << "inf (i.e. no aspect ratio constraint imposed.)" << endl;
   else
      cout << outline_AR << endl;
   cout << "  - HIER_OUTLINE_DEADSPACE: ";
   if (outline_deadspace >= Point::INFTY)
      cout << "inf (i.e. no dead-space constraint imposed.)" << endl;
   else
      cout << (outline_deadspace * 100) << "%" << endl;
   cout << endl;

   // tune and print each tunable paramter
   cout << "Performance Parameters: " << endl;   
   if (!userSpecified("--HIER_CLUSTER_BASE"))
   {
      if (_instanceType == HARD_ONLY)
      {
         if (blockNum < 5000)
            SoftHierEngineType::HIER_CLUSTER_BASE = 8;
         else if (blockNum < 18000)
            SoftHierEngineType::HIER_CLUSTER_BASE = 7;
         else
            SoftHierEngineType::HIER_CLUSTER_BASE = 6;
      }
      else
      {
         if (blockNum < 40)
            SoftHierEngineType::HIER_CLUSTER_BASE = 6;
         else if (blockNum < 90)
            SoftHierEngineType::HIER_CLUSTER_BASE = 5;
         else if (blockNum < 200)
            SoftHierEngineType::HIER_CLUSTER_BASE = 4;
         else
            SoftHierEngineType::HIER_CLUSTER_BASE = 3;
      }
   }
   printIntParameter("HIER_CLUSTER_BASE",
                     SoftHierEngineType::HIER_CLUSTER_BASE);                    

   if (!userSpecified("--HIER_CLUSTER_AREA_DEV"))
   {
      if (blockNum < 1000)
         SoftHierEngineType::HIER_CLUSTER_AREA_DEV = 1.85;
      else
         SoftHierEngineType::HIER_CLUSTER_AREA_DEV = 1.95;
   }  
   printDoubleParameter("HIER_CLUSTER_AREA_DEV",
                        SoftHierEngineType::HIER_CLUSTER_AREA_DEV);

   printDoubleParameter("HIER_INIT_AR",
                        SoftHierEngineType::HIER_AR);

   if (!userSpecified("--HIER_USE_AR_LEVEL"))
   {
      if (blockNum < 5000)
         SoftHierEngineType::HIER_USE_AR_LEVEL = 2;
      else
         SoftHierEngineType::HIER_USE_AR_LEVEL = -1;
   }
   if (SoftHierEngineType::HIER_USE_AR_LEVEL < 0)
   {
      printDoubleParameter("HIER_USE_AR_LEVEL",
                           SoftHierEngineType::HIER_USE_AR_LEVEL,
                           " (i.e. AR constraint not imposed at any level.)");
   }
   else
   {
      printDoubleParameter("HIER_USE_AR_LEVEL",
                           SoftHierEngineType::HIER_USE_AR_LEVEL);
   }
  
   printDoubleParameter("HIER_WIDTH_INCRE",
                        SoftHierEngineType::HIER_WIDTH_INCRE);
   
   printDoubleParameter("HIER_HEIGHT_INCRE",
                        SoftHierEngineType::HIER_HEIGHT_INCRE);
   
   printDoubleParameter("HIER_INIT_DEADSPACE",
                        SoftHierEngineType::HIER_INIT_DEADSPACE);
   
   printDoubleParameter("HIER_DEADSPACE_INCRE",
                        SoftHierEngineType::HIER_DEADSPACE_INCRE);
   
   printDoubleParameter("HIER_SIMILARITY_THRESHOLD",
                        SoftHierEngineType::HIER_SIMILARITY_THRESHOLD);

   cout << "  - compact? "
        << (!userSpecified("--no_compact")? "Yes" : "No") << endl;
   cout << endl;
}   
// --------------------------------------------------------
void SoftHierEngineType::printIntParameter(string option,
                                           int value,
                                           string comments) const
{
   ostringstream sstream;
   sstream << value;

   printStringParameter(option, sstream.str(), comments);
}

void SoftHierEngineType::printDoubleParameter(string option,
                                              double value,
                                              string comments) const
{
   ostringstream sstream;

   sstream.setf(ios::fixed);
   sstream.precision(2);
   sstream << value;

   printStringParameter(option, sstream.str(), comments);
}   

void SoftHierEngineType::printStringParameter(string option,
                                              string value,
                                              string comments) const
{
   cout << "  - " << option << ": "
        << value << " " << comments << " ";
   if (!userSpecified("--" + option))
   {
      cout << " [tuned by CompaSS]" << endl;
   }
   else
   {
      cout << " [tuned by user] " << endl;
   }
}
// --------------------------------------------------------

