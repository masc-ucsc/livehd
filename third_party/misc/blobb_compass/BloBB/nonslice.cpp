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
#include "nonslice.hpp"
#include "datastr.hpp"
#include "bound.hpp"
#include "boundfixed.hpp"
#include "parameters.hpp"
#include "utilities.hpp"
#include "interface.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <float.h>
#include <limits.h>
#include <iomanip>
#include <algorithm>
#include <cmath>
using namespace std;

// --------------------------------------------------------
void NonsliceEngine(char *argv[],
                    const CommandOptions& choice)
{
   ifstream infile;
   ofstream outfile;
   
   OTree* ot_ptr = NULL;
   FloorPlan best;
   Dimension block[MAX_SIZE][ORIENT_NUM];
   
   double minEdge[MAX_SIZE];
   bool same[MAX_BLOCK_NUM+1][MAX_BLOCK_NUM];
   int blkBefore[MAX_SIZE] = {0};
   OwnQueue<int> bContainer;
   
   infile.open(argv[1]);
   Initialize(block, ot_ptr, infile);
   infile.close();   
   
   InitializeBound(block, *ot_ptr, minEdge, same, blkBefore);
   InitializeCounter();
   for (int k = 0; k < ot_ptr ->BLOCK_NUM; k++)
      bContainer.enqueue(k);

   if (INF_SHOW_SIMILARITY_TABLE)
   {
      cout << "similarity table: " << endl;
      PrintSimilarityTable(ot_ptr ->BLOCK_NUM, same, blkBefore);
   }

   if (choice.algorithmType == "--optimal")
      BranchboundSearch(minEdge, same, blkBefore,
                        block, *ot_ptr, bContainer, best);
   else if (choice.algorithmType == "--backtrack")
   {
      BT_DEADSPACE_PERCENT = choice.dpercent;
      BT_MIN_TOTAL = ot_ptr ->TOTAL_AREA * (1+BT_DEADSPACE_PERCENT);
      BT_MIN_DEADSPACE = ot_ptr ->TOTAL_AREA * BT_DEADSPACE_PERCENT;
      BacktrackSearch(minEdge, same, blkBefore,
		      block, *ot_ptr, bContainer, best);
   }
   else if (choice.algorithmType == "--enumerate")
      EnumerateSearch(minEdge, same, blkBefore,
                      block, *ot_ptr, bContainer, best);
   
   outfile.open(argv[2]);
   outputfp(block, best, outfile);
   outfile.close();
   cout << "Output written to " << argv[2] << "." << endl;

   delete ot_ptr;
}
// --------------------------------------------------------
void FindPacking(const ProceedType proceed,
                 const CompareType compare,
                 const double minEdge[],           // <-
                 const bool same[][MAX_BLOCK_NUM], //  |-area min'n
                 const int blkBefore[],            // <-
                 Dimension block[][ORIENT_NUM], // <-
                 OTree& ot,                     //  |---core engine
                 OwnQueue<int>& bContainer,     //  |
                 FloorPlan& best)               // <-                 
{
   static ofstream outs;
   int qSize = bContainer.size();  
      
   if (qSize == 0)
   {
      double width = ot.width();
      double height = ot.height();
      double area = width * height;
      if (compare(area, best.area))
      {
         // check if this is a better solution. If so,
         // start counting from here
         if (area < best.area)
            counter[0][1] = counter[0][0]; 

         best = ot;
	 if (INF_FN_PREFIX != "")
	 {
	    stringstream filename;
	    filename << INF_FN_PREFIX << counter[0][0] << INF_FN_SUFFIX;
	    
	    outs.open(filename.str().c_str());              
	    outputfp(block, best, outs);
	    outs.close();
	 }

	 if (INF_SHOW_INTERMEDIATES)
	 {
	    cout.setf(ios::fixed);
	    cout.precision(2);
	    cout << "[" << counter[0][0] << "] area: " << area
		 << " (" << ((area / ot.TOTAL_AREA - 1) * 100) << "%) " 
		 << setw(10) << getTotalTime() << endl;
	 }

         // ---file counter---
         counter[0][0]++;
      }
      return;
   }

   for (int i = 0; i < 4; i++)
   {
      if (ot.push_tree(i))
         for (int j = 0; j < qSize; j++)
         {
            int b = bContainer.dequeue();
            ot.push_perm(b);
            for (int k = 0; k < ENG_ORIENT_CONSIDERED; k++)
            {
               ot.push_orient(k);
               ot.setpos_update(block, b);

               counter[ot.perSize][0]++;
               if (proceed(minEdge, same, blkBefore,
                           bContainer, best, block, ot))
               {
                  counter[ot.perSize][1]++;
                  FindPacking(proceed, compare,
                              minEdge, same, blkBefore, block,
                              ot, bContainer, best);       
               }

               ot.remove_block(b);
               ot.pop_orient();
            }
            bContainer.enqueue(ot.pop_perm());

            // landmarks, 2n of such marks during runtime
            if (INF_SHOW_LANDMARKS && (ot.perSize == 0))
	    {
	       static int portion = 1;
               cout << "finished " << setw(2) << portion << " / "
		    << (2 * ot.BLOCK_NUM)
                    << setw(8) << getTotalTime() << "s" << endl;
	       portion++;
	    }
         }
      ot.pop_tree();
   }
}
// ========================================================
void BranchboundSearch(const double minEdge[],           
                       const bool same[][MAX_BLOCK_NUM], 
                       const int blkBefore[],            
                       Dimension block[][ORIENT_NUM], 
                       OTree& ot,                     
                       OwnQueue<int>& bContainer,                      
                       FloorPlan& best)
{
   // fixed orientation or free orientation ?
   ProceedType proceed = (ENG_ORIENT_CONSIDERED == 1)?
      fixed_BranchBoundProceed : BranchBoundProceed;
   
   counterName[0] = "visited";
   counterName[1] = "afterPrune";   
   best.area = ot.TOTAL_AREA * (1 + ENG_INIT_DEADSPACE_PERCENT);
   best.width = DBL_MAX;
   best.height = DBL_MAX;
   best.blockNum = 0;
   while (best.blockNum == 0)
   {
      cout.setf(ios::fixed);
      cout.precision(2);
      cout << "Considering solutions with no more than "
	   << ((best.area/ot.TOTAL_AREA - 1) * 100) << "% deadspace." << endl;
      FindPacking(proceed, BranchBoundCompare,
                  minEdge, same, blkBefore, 
                  block, ot, bContainer, best);
      if (INF_SHOW_PRUNED_TABLE)
	 OutputCounter(cout, 2, ot.BLOCK_NUM+1);
      
      if (best.blockNum == 0)
      {
	 best.area *= ENG_DEADSPACE_INCRE;
	 InitializeCounter();
	 cout << "No solution found." << endl;
      }
   }
   cout.setf(ios::fixed);
   cout.precision(2);
   cout << "width:  " << setw(10) << best.width << endl;
   cout << "height: " << setw(10) << best.height << endl;
   cout << "total area: " << best.area 
        << " (" <<  (best.area / ot.TOTAL_AREA * 100 - 100) << "%)" << endl;
   cout << "block area: " << ot.TOTAL_AREA << endl;
}
// --------------------------------------------------------
void BacktrackSearch(const double minEdge[],
                     const bool same[][MAX_BLOCK_NUM],
                     const int blkBefore[],
                     Dimension block[][ORIENT_NUM],
                     OTree& ot,
                     OwnQueue<int>& bContainer,
                     FloorPlan& best)
{
   // fixed or free orientation?
   ProceedType proceed = (ENG_ORIENT_CONSIDERED == 1)?
      fixed_BacktrackProceed : BacktrackProceed;
   
   counterName[0] = "visited";
   counterName[1] = "afterPrune";   
   best.area = ot.TOTAL_AREA;
   best.width = DBL_MAX;
   best.height = DBL_MAX;
   best.blockNum = 0;

   best.area *= (1 + BT_DEADSPACE_PERCENT + 0.0001);
   FindPacking(proceed, BacktrackCompare,
               minEdge, same, blkBefore, 
               block, ot, bContainer, best);
   
   if (INF_SHOW_PRUNED_TABLE)
      OutputCounter(cout, 2, ot.BLOCK_NUM+1);

   cout.setf(ios::fixed);
   cout.precision(2);
   if (best.blockNum == 0)
      cout << "No solution found with no greater than "
           << (BT_DEADSPACE_PERCENT * 100) << "% deadspace." << endl;
   else
   {
      cout << "width:  " << setw(10) << best.width << endl;
      cout << "height: " << setw(10) << best.height << endl;
      cout << "total area: " << best.area 
           << " (" <<  (best.area / ot.TOTAL_AREA * 100 - 100) << "%)" << endl;
      cout << "block area: " << ot.TOTAL_AREA << endl;
   }
}   
// --------------------------------------------------------
void EnumerateSearch(const double minEdge[],           
                     const bool same[][MAX_BLOCK_NUM], 
                     const int blkBefore[],            
                     Dimension block[][ORIENT_NUM], 
                     OTree& ot,                     
                     OwnQueue<int>& bContainer,                
                     FloorPlan& best)
{
   // fixed or free orientation?
   ProceedType proceed = (ENG_ORIENT_CONSIDERED == 1)?
      fixed_EnumerateProceed : EnumerateProceed;
   
   counterName[0] = "visited";
   counterName[1] = "afterPrune";   
   best.area = ot.TOTAL_AREA * (1 + ENG_INIT_DEADSPACE_PERCENT);
   best.width = DBL_MAX;
   best.height = DBL_MAX;
   best.blockNum = 0;
   while (best.blockNum == 0)
   {
      cout.setf(ios::fixed);
      cout.precision(2);
      cout << "Considering solutions with no more than "
	   << ((best.area/ot.TOTAL_AREA - 1) * 100) << "% deadspace." << endl;
      FindPacking(proceed, EnumerateCompare,
                  minEdge, same, blkBefore, 
                  block, ot, bContainer, best);

      if (INF_SHOW_PRUNED_TABLE)
	 OutputCounter(cout, 2, ot.BLOCK_NUM+1);

      cout.setf(ios::fixed);
      cout.precision(2);
      if (best.blockNum == 0)
      {
	 best.area *= ENG_DEADSPACE_INCRE;      
	 InitializeCounter();
	 cout << "No solution found." << endl;
      }
   }
   cout << "# non-symmetric optimal solutions:   "
	<< counter[0][0]-counter[0][1] << endl;
   
   if (INF_FN_PREFIX != "")
      cout << "Solutions [" << counter[0][1] << "] to [" << counter[0][0]-1
	   << "] correspond to optimal solutions." << endl;

   cout << "width:  " << setw(10) << best.width << endl;
   cout << "height: " << setw(10) << best.height << endl;
   cout << "total area: " << best.area
        << " (" <<  (best.area / ot.TOTAL_AREA * 100 - 100) << "%)" << endl;
   cout << "block area: " << ot.TOTAL_AREA << endl;
}
// ========================================================
