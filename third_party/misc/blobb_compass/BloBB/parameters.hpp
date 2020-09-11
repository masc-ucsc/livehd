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
#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <string>
using namespace std;

// defined in interface.cpp
// mainly used by nonslice.cpp, enginest.cpp/slice.cpp
const double INF_DEADSPACE_DEFAULT = 0.04;
const string INF_EXTRA_PREFIX = "EXTRA: ";

extern string INF_FN_PREFIX;
extern string INF_FN_SUFFIX;
extern bool INF_SHOW_INTERMEDIATES;
extern bool INF_SHOW_LANDMARKS;
extern bool INF_SHOW_PRUNED_TABLE;
extern bool INF_SHOW_SIMILARITY_TABLE;
extern bool INF_SHOW_POLISH_EXPRESSION;

// defined in parameters.cpp
// used in nonslice.cpp, enginest.cpp/slice.cpp
const int ENG_UNDEFINED_SENTINEL = -1;
const int ENG_ORIENT_CONSIDERED_DEFAULT = 2;
const double ENG_DEADSPACE_INCRE_DEFAULT = 1.05;
const double ENG_INIT_DEADSPACE_PERCENT_FIXED_DEFAULT = 0.10;
const double ENG_INIT_DEADSPACE_PERCENT_FREE_DEFAULT = 0.05;

extern int ENG_ORIENT_CONSIDERED;
extern double ENG_DEADSPACE_INCRE;
extern double ENG_INIT_DEADSPACE_PERCENT;

// defined in parameters.cpp
// used in bound.cpp, boundst.cpp
extern double BT_DEADSPACE_PERCENT;
extern double BT_MIN_TOTAL;
extern double BT_MIN_DEADSPACE;

// defined in enginehierst.cpp
// used in enginehierst.cpp
const int HIER_CLUSTER_BASE_DEFAULT = 8;
const double HIER_AR_FIXED_DEFAULT = 1.5;
const double HIER_AR_FREE_DEFAULT = 1.5;
const double HIER_AR_INCRE_DEFAULT = 1.5;
const double HIER_BEST_AREA_INCRE_FIXED_DEFAULT = 1.10;
const double HIER_BEST_AREA_INCRE_FREE_DEFAULT = 1.05;
const double HIER_CLUSTER_AREA_DEV_DEFAULT = 2;
const double HIER_SIDE_RESOLUTION_DEFAULT = 1.9;
const unsigned int HIER_SORT_VEC_MAX_SIZE = 300000;
const bool HIER_COMPACT_DEFAULT = false;

const int HIER_UNDEFINED_SENTINEL = -1;
extern int HIER_CLUSTER_BASE;
extern bool HIER_USE_AR;  
extern double HIER_AR;    
extern double HIER_AR_INCRE;
extern double HIER_BEST_AREA_INCRE;
extern double HIER_CLUSTER_AREA_DEV;
extern double HIER_SIDE_RESOLUTION;
extern const unsigned int HIER_SORT_VEC_MAX_SIZE;
extern bool HIER_COMPACT;
extern bool HIER_OPTOPR;

#endif
