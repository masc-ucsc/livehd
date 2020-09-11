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
#include "mainhiersoftst.hpp"
#include "interfaceutil.hpp"

#include "datastrbacksoftst.hpp"
#include "datastrfrontsoftst.hpp"
#include "datastrhiersoftst.hpp"
#include "enginesoftst.hpp"
#include "enginehiersoftst.hpp"

#include "btreefromsstree.hpp"
#include "btreecompactsstree.hpp"

#include "parsers.hpp"

#include <string>
#include <set>
#include <utility>
using namespace std;

// --------------------------------------------------------
void main_hier(int argc, char *argv[])
{   
   ifstream infile;
   ofstream outfile;

   cout << "---< HIERARCHICAL Packing >---" << endl;
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
      cout << "ERROR: invalid input format \"" << argv[3]
           << "\"." << endl;
      exit(1);
   }

   // -----set default values-----
   if (format == BlockInfoType::TXT)
      HIER_ACCURACY = DEFAULT_HIER_TXT_ACCURACY;
   else if (format == BlockInfoType::SOFT)
      HIER_ACCURACY = DEFAULT_HIER_SOFT_ACCURACY;
   
   // -----parse optional options-----
   set<string> userDefinedOptions; // to be excluded from parameter tuning
   
   bool option_compact = true;
   double option_outline_ar = Point::INFTY;
   double option_outline_deadspace = Point::INFTY;
   for (int i = 4; i < argc; i++)
   {
      const int optionPosition = i;
      bool validOption = true;
      if (!strcmp(argv[i], "--fixed"))
      {
         if (format != BlockInfoType::TXT)
         {
            cout << "ERROR: \"--fixed\" must be used with \"--txt\"."
                 << endl;
            exit(1);
         }
         else
            format = BlockInfoType::TXT_FIXED;
      }
      else if (!strcmp(argv[i], "--HIER_CLUSTER_BASE"))
      {
         i++;
         SetInt(argc, argv, i,
                SoftHierEngineType::HIER_CLUSTER_BASE);
      }
      else if (!strcmp(argv[i], "--HIER_CLUSTER_AREA_DEV"))
      {
         i++;
         SetDouble(argc, argv, i,
                   SoftHierEngineType::HIER_CLUSTER_AREA_DEV);
      }
      else if (!strcmp(argv[i], "--HIER_INIT_AR"))
      {
         i++;
         SetDouble(argc, argv, i,
                   SoftHierEngineType::HIER_AR);
      }
      else if (!strcmp(argv[i], "--HIER_WIDTH_INCRE"))
      {
         i++;
         SetDouble(argc, argv, i,
                   SoftHierEngineType::HIER_WIDTH_INCRE);
      }
      else if (!strcmp(argv[i], "--HIER_HEIGHT_INCRE"))
      {
         i++;
         SetDouble(argc, argv, i,
                   SoftHierEngineType::HIER_HEIGHT_INCRE);
      }
      else if (!strcmp(argv[i], "--HIER_USE_AR_LEVEL"))
      {
         i++;
         SetInt(argc, argv, i, 
                SoftHierEngineType::HIER_USE_AR_LEVEL);
      }
      else if (!strcmp(argv[i], "--HIER_INIT_DEADSPACE"))
      {
         i++;
         SetDouble(argc, argv, i,
                   SoftHierEngineType::HIER_INIT_DEADSPACE);
         SoftHierEngineType::HIER_INIT_DEADSPACE /= 100;
      }
      else if (!strcmp(argv[i], "--HIER_DEADSPACE_INCRE"))
      {
         i++;
         SetDouble(argc, argv, i,
                   SoftHierEngineType::HIER_DEADSPACE_INCRE);
      }
      else if (!strcmp(argv[i], "--HIER_SIMILARITY_THRESHOLD"))
      {
         i++;
         SetDouble(argc, argv, i,
                   SoftHierEngineType::HIER_SIMILARITY_THRESHOLD);
      }
      else if (!strcmp(argv[i], "--HIER_OUTLINE_AR"))
      {
         i++;
         SetDouble(argc, argv, i, option_outline_ar);
      }
      else if (!strcmp(argv[i], "--HIER_OUTLINE_DEADSPACE"))
      {
         i++;
         SetDouble(argc, argv, i, option_outline_deadspace);
         option_outline_deadspace /= 100;
      }
      else if (!strcmp(argv[i], "--AREA_ACCURACY"))
      {
         i++;
         SetDouble(argc, argv, i, HIER_ACCURACY);
         HIER_ACCURACY /= 100;
         HIER_ACCURACY += 1;
      }
      else if (!strcmp(argv[i], "--CURVE_ACCURACY"))
      {
         i++;
         SetInt(argc, argv, i, BlockInfoType::CURVE_ACCURACY);
      }
      else if (!strcmp(argv[i], "-h") ||
               !strcmp(argv[i], "--hierarchical"))
      {}
      else if (!strcmp(argv[i], "--no_compact"))
         option_compact = true;
      else
      {
         validOption = false;
         cout << "ERROR: Invalid option: " << argv[i] << endl;
         exit(1);
      }

      if (validOption)
      {
         pair<set<string>::iterator, bool> 
            status = userDefinedOptions.insert(argv[optionPosition]);
         if (!status.second)
         {
            cout << "WARNING: duplicated option: " << argv[optionPosition] << endl;
         }
      }
   } // end for each identier on the command-line

   // -----output parameters-----
   cout.setf(ios::fixed);
   cout.precision(2);
   cout << "Details: " << endl;
   cout << "  - Block type: ";
   if (format == BlockInfoType::TXT)
      cout << "all hard, free orientations." << endl;
   else if (format == BlockInfoType::TXT_FIXED)
      cout << "all hard, fixed orientations." << endl;
   else if (format == BlockInfoType::SOFT)
   {
      cout << "all soft ";
      cout << "(Curve Accuracy = " << BlockInfoType::CURVE_ACCURACY
           << ")." << endl;
   }
   cout << "  - Area accuracy: " << ((HIER_ACCURACY-1) * 100)
        << "%." << endl;      

   // -----Core program starts here-----
   BlockInfoType blockinfo(format, infile);
   const bool fixed_outline = ((option_outline_ar < Point::INFTY) &&
                               (option_outline_deadspace < Point::INFTY));
   const double side_bound = sqrt(blockinfo.block_area() * option_outline_deadspace *
                                  option_outline_ar);
   Point::X_BOUND = (!fixed_outline)? Point::INFTY : side_bound;
   Point::Y_BOUND = (!fixed_outline)? Point::INFTY : side_bound;

   // evoke the hierarchical block packing engine
   SoftHierEngineType engine((format == BlockInfoType::SOFT)
                             ? SoftHierEngineType::SOFT_ONLY 
                             : SoftHierEngineType::HARD_ONLY,
                             blockinfo, &userDefinedOptions);
   SoftSliceRecord *ssrPtr = engine(format);

   // -----Post processing: optimizing operators of PE-----
   cout << endl << "Optimizing operators of the Polish expression";
   if (option_outline_ar < Point::INFTY)
   {
      cout << " (aspect ratio ~" << option_outline_ar << ")..."
           << endl;   
      if (option_outline_deadspace >= Point::INFTY)
      {
         Point::X_BOUND = sqrt(ssrPtr->minArea * option_outline_ar);
         Point::Y_BOUND = sqrt(ssrPtr->minArea * option_outline_ar);
      }
   }
   else
      cout << " (no aspect ratio constraints)..." << endl;
      
   SoftPacking final_packing(*ssrPtr, blockinfo);
   double final_blkArea = final_packing.blockArea;
   double final_deadspace = final_packing.deadspace;
   cout << endl;
   cout << "After operator optimization," << endl;
   cout << "blkArea: " << setw(11) << final_blkArea
        << " deadspace: " << setw(11) << final_deadspace
        << " (" << ((final_deadspace / final_blkArea) * 100)
        << "%) time: " << getTotalTime() << endl;
   cout << endl;
   PrintDimensions(final_packing.totalWidth,
                   final_packing.totalHeight);
   PrintAreas(final_deadspace, final_blkArea);
   cout << endl;
   PrintUtilization(final_deadspace, final_blkArea);
   
   // -----Post processing:  extended for compaction -----
   if (option_compact)
   {
      cout << endl << "Compacting..." << endl;
      BTreeCompactSlice(final_packing, argv[2]);
   }
   else
   {
      final_packing.output(outfile);      
      if (outfile.good())
         cout << "Output successfully written to " << argv[2] << endl;
      else
         cout << "Something wrong with the file " << argv[2] << endl;
   }
   cout << endl;
   outfile.close();
   delete ssrPtr;
}
// --------------------------------------------------------
