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
#include "enginehierst.hpp"
#include "vectorize.hpp"
#include "utilities.hpp"
#include "interface.hpp"
#include "parameters.hpp"

#include "btreefromblobb.hpp"
#include "btreecompactsstree.hpp"
#include "optoprblobb.hpp"

#include <iostream>
#include <fstream>
#include <queue>
#include <iomanip>
using namespace std;

void SliceHierEngine(char *argv[],
                     const CommandOptions& choice)
{
   ifstream infile;
   ofstream outfile;

   vector< vector<Dimension> > block;
   int blockNum = 0;   
   FloorPlanVec fp;

   vector<Cluster> clusters;
   vector<ClusterSet> clusterSets;
   
   cout.setf(ios::fixed);
   cout.precision(0);
   
   infile.open(argv[1]);
   InitializeSlice(block, blockNum, infile);
   infile.close();
   
   InitializeCluster(block, blockNum, clusters);
   if (INF_SHOW_INTERMEDIATES)
      cout << "===== after InitializeCluster =====" << endl;
   
   while (clusters.size() > 1)
   {
      if (INF_SHOW_INTERMEDIATES)
         cout << "===== Grouping " << clusters.size()
              << " clusters... " << endl;
      GroupClusterSets(clusters, clusterSets);
      
      int clusterSetNum = clusterSets.size();
      HIER_USE_AR = clusterSetNum > 1; // HIER_CLUSTER_BASE;

      if (INF_SHOW_INTERMEDIATES)
         cout << "===== Packing into " << clusterSets.size()
              << " clusters... " << endl;
      CoreEngine(blockNum, clusterSets, clusters);
      
      int clusterNum = clusters.size();
      double totalArea = 0;
      double deadspace = 0;
      for (int i = 0; i < clusterNum; i++)
      {
         totalArea += clusters[i].area;
         deadspace += clusters[i].deadspace;
      }
      if (INF_SHOW_INTERMEDIATES)
      {
	 cout.setf(ios::fixed);
	 cout.precision(2);
	 cout << endl;
         cout << "totalArea: " << setw(11) << totalArea 
              << " deadspace: " << setw(11) << deadspace 
              << " (" << ((deadspace / totalArea) * 100) 
              << "%) time: " << setw(5) << getTotalTime() << endl;
         cout << endl;
      }
   }

   cout.setf(ios::fixed);
   cout.precision(2);
   PrintDimensions(clusters[0].width, clusters[0].height);
   PrintAreas(clusters[0].deadspace, clusters[0].area);
   cout << endl;
   PrintUtilization(clusters[0].deadspace, clusters[0].area);
   cout << endl;

   Evaluate(block, clusters[0], fp);   
   SoftPacking *spk_ptr = NULL;
   
   // ----optimizing operators-----
   if (HIER_OPTOPR)
   {
      cout << "Optimizing operators in the Polish expression..." << endl;
      infile.clear();
      infile.open(argv[1]);
      BlockInfoType blockinfo(infile);
      infile.close();

      SoftSliceRecordFromBloBB ssr(clusters[0]);   
      spk_ptr = new SoftPacking(ssr, blockinfo);

      cout << "After operator optimization," << endl;
      PrintDimensions(spk_ptr->totalWidth, spk_ptr->totalHeight);
      PrintAreas(spk_ptr->deadspace, spk_ptr->blockArea);
      cout << endl;
      PrintUtilization(spk_ptr->deadspace, spk_ptr->blockArea);
      cout << endl;
   }
   else
      spk_ptr = new SoftPackingFromBloBB(block, clusters[0], fp);

   // ----compaction-----
   if (HIER_COMPACT)
   {
      cout << "Compacting..." << endl;
      BTreeCompactSlice(*spk_ptr, argv[2]);
   }
   else
   {
      outfile.open(argv[2]);
      spk_ptr->output(outfile);

      if (outfile.good())
         cout << "Output successfully written to " << argv[2] << endl;
      else
      {
         cout << "Something wrong with the file " << argv[2] << endl;
         exit(1);
      }
      outfile.close();
   }
   cout << endl;
   delete spk_ptr;
}
// --------------------------------------------------------
   

