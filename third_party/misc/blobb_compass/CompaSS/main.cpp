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
#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"
#include "datastrhiersoftst.hpp"
#include "enginesoftst.hpp"
#include "enginehiersoftst.hpp"

#include "mainsoftst.hpp"
#include "mainhiersoftst.hpp"
#include <cstring>

#include "utilities.hpp"

// --------------------------------------------------------
int main(int argc, char *argv[])
{
   int algorithm = -1;

   if (argc < 4)
   {
      cout << "USAGE: compass <input-file> <output-file> "
           << "<input-format> [options] " << endl;
      exit(0);
   }
   
   cout << "===== CompaSS (Compacting Soft Slicing Packings) =====" << endl;
   for (int i = argc-1; i >= 0; i--)
      if (!strcmp(argv[i], "--optimal") ||
          !strcmp(argv[i], "-o"))
         algorithm = SoftEngineType::B_BOUND;
      else if (!strcmp(argv[i], "--hierarchical") ||
               !strcmp(argv[i], "-h"))
         algorithm = SoftEngineType::HIER;

   if (algorithm == -1)
   {
      cout << "WARNING: Algorithm not chosen, \"--hierarchical\" assumed."
           << endl;
      main_hier(argc, argv);
   }
   else if (algorithm == SoftEngineType::B_BOUND)
      main_optimal(argc, argv);
   else if (algorithm == SoftEngineType::HIER)
      main_hier(argc, argv);

   cout << "CompaSS's runtime: " << getTotalTime() << "s" << endl;
   cout << "===== Thanks for using CompaSS =====" << endl;
   return 0;
}
// --------------------------------------------------------
