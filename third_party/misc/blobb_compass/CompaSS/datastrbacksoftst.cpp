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

#include <float.h>
#include <vector>
#include <algorithm>
#include <cmath>
using namespace std;

// --------------------------------------------------------
const double Point::INFTY = pow(DBL_MAX, 0.3);
double Point::X_BOUND = Point::INFTY;
double Point::Y_BOUND = Point::INFTY;
// --------------------------------------------------------
BoundaryType::BoundaryType(int instr,
                           const BoundaryType& b1,
                           const BoundaryType& b2)
   : in_min_point(-1, -1)
{
   switch (instr)
   {
   case PLUS:
      PlusCurves(b1.in_curve, b2.in_curve, in_curve);
      break;
      
   case STAR:
      StarCurves(b1.in_curve, b2.in_curve, in_curve);
      break;

   case OR:
      OrCurves(b1.in_curve, b2.in_curve, in_curve);
      break;

   case BOTH:
      BothCurves(b1.in_curve, b2.in_curve, in_curve);
      break;
   }
   in_min_point = *min_element(in_curve.begin(), in_curve.end());
}
// --------------------------------------------------------
BoundaryType::BoundaryType(int instr,
                           const BoundaryType& b1,
                           const BoundaryType& b2,
                           double width_limit,
                           double height_limit)
   : in_min_point(width_limit*2, height_limit*2)
{
   switch (instr)
   {
   case PLUS:
      PlusCurves(b1.in_curve, b2.in_curve, in_curve);
      break;
      
   case STAR:
      StarCurves(b1.in_curve, b2.in_curve, in_curve);
      break;

   case OR:
      OrCurves(b1.in_curve, b2.in_curve, in_curve);
      break;

   case BOTH:
      BothCurves(b1.in_curve, b2.in_curve, in_curve);
      break;
   }

   int curveSize = in_curve.size();
   int start_ptr = 0;
   while ((start_ptr < curveSize) &&
          (in_curve[start_ptr].yCoord() > height_limit))
      start_ptr++;

   int end_ptr = curveSize-1;
   while ((end_ptr >= 0) &&
          (in_curve[end_ptr].xCoord() > width_limit))
      end_ptr--;

   if (start_ptr <= end_ptr)
      in_min_point = *min_element(&(in_curve[start_ptr]),
                                  &(in_curve[end_ptr+1]));
//    else
//       in_min_point = Point(in_curve[1].yCoord(),
//                            in_curve[curveSize-2].xCoord());
//   in_min_point = *min_element(in_curve.begin(), in_curve.end());
}  
// --------------------------------------------------------
double BoundaryType::getX(double yCoord) const 
{
   int left = 0;
   int right = in_curve.size()-1;
   int mid = right / 2;

   if (yCoord < in_curve[right].yCoord())
      return -1; // no solution found if yCoord too small
   
   while (left < right)
   {
      if (mid > left &&
          in_curve[mid-1].yCoord() >= yCoord &&
          in_curve[mid].yCoord() <= yCoord)
      {
         // special case?
         if (yCoord == in_curve[mid-1].yCoord())
         {
            double minX = in_curve[mid-1].xCoord();
            int curr = mid-2;
            while (curr >= left && yCoord == in_curve[curr].yCoord())
            {
               minX = in_curve[curr].xCoord();
               curr--;
            }
            return minX;
         }
         else
            return interpolateX(in_curve[mid-1], in_curve[mid], yCoord);
      }
      else if (mid < right &&
               in_curve[mid].yCoord() >= yCoord &&
               in_curve[mid+1].yCoord() <= yCoord)
         return interpolateX(in_curve[mid], in_curve[mid+1], yCoord);
      else if (in_curve[mid].yCoord() > yCoord)
      {
         left = mid;
         mid = (right+left) / 2;
      }
      else if (in_curve[mid].yCoord() < yCoord)
      {
         right = mid;
         mid = (right+left) / 2;
      }
   }
   return -2;  // a bug if reaches here
}
// --------------------------------------------------------
double BoundaryType::getY(double xCoord) const
{
   int left = 0;
   int right = in_curve.size() - 1;
   int mid = right / 2;

   if (xCoord < in_curve[left].xCoord())
      return -1;  // no solution found if xCoord too small

   while (left < right)
   {
      if (mid < right &&
          in_curve[mid].xCoord() <= xCoord &&
          in_curve[mid+1].xCoord() >= xCoord)
      {
         // special case?
         if (xCoord == in_curve[mid+1].xCoord())
         {
            double minY = in_curve[mid+1].yCoord();
            int curr = mid+2;
            
            while (curr <= right && xCoord == in_curve[curr].xCoord())
            {
               minY = in_curve[curr].yCoord();
               curr++;
            }
            return minY;
         }
         else
            return interpolateY(in_curve[mid], in_curve[mid+1], xCoord);
      }
      else if (mid > left &&
          in_curve[mid-1].xCoord() <= xCoord &&
          in_curve[mid].xCoord() >= xCoord)
         return interpolateY(in_curve[mid-1], in_curve[mid], xCoord);
      else if (in_curve[mid].xCoord() < xCoord)
      {
         left = mid;
         mid = (right+left) / 2;
      }
      else if (in_curve[mid].xCoord() > xCoord)
      {
         right = mid;
         mid = (right+left) / 2;
      }
   }
   return -2; // a bug if reaches here
}
// --------------------------------------------------------     
void BoundaryType::PlusCurves(const vector<Point>& curveA,
                              const vector<Point>& curveB,
                              vector<Point>& outCurve)
{
   int cA_size = curveA.size();
   int cB_size = curveB.size();

   int cA_prev = 1;
   int cA_curr = 2;
   int cB_prev = 1;
   int cB_curr = 2;

   outCurve.reserve(cA_size + cB_size);
   if (curveA[cA_prev].xCoord() < curveB[cB_prev].xCoord())
   {
      while ((cA_curr < cA_size) &&
             (curveA[cA_curr].xCoord() < curveB[cB_prev].xCoord()))
      {
         cA_prev = cA_curr;
         cA_curr++;
      }

      double first_xCoord = curveB[cB_prev].xCoord();
      double first_yCoord = curveB[cB_prev].yCoord() +
         interpolateY(curveA[cA_prev], curveA[cA_curr], first_xCoord);

      outCurve.push_back(Point(first_xCoord, Point::INFTY));
      outCurve.push_back(Point(first_xCoord, first_yCoord));
   }
   else
   {
      while ((cB_curr < cB_size) &&
             (curveB[cB_curr].xCoord() < curveA[cA_prev].xCoord()))
      {
         cB_prev = cB_curr;
         cB_curr++;
      }

      double first_xCoord = curveA[cA_prev].xCoord();
      double first_yCoord = curveA[cA_prev].yCoord() +
         interpolateY(curveB[cB_prev], curveB[cB_curr], first_xCoord);

      outCurve.push_back(Point(first_xCoord, Point::INFTY));
      outCurve.push_back(Point(first_xCoord, first_yCoord));
   }

   double back_xCoord = outCurve.back().xCoord();
   double back_yCoord = outCurve.back().yCoord();
   while ((cA_curr < cA_size) && (cB_curr < cB_size))
   {
      while ((cA_curr < cA_size) &&
             (curveA[cA_curr].xCoord() <= curveB[cB_curr].xCoord()))
      {
         double xCoord = curveA[cA_curr].xCoord();
         double yCoord = curveA[cA_curr].yCoord() +
            interpolateY(curveB[cB_prev], curveB[cB_curr], xCoord);

         if (xCoord != back_xCoord || yCoord != back_yCoord)
         {
            outCurve.push_back(Point(xCoord, yCoord));
            back_xCoord = xCoord;
            back_yCoord = yCoord;
         }

         cA_prev = cA_curr;
         cA_curr++;
      }

      if (cA_curr < cA_size)
         while ((cB_curr < cB_size) &&
                (curveB[cB_curr].xCoord() <= curveA[cA_curr].xCoord()))
         {
            double xCoord = curveB[cB_curr].xCoord();
            double yCoord = curveB[cB_curr].yCoord() +
               interpolateY(curveA[cA_prev], curveA[cA_curr], xCoord);

            if (xCoord != back_xCoord || yCoord != back_yCoord)
            {
               outCurve.push_back(Point(xCoord, yCoord));
               back_xCoord = xCoord;
               back_yCoord = yCoord;
            }
            
            cB_prev = cB_curr;
            cB_curr++;
         }
   }
}
// --------------------------------------------------------
void BoundaryType::StarCurves(const vector<Point>& curveA,
                              const vector<Point>& curveB,
                              vector<Point>& outCurve)
{
   int cA_last = curveA.size()-1;
   int cB_last = curveB.size()-1;

   int cA_prev = cA_last-1;
   int cA_curr = cA_last-2;
   int cB_prev = cB_last-1;
   int cB_curr = cB_last-2;

   outCurve.reserve(cA_last + cB_last);
   if (curveA[cA_prev].yCoord() < curveB[cB_prev].yCoord())
   {
      while ((cA_curr >= 0) &&
             (curveA[cA_curr].yCoord() < curveB[cB_prev].yCoord()))
      {
         cA_prev = cA_curr;
         cA_curr--;
      }
      double first_yCoord = curveB[cB_prev].yCoord();
      double first_xCoord = curveB[cB_prev].xCoord() +
         interpolateX(curveA[cA_curr], curveA[cA_prev], first_yCoord);
      
      outCurve.push_back(Point(Point::INFTY, first_yCoord));
      outCurve.push_back(Point(first_xCoord, first_yCoord));
   }
   else // curveB[cB_prev].yCoord() <= curveA[cA_prev].yCoord()
   {
      while ((cB_curr >= 0) &&
             (curveB[cB_curr].yCoord() < curveA[cA_prev].yCoord()))
      {
         cB_prev = cB_curr;
         cB_curr--;
      }
      double first_yCoord = curveA[cA_prev].yCoord();
      double first_xCoord = curveA[cA_prev].xCoord() +
         interpolateX(curveB[cB_curr], curveB[cB_prev], first_yCoord);

      outCurve.push_back(Point(Point::INFTY, first_yCoord));
      outCurve.push_back(Point(first_xCoord, first_yCoord));
   }

   double back_xCoord = outCurve.back().xCoord();
   double back_yCoord = outCurve.back().yCoord();
   while (cA_curr >= 0 && cB_curr >= 0)
   {
      while ((cA_curr >= 0) &&
             (curveA[cA_curr].yCoord() <= curveB[cB_curr].yCoord()))
      {
         double yCoord = curveA[cA_curr].yCoord();
         double xCoord = curveA[cA_curr].xCoord() +
            interpolateX(curveB[cB_curr], curveB[cB_prev], yCoord);

         if (xCoord != back_xCoord || yCoord != back_yCoord)
         {
            outCurve.push_back(Point(xCoord, yCoord));
            back_xCoord = xCoord;
            back_yCoord = yCoord;
         }
         
         cA_prev = cA_curr;
         cA_curr--;
      }

      if (cA_curr >= 0)
         while ((cB_curr >= 0) &&
                (curveB[cB_curr].yCoord() <= curveA[cA_curr].yCoord()))
         {
            double yCoord = curveB[cB_curr].yCoord();
            double xCoord = curveB[cB_curr].xCoord() +
               interpolateX(curveA[cA_curr], curveA[cA_prev], yCoord);

            if (xCoord != back_xCoord || yCoord != back_yCoord)
            {
               outCurve.push_back(Point(xCoord, yCoord));
               back_xCoord = xCoord;
               back_yCoord = yCoord;
            }
            
            cB_prev = cB_curr;
            cB_curr--;
         }
   }
   reverse(outCurve.begin(), outCurve.end());
}
// --------------------------------------------------------
void BoundaryType::OrCurves(const vector<Point>& c1,
                            const vector<Point>& c2,
                            vector<Point>& outCurve)
{
   const vector<Point> *cA_ptr = NULL;
   const vector<Point> *cB_ptr = NULL;
   if (c1[1].xCoord() < c2[1].xCoord())
   {
      cA_ptr = &c1;
      cB_ptr = &c2;
   }
   else if (c1[1].xCoord() > c2[1].xCoord())
   {
      cA_ptr = &c2;
      cB_ptr = &c1;
   }
   else
   {
      double y1 = c1[1].yCoord();
      double y2 = c2[1].yCoord();
      cA_ptr = (y1 >= y2)? &c1 : &c2;
      cB_ptr = (y1 >= y2)? &c2 : &c1;
   }

   const vector<Point>& curveA = *cA_ptr;
   const vector<Point>& curveB = *cB_ptr;
   
   int cA_last = curveA.size() - 1;
   int cB_last = curveB.size() - 1;

   int cA_prev = 0;
   int cA_curr = 1;
   int cB_prev = 0;
   int cB_curr = 1;

   outCurve.reserve(cA_last + cB_last + 2);

   // add the first point
   double first_xCoord = curveA[1].xCoord();
   outCurve.push_back(Point(first_xCoord, Point::INFTY));

   // add the points until curveB needs to be considered
   // keeping cA_prev <= cB_prev (xCoord)
   while ((cA_curr <= cA_last) &&
          (curveA[cA_curr].xCoord() <= curveB[cB_curr].xCoord()))
   {
      double xCoord = curveA[cA_curr].xCoord();
      double yCoord = curveA[cA_curr].yCoord();
      outCurve.push_back(Point(xCoord, yCoord));

      cA_prev++;
      cA_curr++;
   }

   // consider all points in both curves
   // lastUpdated == 0 <-- last point lies on A
   //                1 <-- last point lies on B
   int lastUpdated = 0;
   while (cA_curr < cA_last || cB_curr < cB_last)
   {
      double A_xCoord = curveA[cA_curr].xCoord();
      double B_xCoord = curveB[cB_curr].xCoord();

      if (A_xCoord <= B_xCoord)
      {
         double A_yCoord = curveA[cA_curr].yCoord();
         double A_first_yCoord =
            interpolateY(curveB[cB_prev], curveB[cB_curr], A_xCoord);

         if (A_yCoord < A_first_yCoord)
         {
            if (lastUpdated == 1)            
               outCurve.push_back(Point(A_xCoord, A_first_yCoord));
            outCurve.push_back(Point(A_xCoord, A_yCoord));
            lastUpdated = 0;
         }
         else if (A_first_yCoord < A_yCoord)
         {
            if (lastUpdated == 0)
            {
               outCurve.push_back(Point(A_xCoord, A_yCoord));
               outCurve.push_back(Point(A_xCoord, A_first_yCoord));
            }
            lastUpdated = 1;
         }
         else 
            outCurve.push_back(Point(A_xCoord, A_yCoord));

         cA_prev++;
         cA_curr++;
      }
      else
      {
         double B_yCoord = curveB[cB_curr].yCoord();
         double B_first_yCoord =
            interpolateY(curveA[cA_prev], curveA[cA_curr], B_xCoord);

         if (B_yCoord < B_first_yCoord)
         {
            if (lastUpdated == 0)
               outCurve.push_back(Point(B_xCoord, B_first_yCoord));
            outCurve.push_back(Point(B_xCoord, B_yCoord));
            lastUpdated = 1;
         }
         else if (B_first_yCoord < B_yCoord)
         {
            if (lastUpdated == 1)
            {
               outCurve.push_back(Point(B_xCoord, B_yCoord));
               outCurve.push_back(Point(B_xCoord, B_first_yCoord));
            }
            lastUpdated = 0;
         }
         else
            outCurve.push_back(Point(B_xCoord, B_yCoord));
         
         cB_prev++;
         cB_curr++;
      }
   }
   double last_yCoord = min(curveA[cA_last].yCoord(),
                            curveB[cB_last].yCoord());
   outCurve.push_back(Point(Point::INFTY, last_yCoord));
}                  
// --------------------------------------------------------
double BoundaryType::interpolateY(const Point& left,
                                  const Point& right,
                                  double newX)
{
   double leftX = left.xCoord();
   double rightX = right.xCoord();

   if (leftX == rightX)
      return right.yCoord();
   else
   {
      double leftY = left.yCoord();
      double rightY = right.yCoord();
      double ratio = (newX - leftX) / (rightX - leftX);

      return leftY * (1-ratio) + rightY * ratio;
   }
}
// --------------------------------------------------------
double BoundaryType::interpolateX(const Point& top,
                                  const Point& bottom,
                                  double newY)
{
   double topY = top.yCoord();
   double bottomY = bottom.yCoord();

   if (topY == bottomY)
      return top.xCoord();
   else
   {
      double topX = top.xCoord();
      double bottomX = bottom.xCoord();
      double ratio = (newY - bottomY) / (topY - bottomY);

      return topX * ratio + bottomX * (1-ratio);
   }
}
// ========================================================
// ----temporary-----
BoundaryType::BoundaryType(int instr,
                           const BoundaryType& b)
   : in_min_point(Point::INFTY, Point::INFTY)
{
   switch (instr)
   {
   case FLIP:
      FlipCurve(b.in_curve, in_curve);
      break;

   case FLIP_OR:
      FlipOrCurve(b.in_curve, in_curve);
      break;

   case NOOP:
      in_curve = b.in_curve;
      break;
   }
   in_min_point = *min_element(in_curve.begin(), in_curve.end());
}
// --------------------------------------------------------
void BoundaryType::FlipCurve(const vector<Point>& inCurve,
                             vector<Point>& outCurve)
{
   int curveSize = inCurve.size();
   outCurve.reserve(curveSize);
   for (int i = 0; i < curveSize; i++)
   {
      double xCoord = inCurve[i].xCoord();
      double yCoord = inCurve[i].yCoord();

      outCurve.push_back(Point(yCoord, xCoord));
   }
   reverse(outCurve.begin(), outCurve.end());
}
// ========================================================
      
   
