/**************************************************************************
***    
*** Copyright (c) 2005 Regents of the University of Michigan,
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
#ifndef DATASTROMERGEST_H
#define DATASTROMERGEST_H

#include "datastr.hpp"
#include "datastrst.hpp"

// --------------------------------------------------------
class HardClusterRecord
{
public:
   HardClusterRecord(const double width_,
                     const double height_,
                     const vector<int>& expression_,
                     const vector<int>& orient_)
      : width(width_),
        height(height_),
        expression(expression_),
        orient(orient_)
      {}

   const double width;
   const double height;
   const vector<int> expression;
   const vector<int> orient;

   inline static bool maxSideLessThan(const HardClusterRecord& cr1,
                                      const HardClusterRecord& cr2);
   inline static bool areaLessThan(const HardClusterRecord& cr1,
                                   const HardClusterRecord& cr2);
};
// --------------------------------------------------------
// used to store temp results, indices in _clusters[i]
// refer to a cluster, not necessary a signle block
class ClusterSetRecord
{
public:
   ClusterSetRecord();

   void opertor =(const STree& stree);

   inline double blockArea() const;
   inline double totalArea() const;
   inline double deadspace() const;
   inline const HardClusterRecord& clusters(int index) const;
   
protected:
   double _blockArea;
   double _totalArea;
   double _deadspace;

   vector<HardClusterRecord> _clusters; 
};
// --------------------------------------------------------

#endif
