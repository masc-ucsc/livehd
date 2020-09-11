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
#include "mainsoftst.hpp"
#include "interfaceutil.hpp"

#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"
#include "datastrhiersoftst.hpp"
#include "enginesoftst.hpp"

#include <iostream>
#include <fstream>
using namespace std;

// --------------------------------------------------------
void main_optimal(int argc, char *argv[])
{
   ifstream infile;
   ofstream outfile;

   cout << "---< OPTIMAL Packing >---" << endl;
   infile.open(argv[1]);
   if (!infile.good())
   {
      cout << "ERROR: cannot open file: " << argv[1] << endl;
      exit(1);
   }

   outfile.open(argv[2]);
   if (!outfile.good())
   {
      cout << "ERROR: cannot open file: " << argv[3] << endl;
      exit(1);
   }

   int format = -1;
   if (!strcmp(argv[3], "--txt"))
      format = BlockInfoType::TXT;
   else if (!strcmp(argv[3], "--soft"))
      format = BlockInfoType::SOFT;
   else
   {
      cout << "ERROR: unrecognized format: " << argv[3]
           << " (--txt or --soft)." << endl;
      exit(1);
   }

   // optional options
   for (int i = 4; i < argc; i++)
      if (!strcmp(argv[i], "--INIT_DEADSPACE"))
      {
         i++;
         SetDouble(argc, argv, i, SoftEngineType::INIT_DEADSPACE);
         SoftEngineType::INIT_DEADSPACE /= 100;
      }
      else if (!strcmp(argv[i], "--DEADSPACE_INCRE"))
      {
         i++;
         SetDouble(argc, argv, i, SoftEngineType::DEADSPACE_INCRE);
      }
      else if (!strcmp(argv[i], "-o") ||
               !strcmp(argv[i], "--optimal"))
      {}
      else if (!strcmp(argv[i], "--fixed"))
      {
         if (format != BlockInfoType::TXT)
         {
            cout << "ERROR: \"--fixed\" must be used with "
                 << "\"--txt\"." << endl;
            exit(1);
         }
         else
            format = BlockInfoType::TXT_FIXED;
      }
      else if (!strcmp(argv[i], "--AREA_ACCURACY"))
      {
         i++;
         SetDouble(argc, argv, i, OPTIMAL_ACCURACY);
         OPTIMAL_ACCURACY /= 100;
         OPTIMAL_ACCURACY += 1;
      }
      else if (!strcmp(argv[i], "--CURVE_ACCURACY"))
      {
         i++;
         SetInt(argc, argv, i, BlockInfoType::CURVE_ACCURACY);
      }
      else
      {
         cout << "ERROR: unrecognized option: " << argv[i] << endl;
         exit(1);
      }

   int mode = SoftEngineType::EXPLICIT;
   int algo = SoftEngineType::B_BOUND;
   cout << "Algorithm: ";
   switch(algo)
   {
   case SoftEngineType::B_BOUND:
      cout << "Branch-and-bound." << endl;
      break;
   }
   cout.setf(ios::fixed);
   cout.precision(2);
   cout << "  - Block type: ";
   if (format == BlockInfoType::TXT)
      cout << "all hard, free orientations." << endl;
   else if (format == BlockInfoType::TXT_FIXED)
      cout << "all hard, fixed orientations." << endl;
   else
      cout << "all soft." << endl;
   cout << "  - Area accuracy: " << (OPTIMAL_ACCURACY - 1) * 100
        << "%." << endl;
   if (format == BlockInfoType::SOFT)
      cout << "  - Curve accuracy: " << BlockInfoType::CURVE_ACCURACY << endl;
   
   cout << "  - Initial deadspace: " << (SoftEngineType::INIT_DEADSPACE * 100)
        << "%" << endl;
   cout << "  - Deadspace increment: " << SoftEngineType::DEADSPACE_INCRE << endl;
   cout << endl;

   counterName[0] = "# nodes";
   counterName[1] = "operand";
   counterName[2] = "left";
   counterName[3] = "operator";
   counterName[4] = "left";

   // -----core program starts here-----
   BlockInfoType blockinfo(format, infile);
   SoftEngineType engine(blockinfo);

   SoftSliceRecordList *ssr_ptr = engine(algo, mode);

   cout << "solution found: " << endl;
   SoftPacking(ssr_ptr->last(), blockinfo).output(outfile);
   SoftPacking(ssr_ptr->last(), blockinfo).output(cout);

   if (outfile.good())
      cout << "Output successfully written to " << argv[2] << endl;
   else
      cout << "Something wrong with the file " << argv[2] << endl;
          
   outfile.close();
   delete ssr_ptr;
}
// --------------------------------------------------------
