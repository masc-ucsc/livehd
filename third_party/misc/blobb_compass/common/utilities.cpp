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
#include "utilities.hpp"

int counter[COUNTER_LENGTH][COUNTER_SIZE];
string counterName[COUNTER_SIZE];

// ========================================================
//             RECORD_H IMPLEMENTATION
// ========================================================
void InitializeCounter()
{
   for (int i = 0; i < COUNTER_SIZE; i++)
      for (int j = 0; j < COUNTER_SIZE; j++)
         counter[i][j] = 0;
}
// --------------------------------------------------------
void OutputCounter(ostream& outs,
                   int columnNum,
                   int rowNum)
{
   int sum = 0;
   outs << "-size-";
   for (int i = 0; i < columnNum; i++)
   {
      int spaceNum = max(0ul, 10-counterName[i].length());
      outs << "-" << counterName[i];
      for (int j = 0; j < spaceNum; j++)
         outs << "-";
      outs << "-";
   }
   outs << endl;

   for (int i = 1; i < rowNum; i++)
   {
      outs << setw(4) << i << "  ";
      for (int j = 0; j < columnNum; j++)
         outs << setw(12) << counter[i][j];
      outs << endl;
      sum += counter[i][0];
   }
   outs << "----" << endl << "sum:  " << setw(12) << sum << endl;
}
// --------------------------------------------------------


// ========================================================
//                CPUUSAGE_H IMPLEMENTATION
// ========================================================
double getTotalTime()
{
	// struct to store return values for getrusage()
	// this struct is defined in the manpages
	struct rusage infoStruct;
	
	// getrusage() returns seconds and fractions of a second (as micro-
	// seconds) in separate variables for both system time and
	// user time
	getrusage(RUSAGE_SELF, &infoStruct);
	
	// total time is defined as system time + user time
	double totalTime = infoStruct.ru_stime.tv_sec + 
	  infoStruct.ru_utime.tv_sec + (double(infoStruct.ru_stime.tv_usec +
	  infoStruct.ru_utime.tv_usec) / 1000000);
	  
	return totalTime;
}
// --------------------------------------------------------
