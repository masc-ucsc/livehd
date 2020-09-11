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
#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>
#include <algorithm>
#include <iomanip>

#include <sys/time.h>
#include <sys/resource.h>
#include <iostream>
using namespace std;

// ========
// RECORD_H
// ========
// --------------------------------------------------------
const int COUNTER_SIZE = 20;
const int COUNTER_LENGTH = 100;
extern int counter[COUNTER_LENGTH][COUNTER_SIZE];
extern string counterName[COUNTER_SIZE];  // length <= 10

void InitializeCounter();
void OutputCounter(ostream& outs, int columnNum, int rowNum);
// --------------------------------------------------------

// ==========
// CPUUSAGE_H
// ==========
// --------------------------------------------------------
// EECS 281: Algorithms and Data Structures
// Project 3
// getrusage() demo
//
// by David Zohrob, dzohrob@umich.edu; please e-mail
// with comments or corrections.
//
// This program sorts as much of a given array as it can
// using BubbleSort, and quits after a specified time limit
// suppplied in seconds


// array size for bubble sort example
#define ARRAY_SIZE 10000

// time limit
#define TIME_LIMIT 10

// these definitions are provided in the getrusage() man pages
// you can consider them to be "magic" if you like - they deal
// with process hierarchies in UNIX.

#ifndef RUSAGE_SELF
#define RUSAGE_SELF      0              /* calling process */
#endif

#ifndef RUSAGE_CHILDREN
#define RUSAGE_CHILDREN  -1     /* terminated child processes */
#endif

using namespace std;

// this function uses getrusage() to return the total time your
// program has been running in both system and user modes
double getTotalTime();
// --------------------------------------------------------

#endif
