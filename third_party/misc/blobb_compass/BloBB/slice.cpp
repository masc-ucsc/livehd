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
#include "slice.hpp"
#include "stackqueue.hpp"
#include "datastrst.hpp"
#include "boundst.hpp"
#include "enginest.hpp"

#include "parameters.hpp"
#include "utilities.hpp"
#include "interface.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
using namespace std;

// --------------------------------------------------------
void SliceEngine(char* argv[],
		 const CommandOptions& choice)
{
   Dimension block[MAX_SIZE][ORIENT_NUM];
   bool same[MAX_BLOCK_NUM][MAX_BLOCK_NUM];
   int blkBefore[MAX_SIZE];

   STree* st_ptr = NULL;
   OwnQueue<int> bContainer;
   SliceRecord best;
   FloorPlan fp;

   ifstream infile;
   ofstream outfile;
   int blockNum = 0;

   infile.open(argv[1]);
   InitializeSlice(block, st_ptr, blockNum, infile);
   InitializeBound(block, *st_ptr, same, blkBefore);
   infile.close();
      
   // size of bContainer is 1 LESS than st_ptr ->BLOCK_NUM
   for (int i = 1; i < blockNum; i++)
      bContainer.enqueue(i);
   
   if (INF_SHOW_SIMILARITY_TABLE)
   {
      cout << "similarity table: " << endl;
      PrintSimilarityTable(st_ptr ->BLOCK_NUM, same, blkBefore);
   }

   if (choice.algorithmType == "--optimal")
      BranchBoundSearch(block, same, blkBefore,
			*st_ptr, bContainer, best);
   else if (choice.algorithmType == "--backtrack")
   {
      BT_DEADSPACE_PERCENT = choice.dpercent;
      BT_MIN_DEADSPACE = st_ptr ->TOTAL_AREA * BT_DEADSPACE_PERCENT;
      BacktrackSearch(block, same, blkBefore,
		      *st_ptr, bContainer, best);
   }
   else if (choice.algorithmType == "--enumerate")
      EnumerateSearch(block, same, blkBefore,
		      *st_ptr, bContainer, best);

   if (best.blockNum > 0)
   {  // Evaluate gives seg fault if best.blockNum == 0
      Evaluate(block, best, fp);
      outfile.open(argv[2]);
      outputfp(block, fp, outfile);
      outfile.close();
      cout << "Ouput written to " << argv[2] << "." << endl;
   }
   else
      cout << "No output written to " << argv[2] << "." << endl;

   delete st_ptr;
}
// --------------------------------------------------------
void BranchBoundSearch(const Dimension block[][ORIENT_NUM],
		       const bool same[][MAX_BLOCK_NUM],
		       const int blkBefore[],
		       STree& st,
		       OwnQueue<int>& bContainer,
		       SliceRecord& best)
{
   counterName[0] = "visited";
   counterName[1] = "operand";
   counterName[2] = "opd left";
   counterName[3] = "operator";
   counterName[4] = "opr left";
   best.area = st.TOTAL_AREA * (1 + ENG_INIT_DEADSPACE_PERCENT);
   best.deadspace = best.area - st.TOTAL_AREA;
   while (best.blockNum == 0)
   {
      cout.setf(ios::fixed);
      cout.precision(2);
      cout << "Considering solutions with no more than "
	   << (best.deadspace / st.TOTAL_AREA * 100) << "% deadspace." << endl;

      st.push_orient(0);
      st.push_operand(0, block);
      
      FindSlice(BranchBoundOperandBound,
		BranchBoundOperatorBound,
		BranchBoundCompare,
		block, same, blkBefore, 
                st, bContainer, best);

      st.pop_operand();
      st.pop_orient();

      if (INF_SHOW_PRUNED_TABLE)
	 OutputCounter(cout, 5, 2*st.BLOCK_NUM);
      
      if (best.blockNum == 0)
      {
	 best.area *= ENG_DEADSPACE_INCRE;
	 best.deadspace = best.area - st.TOTAL_AREA;
	 InitializeCounter();
	 cout << "No solution found." << endl;
      }
   }
   cout.setf(ios::fixed);
   cout.precision(2);
   cout << "width:  " << setw(10) << best.width << endl;
   cout << "height: " << setw(10) << best.height << endl;
   cout << "total area: " << best.area 
        << " (" <<  ((best.deadspace / st.TOTAL_AREA) * 100) << "%)" << endl;
   cout << "block area: " << st.TOTAL_AREA << endl;
   if (INF_SHOW_POLISH_EXPRESSION)
      outputNPE(best, cout);
}
// --------------------------------------------------------
void BacktrackSearch(const Dimension block[][ORIENT_NUM],
		     const bool same[][MAX_BLOCK_NUM],
		     const int blkBefore[],
		     STree& st,
		     OwnQueue<int>& bContainer,
		     SliceRecord& best)
{
   counterName[0] = "visited";
   counterName[1] = "operand";
   counterName[2] = "opd left";
   counterName[3] = "operator";
   counterName[4] = "opr left";
   best.area = st.TOTAL_AREA * (1 + BT_DEADSPACE_PERCENT + 0.0001);
   best.deadspace = best.area - st.TOTAL_AREA;

   st.push_orient(0);
   st.push_operand(0, block);
   
   FindSlice(BacktrackOperandBound,
	     BacktrackOperatorBound,
	     BacktrackCompare,
	     block, same, blkBefore, 
	     st, bContainer, best);

   st.pop_operand();
   st.pop_orient();

   if (INF_SHOW_PRUNED_TABLE)
      OutputCounter(cout, 5, 2*st.BLOCK_NUM);

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
           << " (" <<  (best.deadspace / st.TOTAL_AREA * 100) << "%)" << endl;
      cout << "block area: " << st.TOTAL_AREA << endl;   
      if (INF_SHOW_POLISH_EXPRESSION)
	 outputNPE(best, cout);
   }
}
// --------------------------------------------------------
void EnumerateSearch(const Dimension block[][ORIENT_NUM],
		     const bool same[][MAX_BLOCK_NUM],
		     const int blkBefore[],
		     STree& st,
		     OwnQueue<int>& bContainer,
		     SliceRecord& best)
{
   counterName[0] = "visited";
   counterName[1] = "operand";
   counterName[2] = "opd left";
   counterName[3] = "operator";
   counterName[4] = "opr left";
   best.area = st.TOTAL_AREA * (1 + ENG_INIT_DEADSPACE_PERCENT);
   best.deadspace = best.area - st.TOTAL_AREA;
   while (best.blockNum == 0)
   {
      cout.setf(ios::fixed);
      cout.precision(2);
      cout << "Considering solutions with no more than "
	   << (best.deadspace / st.TOTAL_AREA * 100) << "% deadspace." << endl;
      
      st.push_orient(0);
      st.push_operand(0, block);
      
      FindSlice(EnumerateOperandBound,
		EnumerateOperatorBound,
		EnumerateCompare,
		block, same, blkBefore, 
                st, bContainer, best);

      st.pop_operand();
      st.pop_orient();

      if (INF_SHOW_PRUNED_TABLE)
	 OutputCounter(cout, 5, 2*st.BLOCK_NUM);
      
      if (best.blockNum == 0)
      {
	 best.area *= ENG_DEADSPACE_INCRE;
	 best.deadspace = best.area - st.TOTAL_AREA;
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
        << " (" <<  (best.deadspace / st.TOTAL_AREA * 100) << "%)" << endl;
   cout << "block area: " << st.TOTAL_AREA << endl;
}
// ========================================================
