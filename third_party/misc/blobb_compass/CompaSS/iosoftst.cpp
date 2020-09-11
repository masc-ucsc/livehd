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

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
using namespace std;

// ========================================================
void BlockInfoType::ReadSoftFormat(int accuracy,
                                   istream& ins)
{
   int blocknum;
   ins >> blocknum;
   ins.ignore(1000, '\n');
   
   for (int i = 0; i < blocknum; i++)
   {
      // read the information for blocks[j]
      double area;
      vector<double> minAR;
      vector<double> maxAR;
      string line;      
      getline(ins, line);

      stringstream ss;
      ss << line << " ";
      ss >> area;
      while (ss.good())
      {
         double min_ar, max_ar;
         ss >> min_ar >> max_ar;
         if (ss.good())
         {
            minAR.push_back(min_ar);
            maxAR.push_back(max_ar);
         }
      }

      if (minAR.empty())
      {
         cout << "ERROR: error in reading block[" << i << "]" << endl;
         exit(1);
      }

      // construct curve for blocks[j]
      vector<Point> curve;
      for (int j = minAR.size()-1; j >= 0; j--)
      {
         double widthStart = sqrt(area / maxAR[j]);
         double widthEnd = sqrt(area / minAR[j]);
         double interval = (widthEnd-widthStart) / accuracy;

         if (j == int(minAR.size()-1))
         {
            // add the vertical infty part of the curve
            curve.push_back(Point(widthStart, Point::INFTY));
         }
         else
         {
            // add the vertical corner before this part
            double oldY = curve.back().yCoord();
            double newX = widthStart;
            curve.push_back(Point(newX, oldY));
         }
         
         if (maxAR[j] == minAR[j]) // rigid at that aspect ratio
         {
            double width = widthStart;
            double height = area / width;
            curve.push_back(Point(width, height));
         }
         else if (maxAR[j] > minAR[j]) // soft block
         {
            for (double width = widthStart;
                 abs(widthEnd-width) > 0.001; width += interval)
            {
               double height = area / width;
               curve.push_back(Point(width, height));
            }
         }
         else
         {
            cout << "ERROR: minAR (" << minAR[j] << ") greater than "
                 << "maxAR (" << maxAR[j] << ")." << endl;
            exit(1);
         }

         if (j == 0)
            curve.push_back(Point(Point::INFTY, area / widthEnd));
      }
      blocks.push_back(BoundaryType(curve));
      in_total_area += area;
   }
}
// --------------------------------------------------------
void BlockInfoType::ReadTxtFormat(istream& ins)
{
   int blocknum;
   ins >> blocknum;
   ins.ignore(1000, '\n');

   for (int i = 0; i < blocknum; i++)
   {
      double width, height;
      
      ins >> width >> height;
      if (!ins.good())
      {
         cout << "ERROR: error in reading block[" << i << "]." << endl;
         exit(1);
      }

      double maxEdge = max(width, height);
      double minEdge = min(width, height);
      vector<Point> curve;

      curve.push_back(Point(minEdge, Point::INFTY));
      curve.push_back(Point(minEdge, maxEdge));
      curve.push_back(Point(maxEdge, maxEdge));
      curve.push_back(Point(maxEdge, minEdge));
      curve.push_back(Point(Point::INFTY, minEdge));

      blocks.push_back(curve);
      in_total_area += width * height;
   }
}
// --------------------------------------------------------
void BlockInfoType::ReadTxtFixedFormat(istream& ins)
{
   int blocknum;
   ins >> blocknum;
   ins.ignore(1000, '\n');

   for (int i = 0; i < blocknum; i++)
   {
      double width, height;
      
      ins >> width >> height;
      if (!ins.good())
      {
         cout << "ERROR: error in reading block[" << i << "]." << endl;
         exit(1);
      }

      vector<Point> curve;

      curve.push_back(Point(width, Point::INFTY));
      curve.push_back(Point(width, height));
      curve.push_back(Point(Point::INFTY, height));

      blocks.push_back(curve);
      in_total_area += width * height;
   }
}     
// ========================================================
void SoftPacking::output(ostream& outs) const
{
   int blocknum = xloc.size();

   outs.setf(ios::fixed);
   outs.precision(3);
   outs << totalWidth << endl;
   outs << totalHeight << endl;
   
   outs << blocknum << endl;
   for (int i = 0; i < blocknum; i++)
      outs << width[i] << " " << height[i] << endl;
   outs << endl;

   for (int i = 0; i < blocknum; i++)
      outs << xloc[i] << " " << yloc[i] << endl;
   outs << endl;
   
   outs << endl << endl;
   for (unsigned int i = 0; i < expression.size(); i++)
   {
      int sign = expression[i];
      if (SoftSTree::isOperand(sign))
         outs << sign << " ";
      else if (sign == SoftSTree::STAR)
         outs << "* ";
      else if (sign == SoftSTree::PLUS)
         outs << "+ ";
      else if (sign == SoftSTree::BOTH)
         outs << "- ";
      else
         outs << "? ";
   }
   outs << endl;
}   
// ========================================================
