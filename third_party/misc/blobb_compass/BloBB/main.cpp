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
#include "interface.hpp"
#include "nonslice.hpp"
#include "slice.hpp"
#include "slicehier.hpp"
#include "parameters.hpp"

#include "utilities.hpp"

#include <iostream>
using namespace std;

int main(int argc, char *argv[])
{
   CommandOptions choice = {"", "", -1};
   ParseCommandLine(argc, argv, choice);

   cout << "===== BloBB (Block-packing with Branch-and-bound) =====" << endl;
   cout << "floorplanType: " << choice.floorplanType << endl;
   cout << "algorithmType: " << choice.algorithmType << endl;
   cout << "block orientn: ";
   switch (ENG_ORIENT_CONSIDERED)
   {
   case 1: cout << "--fixed-orient" << endl;
      break;
   case 2: cout << "--free-orient" << endl;
      break;
   default: cout << "ERROR: invalid value for \"ENG_ORIENT_CONSIDERED\" ("
                 << ENG_ORIENT_CONSIDERED << ")." << endl;
      exit(1);
      break;
   }

   if (choice.algorithmType == "--backtrack")
      PrintExtraBacktrack(choice);
   else if (choice.algorithmType == "--optimal" ||
            choice.algorithmType == "--enumerate")
      PrintExtraEng();
   else if (choice.algorithmType == "--hierarchical")
      PrintExtraHierarchical();
   cout << "=======================================================" << endl;

   if (choice.floorplanType == "--slicing")
   {
      if (choice.algorithmType == "--optimal" ||
          choice.algorithmType == "--backtrack" ||
          choice.algorithmType == "--enumerate")
         SliceEngine(argv, choice);
      else
         SliceHierEngine(argv, choice);
   }
   else if (choice.floorplanType == "--non-slicing")
   {
      if (choice.algorithmType == "--optimal" ||
          choice.algorithmType == "--backtrack" ||
          choice.algorithmType == "--enumerate")
         NonsliceEngine(argv, choice);
      else
         cout << "Sorry!  Currently, non-slicing hierarchical block-packing "
              << "is not supported." << endl;
   }

   cout.setf(ios::fixed);
   cout.precision(2);
   cout << "BloBB's total runtime: " << getTotalTime() << "s" << endl;
   cout << "===== Thanks for using BloBB =====" << endl;
   return 0;
}
