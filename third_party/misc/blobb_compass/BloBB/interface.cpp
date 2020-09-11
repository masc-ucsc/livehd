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
#include "datastrst.hpp"
#include "parameters.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <iomanip>
#include <cstdlib>
using namespace std;

string INF_FN_PREFIX("");
string INF_FN_SUFFIX(".bbb");
bool INF_SHOW_INTERMEDIATES = true;
bool INF_SHOW_LANDMARKS = true;
bool INF_SHOW_PRUNED_TABLE = true;
bool INF_SHOW_SIMILARITY_TABLE = true;
bool INF_SHOW_POLISH_EXPRESSION = true;
// --------------------------------------------------------
void ParseCommandLine(int argc,
                      char *argv[],
                      CommandOptions& choice)
{
   ifstream infile;
   ofstream outfile;
   
   if (argc < 2)
   {
      PrintUsage();
      cout << "       for details, type the option \"--help\"." << endl;
      exit(1);
   }
   else if (!strcmp(argv[1], "--help"))
   {
      PrintHelp();
      exit(0);
   }
   else if (argc < 3)
   {
      cout << "USAGE: " << argv[0] << " <input-file> <output-file> "
           << "[-options]" << endl;
      cout << "       for details, type the option \"--help\"." << endl;
      exit(1);
   }

   infile.open(argv[1]);
   if (!infile.good())
   {
      cout << "ERROR: cannot open input file \"" << argv[1]
           << "\"." << endl;
      exit(1);
   }
   infile.close();

   outfile.open(argv[2]);
   if (!outfile.good())
   {
      cout << "ERROR: cannot open output file \"" << argv[2]
           << "\"." << endl;
      exit(1);
   }
   outfile.close();

   for (int i = 3; i < argc; i++)
   {
      if (!strcmp(argv[i], "--slicing") ||
          !strcmp(argv[i], "-s"))
         SetOption(choice.floorplanType, "--slicing");
      else if (!strcmp(argv[i], "--non-slicing") ||
               !strcmp(argv[i], "--general") ||
               !strcmp(argv[i], "-n"))
         SetOption(choice.floorplanType, "--non-slicing");
      else if (!strcmp(argv[i], "--optimal") ||
               !strcmp(argv[i], "-o"))
         SetOption(choice.algorithmType, "--optimal");
      else if (!strcmp(argv[i], "--hierarchical") ||
               !strcmp(argv[i], "-h"))
         SetOption(choice.algorithmType, "--hierarchical");
      else if (!strcmp(argv[i], "--backtrack") ||
               !strcmp(argv[i], "-b"))
         SetOption(choice.algorithmType, "--backtrack");
      else if (!strcmp(argv[i], "--enumerate") ||
               !strcmp(argv[i], "-e"))
         SetOption(choice.algorithmType, "--enumerate");
      else if (!strcmp(argv[i], "--deadspace_percent") ||
               !strcmp(argv[i], "-d"))
      {
         i++;
         SetDeadspacePercent(argc, argv, i, choice);
      }
      else if (!strcmp(argv[i], "--file_prefix") ||
               !strcmp(argv[i], "-f"))
      {
         i++;
         SetFNPrefix(argc, argv, i);
      }
      // block orientation constraints
      else if (!strcmp(argv[i], "--free-orient") ||
	       !strcmp(argv[i], "-fr"))
	 ENG_ORIENT_CONSIDERED = 2;
      else if (!strcmp(argv[i], "--fixed-orient") ||
	       !strcmp(argv[i], "-fx"))
	 ENG_ORIENT_CONSIDERED = 1;
      // numerical parameters for slice/nonslice
      else if (!strcmp(argv[i], "--ENG_DEADSPACE_INCRE"))
      {
         i++;
         SetDouble(argc, argv, i, ENG_DEADSPACE_INCRE);
      }
      else if (!strcmp(argv[i], "--ENG_INIT_DEADSPACE_PERCENT"))
      {
         i++;
         SetDouble(argc, argv, i, ENG_INIT_DEADSPACE_PERCENT);
         ENG_INIT_DEADSPACE_PERCENT /= 100;
      }
      // numerical parameters for hierarchical slicing
      else if (!strcmp(argv[i], "--HIER_CLUSTER_BASE"))
      {
         i++;
         SetInt(argc, argv, i, HIER_CLUSTER_BASE);
      }
      else if (!strcmp(argv[i], "--HIER_AR"))
      {
         i++;
         SetDouble(argc, argv, i, HIER_AR);
      }
      else if (!strcmp(argv[i], "--HIER_AR_INCRE"))
      {
         i++;
         SetDouble(argc, argv, i, HIER_AR_INCRE);
      }
      else if (!strcmp(argv[i], "--HIER_BEST_AREA_INCRE"))
      {
         i++;
         SetDouble(argc, argv, i, HIER_BEST_AREA_INCRE);
      }
      else if (!strcmp(argv[i], "--HIER_CLUSTER_AREA_DEV"))
      {
         i++;
         SetDouble(argc, argv, i, HIER_CLUSTER_AREA_DEV);
      }
      else if (!strcmp(argv[i], "--HIER_SIDE_RESOLUTION"))
      {
         i++;
         SetDouble(argc, argv, i, HIER_SIDE_RESOLUTION);
      }
      else if (!strcmp(argv[i], "--no_compact"))
      {
         HIER_COMPACT = false;
      }
      else if (!strcmp(argv[i], "--no_opt_opr"))
      {
         HIER_OPTOPR = false;
      }
      // interface boolean options
      else if (!strcmp(argv[i], "--verbose") ||
               !strcmp(argv[i], "-v"))
      {
         INF_SHOW_INTERMEDIATES = true;
         INF_SHOW_LANDMARKS = true;
         INF_SHOW_PRUNED_TABLE = true;
         INF_SHOW_SIMILARITY_TABLE = true;
         INF_SHOW_POLISH_EXPRESSION = true;
      }
      else if (!strcmp(argv[i], "--terse") ||
               !strcmp(argv[i], "-t"))
      {
         INF_SHOW_INTERMEDIATES = false;
         INF_SHOW_LANDMARKS = false;
         INF_SHOW_PRUNED_TABLE = false;
         INF_SHOW_SIMILARITY_TABLE = false;
         INF_SHOW_POLISH_EXPRESSION = false;
      }
      else if (!strcmp(argv[i], "--INF_SHOW_INTERMEDIATES"))
         INF_SHOW_INTERMEDIATES = true;
      else if (!strcmp(argv[i], "--nINF_SHOW_INTERMEDIATES"))
         INF_SHOW_INTERMEDIATES = false;
      else if (!strcmp(argv[i], "--INF_SHOW_LANDMARKS"))
         INF_SHOW_LANDMARKS = true;
      else if (!strcmp(argv[i], "--nINF_SHOW_LANDMARKS"))
         INF_SHOW_LANDMARKS = false;
      else if (!strcmp(argv[i], "--INF_SHOW_PRUNED_TABLE"))
         INF_SHOW_PRUNED_TABLE = true;
      else if (!strcmp(argv[i], "--nINF_SHOW_PRUNED_TABLE"))
         INF_SHOW_PRUNED_TABLE = false;
      else if (!strcmp(argv[i], "--INF_SHOW_SIMILARITY_TABLE"))
         INF_SHOW_SIMILARITY_TABLE = true;
      else if (!strcmp(argv[i], "--nINF_SHOW_SIMILARITY_TABLE"))
         INF_SHOW_SIMILARITY_TABLE = false;
      else if (!strcmp(argv[i], "--INF_SHOW_POLISH_EXPRESSION"))
         INF_SHOW_POLISH_EXPRESSION = true;
      else if (!strcmp(argv[i], "--nINF_SHOW_POLISH_EXPRESSION"))
         INF_SHOW_POLISH_EXPRESSION = false;
      else
      {
         cout << "ERROR: invalid option \"" << argv[i]
              << "\"." << endl;
         exit(1);
      }
   }
   
   // ----after command line----
   CheckDeadspace(choice);
   CheckFloorplanType(choice);
   CheckAlgorithmType(choice);
   CheckOrient();
   CheckFNPrefix(choice, INF_FN_PREFIX);
   if (choice.algorithmType == "--optimal" ||
       choice.algorithmType == "--backtrack" ||
       choice.algorithmType == "--enumerate")
      CheckEng();
   if (choice.algorithmType == "--hierarchical")
      CheckHierarchical();
}           
// --------------------------------------------------------
void SetOption(string& option,
               const char *flag)
{
   if ((option != "") && (option != flag))
   {
      cout << "ERROR: options \"" << option << "\" and \""
           << flag << "\" cannot be used together." << endl;
      exit(1);
   }
   else
      option = flag;
}
// --------------------------------------------------------
void SetDeadspacePercent(int argc,
                         char *argv[],
                         int index,
                         CommandOptions& choice)
{
   if (argc <= index)
   {
      cout << "ERROR: must specified a number after label \""
           << argv[index-1] << "\"." << endl;
      exit(1);
   }
   else if (choice.dpercent != -1)
   {
      cout << "ERROR: multiple specification of \""
           << "--deadspace_percent\" (" << (choice.dpercent*100)
           << " vs. " << argv[index] << ")." << endl;
      exit(1);
   }         
   
   SetDouble(argc, argv, index, choice.dpercent);
   choice.dpercent /= 100;
   if (choice.dpercent < 0)
   {
      cout << "ERROR: deadspace_percent cannot be negative." << endl;
      exit(1);
   }
}
// --------------------------------------------------------
void SetFNPrefix(int argc,
                 char *argv[],
                 int index)
{
   if (argc <= index)
   {
      cout << "ERROR: must specified a number after label \""
           << argv[index-1] << "\"." << endl;
      exit(1);
   }
   else if (INF_FN_PREFIX != "")
   {
      cout << "ERROR: multiple specification of \""
           << "--file_prefix\" (" << INF_FN_PREFIX
           << " vs. " << argv[index] << ")." << endl;
      exit(1);
   }
   INF_FN_PREFIX = argv[index];
}
// --------------------------------------------------------
// void SetDouble(int argc,
//                char *argv[],
//                int index,
//                double& param)
// {
//    if (argc <= index)
//    {
//       cout << "ERROR: must specified a number after label \""
//            << argv[index-1] << "\"." << endl;
//       exit(1);
//    }
   
