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
#ifndef NONSLICE_H
#define NONSLICE_H

#include "stackqueue.hpp"
#include "datastr.hpp"
#include "bound.hpp"
#include "interface.hpp"

// ========================================================
void NonsliceEngine(char *argv[], const CommandOptions& choice);
// --------------------------------------------------------
void FindPacking(const ProceedType proceed,
                 const CompareType compare,
                 const double minEdge[],
                 const bool same[][MAX_BLOCK_NUM],
                 const int blkBefore[],
                 Dimension block[][ORIENT_NUM],
                 OTree& ot,
                 OwnQueue<int>& bContainer,
                 FloorPlan& best);
// ========================================================
void BranchboundSearch(const double minEdge[],           
                       const bool same[][MAX_BLOCK_NUM], 
                       const int blkBefore[],            
                       Dimension block[][ORIENT_NUM], 
                       OTree& ot,                     
                       OwnQueue<int>& bContainer,                      
                       FloorPlan& best);
// --------------------------------------------------------
void BacktrackSearch(const double minEdge[],           
                     const bool same[][MAX_BLOCK_NUM], 
                     const int blkBefore[],            
                     Dimension block[][ORIENT_NUM], 
                     OTree& ot,                     
                     OwnQueue<int>& bContainer,                
                     FloorPlan& best);
// --------------------------------------------------------
void EnumerateSearch(const double minEdge[],           
                     const bool same[][MAX_BLOCK_NUM], 
                     const int blkBefore[],            
                     Dimension block[][ORIENT_NUM], 
                     OTree& ot,                     
                     OwnQueue<int>& bContainer,                
                     FloorPlan& best);
// ========================================================
   
#endif