//    char **endp = new (char*);
//    endp[0] = new char[100];         
//    param = strtod(argv[index], endp);
   
//    if (strcmp(endp[0], ""))
//    {
//       cout << "ERROR: invalid number \""
//            << argv[index] << "\"." << endl;
//       delete endp; 
//       exit(1);
//    }
//    else
//       delete endp;
// } 
// // -------------------------------------------------------
// void SetInt(int argc,
//             char *argv[],
//             int index,
//             int& param)
// {
//    if (argc <= index)
//    {
//       cout << "ERROR: must specified a number after label \""
//            << argv[index-1] << "\"." << endl;
//       exit(1);
//    }
   
//    char **endp = new (char*);
//    endp[0] = new char[100];         
//    param = strtol(argv[index], endp, 10);
   
//    if (strcmp(endp[0], ""))
//    {
//       cout << "ERROR: invalid number \""
//            << argv[index] << "\"." << endl;
//       delete endp;
//       exit(1);
//    }
//    else
//       delete endp;
// }   
// ========================================================
void CheckDeadspace(CommandOptions& choice)
{
   if ((choice.algorithmType == "--backtrack") &&
       (choice.dpercent == -1))
   {
      cout.setf(ios::fixed);
      cout.precision(2);
      cout << "WARNING: Option \"--backtrack\" is chosen but "
           << "the deadspace threshold is not " << endl;
      cout << "         specified.  Default value "
           << (INF_DEADSPACE_DEFAULT * 100) << "% is used." << endl;
      choice.dpercent = INF_DEADSPACE_DEFAULT;
   }
}
// --------------------------------------------------------
void CheckFloorplanType(CommandOptions& choice)
{
   if (choice.floorplanType  == "")
   {
      cout << "WARNING: The type of floorplan is not specified, " << endl;
      cout << "         default option \"";
      choice.floorplanType = "--slicing";
      cout << choice.floorplanType << "\" is used." << endl;
   }
   
}
//.--------------------------------------------------------
void CheckAlgorithmType(CommandOptions& choice)
{
   if (choice.algorithmType == "")
   {
      cout << "WARNING: The type of algorithm is not specified, " << endl;
      cout << "         default option \"";
      if (choice.floorplanType == "--slicing")
         choice.algorithmType = "--hierarchical";
      else
         choice.algorithmType = "--optimal";
      cout << choice.algorithmType << "\" is used." << endl;
   }
}
// --------------------------------------------------------
void CheckFNPrefix(const CommandOptions& choice,
                   const string& INF_FN_PREFIX)
{
   if (choice.algorithmType == "--optimal" ||
       choice.algorithmType == "--backtrack" ||
       choice.algorithmType == "--enumerate")
      if (INF_FN_PREFIX == "")
      {
         cout << "WARNING: No file name prefix is provided.  " << endl;
         cout << "         Intermediate solutions are not saved." << endl;
      }
}
// --------------------------------------------------------
void CheckOrient()
{
   if (ENG_ORIENT_CONSIDERED == ENG_UNDEFINED_SENTINEL)
      ENG_ORIENT_CONSIDERED = ENG_ORIENT_CONSIDERED_DEFAULT;
}
// --------------------------------------------------------
void CheckEng()
{
   if (ENG_INIT_DEADSPACE_PERCENT == ENG_UNDEFINED_SENTINEL)
   {
      if (ENG_ORIENT_CONSIDERED == 1)
	 ENG_INIT_DEADSPACE_PERCENT = ENG_INIT_DEADSPACE_PERCENT_FIXED_DEFAULT;
      else if (ENG_ORIENT_CONSIDERED == 2)
	 ENG_INIT_DEADSPACE_PERCENT = ENG_INIT_DEADSPACE_PERCENT_FREE_DEFAULT;
   }

   if (ENG_DEADSPACE_INCRE == ENG_UNDEFINED_SENTINEL)
      ENG_DEADSPACE_INCRE = ENG_DEADSPACE_INCRE_DEFAULT;
}
// --------------------------------------------------------	 
void CheckHierarchical()
{
   if (HIER_CLUSTER_BASE == HIER_UNDEFINED_SENTINEL)
      HIER_CLUSTER_BASE = HIER_CLUSTER_BASE_DEFAULT;
   
   if (HIER_AR == HIER_UNDEFINED_SENTINEL)
   {
      if (ENG_ORIENT_CONSIDERED == 1)
	 HIER_AR = HIER_AR_FIXED_DEFAULT;
      else if (ENG_ORIENT_CONSIDERED == 2)
	 HIER_AR = HIER_AR_FREE_DEFAULT;
   }

   if (HIER_AR_INCRE == HIER_UNDEFINED_SENTINEL)
      HIER_AR_INCRE = HIER_AR_INCRE_DEFAULT;

   if (HIER_BEST_AREA_INCRE == HIER_UNDEFINED_SENTINEL)
   {
      if (ENG_ORIENT_CONSIDERED == 1)
	 HIER_BEST_AREA_INCRE = HIER_BEST_AREA_INCRE_FIXED_DEFAULT;
      else if (ENG_ORIENT_CONSIDERED == 2)
	 HIER_BEST_AREA_INCRE = HIER_BEST_AREA_INCRE_FREE_DEFAULT;
   }

   if (HIER_CLUSTER_AREA_DEV == HIER_UNDEFINED_SENTINEL)
      HIER_CLUSTER_AREA_DEV = 2;

   if (HIER_SIDE_RESOLUTION == HIER_UNDEFINED_SENTINEL)
      HIER_SIDE_RESOLUTION = HIER_SIDE_RESOLUTION_DEFAULT;
}      
// ========================================================
void PrintSimilarityTable(int blockNum,
                          const bool same[][MAX_BLOCK_NUM],
                          const int blkBefore[])
{
   for (int i = 0; i < blockNum; i++)
   {
      for (int j = 0; j < blockNum; j++)
         if (same[i][j])
            cout << "  T";
         else
            cout << "  -";
      cout << endl;
   }
   
   for (int i = 0; i < blockNum; i++)
      cout << setw(3) << blkBefore[i];
   cout << endl;
}
// --------------------------------------------------------
void PrintExtraBacktrack(const CommandOptions& choice)
{
   cout.setf(ios::fixed);
   cout.precision(2);
   cout << INF_EXTRA_PREFIX << "deadspace_percent="
        << (choice.dpercent*100) << "%." << endl;
}
// --------------------------------------------------------
void PrintExtraEng()
{
   cout.setf(ios::fixed);
   cout.precision(2);
   cout << INF_EXTRA_PREFIX << "initial deadspace="
	<< (ENG_INIT_DEADSPACE_PERCENT*100) << "% " << endl;
   cout << INF_EXTRA_PREFIX << "dead space increment="
	<< ENG_DEADSPACE_INCRE << endl;
}
// --------------------------------------------------------
void PrintExtraHierarchical()
{
   cout.setf(ios::fixed);
   cout.precision(2);
   cout << INF_EXTRA_PREFIX << "cluster base="
        << HIER_CLUSTER_BASE << endl;
   cout << INF_EXTRA_PREFIX << "aspect ratio tolerance="
        << HIER_AR << endl;
   cout << INF_EXTRA_PREFIX << "asepct ratio increment="
        << HIER_AR_INCRE << endl;
   cout << INF_EXTRA_PREFIX << "dead space increment="
        << HIER_BEST_AREA_INCRE << endl;
   cout << INF_EXTRA_PREFIX << "cluster area deviation="
        << HIER_CLUSTER_AREA_DEV << endl;
   cout << INF_EXTRA_PREFIX << "side resolution="
        << HIER_SIDE_RESOLUTION << endl;
   cout << INF_EXTRA_PREFIX << "optimize operators in the Polish expression? "
        << ((HIER_OPTOPR)? "Yes" : "No") << endl;
   cout << INF_EXTRA_PREFIX << "compact? "
        << ((HIER_COMPACT)? "Yes" : "No") << endl;
}
// --------------------------------------------------------
void PrintUsage()
{
   cout << "USAGE: blobb <input-file> <output-file> "
        << "[-options]" << endl;

}
// --------------------------------------------------------   
void PrintHelp()
{
   PrintUsage();
   cout << "=====Basic Options=====" << endl;
   cout << "[Floorplan Types]" << endl;
   cout << "\"-s\" or \"--slicing\" : \n";
   cout << "consider slicing packings only.\n";
   cout << "\"-n\" or \"--general\" or \"--non-slicing\" : \n";
   cout << "consider general packings that are not necessary slicing\n";
   cout << endl;
   cout << "[Algorithm Types]" << endl;
   cout << "\"-o\" or \"--optimal\" : \n";
   cout << "find an optimal packing by branch-and-bound.\n";
   cout << "\"-b\" or \"--backtrack\" : \n";
   cout << "find a packing with deadspace smaller than a given percentage.\n";
   cout << "That percentage is specified by following \"-d\" or "
        << "\"--deadspace_percent\".\n";
   cout << "\"-e\" or \"--enumerate\" : \n";
   cout << "enumerate all non-symmetric optimal packings.  The last packing\n";
   cout << "enumerated is stored in <outfile> while the rest (and the last)\n";
   cout << "are stored in the intermediate files (see [Intermediate"
        << " Packings]).\n";
   cout << "\"-h\" or \"--hierarchical\" : \n";
   cout << "find a sub-optimal packing using multilevel branch-and-bound.\n";
   cout << endl;
   cout << "[Other Options]" << endl;
   cout << "\"-f <filename>\" or \"--file_prefix <filename>\" : \n";
   cout << "specifies the prefix of the series files where the intermediate\n";
   cout << "packings are stored, and the file names are extended with .txt\n";
   cout << "\"-f\" can be used with all algorithm types, even though it is \n";
   cout << "intended for non-symmetric packing enumeration (see [Algorithm "
        << "Types]).\n";
   cout << "If no intermediate file is specified, no intermediate packings\n";
   cout << "are saved.\n";
   cout << "\"-d <percent>\" or \"--deadspace_percent <percent>\" : \n";
   cout << "specifies the maximum amount of deadspace allowed (see [Algorithm "
        << "Types]).\n";
   cout << endl;
   cout << "[Examples]" << endl;
   cout << "[0] blobb infile outfile -n --optimal \n";
   cout << "[1] blobb infile outfile -s -b -d 4.3 \n";
   cout << "[2] blobb infile outfile -s -e -f dummy \n\n";
   cout << "[0] searches for an optimal non-slicing packing.  [1] looks for\n";
   cout << "a packing with less than 4.30% deadspace.  [2] enumerates all \n";
   cout << "non-symmetric optimal slicing packings.  All the intermediate \n";
   cout << "packings are stored in dummy0.txt dummy1.txt dummy2.txt etc.\n";
   cout << endl;
   cout << "For more information, please read \"doc.pdf\".\n";
}   
// ========================================================

