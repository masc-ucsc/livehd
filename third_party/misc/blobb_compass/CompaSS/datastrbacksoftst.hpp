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
#ifndef DATASTRBACKSOFTST_H
#define DATASTRBACKSOFTST_H

#include <iostream>
#include <vector>
#include <algorithm>
using namespace std;

// --------------------------------------------------------
class Point
{
public:
   Point(double nX, double nY)
      : in_x(nX), in_y(nY), in_area(nX*nY) {}
   Point(const Point& p2)
      : in_x(p2.in_x), in_y(p2.in_y), in_area(p2.in_area) {}

   inline double xCoord() const
      {  return in_x; }

   inline double yCoord() const
      {  return in_y; }

   inline double area() const
      {  return in_area; }

   inline void operator =(const Point& p2)
      {  in_x = p2.in_x; in_y = p2.in_y; in_area = p2.in_area; }

   // zero-tolorance to errors
   inline bool operator ==(const Point& p2) const
      {  return ((in_x == p2.in_x) &&
                 (in_y == p2.in_y) &&
                 (in_area == p2.in_area)); }
   
   inline bool operator <(const Point& p2) const
      {
         bool p1fit = (in_x < X_BOUND) && (in_y < Y_BOUND);
         bool p2fit = (p2.in_x < X_BOUND) && (p2.in_y < Y_BOUND);
         if (p1fit && p2fit)
            return in_area < p2.in_area;
         else if (p1fit)
            return true;
         else if (p2fit)
            return false;
         else
         {
            double p1viol = (in_x - X_BOUND) + (in_y - Y_BOUND);
            double p2viol = (p2.in_x - X_BOUND) + (p2.in_y - Y_BOUND);
            return p1viol < p2viol;
         }
      }

   static const double INFTY;
   static double X_BOUND;
   static double Y_BOUND;

private:
   double in_x;
   double in_y;
   double in_area;
};
// --------------------------------------------------------
class BoundaryType
{
public:
   BoundaryType(const vector<Point>& nCurve)
      : in_curve(nCurve),
        in_min_point(*min_element(nCurve.begin(), nCurve.end())) {}
   
   BoundaryType(int instr,
                const BoundaryType& c1,
                const BoundaryType& c2);
   
   BoundaryType(int instr,
                const BoundaryType& c1,
                const BoundaryType& c2,
                double width_limit,
                double height_limit);

   BoundaryType(const BoundaryType& b2)
      : in_curve(b2.in_curve), in_min_point(b2.in_min_point) {}

   enum InstrType {PLUS = -2, STAR = -3, OR= -4,
                   BOTH = -5, FLIP = -6, FLIP_OR = -7, NOOP = -8};

   inline const vector<Point>& curve() const
      {  return in_curve; }
   
   inline Point operator [](int index) const
      {  return in_curve[index]; }

   inline int size() const
      {  return in_curve.size(); }

   inline double min_area() const
      {  return in_min_point.area(); }

   inline Point min_point() const
      {  return in_min_point; }

   inline void operator =(const BoundaryType& b2)
      {  in_curve = b2.in_curve;  in_min_point = b2.in_min_point; }

   // 0-tolarance to errors
   inline bool operator ==(const BoundaryType& b2) const 
      {  return in_curve == b2.in_curve; }
   inline bool operator <(const BoundaryType& b2) const
      {  return in_min_point.area() < b2.in_min_point.area(); }
   
   // return -1 if the value is too small to be on the curve
   double getX(double yCoord) const;
   double getY(double xCoord) const;
   
   // assume c1, c2 have at least 3 points ie. there exists
   // feasible point with finite coordinates
   // OrCurves(): may overestimate the area required
   //             lines intersection assumes one of them is vertical
   static void PlusCurves(const vector<Point>& c1,
                          const vector<Point>& c2,
                          vector<Point>& outCurve);
   static void StarCurves(const vector<Point>& c1,
                          const vector<Point>& c2,
                          vector<Point>& outCurve);
   static void OrCurves(const vector<Point>& c1,
                        const vector<Point>& c2,
                        vector<Point>& outCurve);
   inline static void BothCurves(const vector<Point>& c1,
                                 const vector<Point>& c2,
                                 vector<Point>& outCurve)
      {
         vector<Point> plused;
         vector<Point> starred;

         PlusCurves(c1, c2, plused);
         StarCurves(c1, c2, starred);
         OrCurves(plused, starred, outCurve);
      }

   // ----temporary for hierarchical packer-----
   BoundaryType(int instr,
                const BoundaryType& b1);
   static void FlipCurve(const vector<Point>& inCurve,
                         vector<Point>& outCurve);
   inline static void FlipOrCurve(const vector<Point>& inCurve,
                                  vector<Point>& outCurve)
      {
         vector<Point> flipped;
         FlipCurve(inCurve, flipped);
         OrCurves(inCurve, flipped, outCurve);
      }
   
private:
   vector<Point> in_curve;
   Point in_min_point;

   // used by Plus/StarCurves
   static double interpolateY(const Point& left,
                              const Point& right, double newX);
   static double interpolateX(const Point& top,
                              const Point& bottom, double newY);
};
// --------------------------------------------------------
class SoftNode
{
public:
   SoftNode(int nBlk, const BoundaryType& nBoundary)
      : sign(nBlk), BLBlock(nBlk), TRblblock(nBlk),
        boundary(nBoundary),
        blkArea(nBoundary.min_area()),
        minArea(nBoundary.min_area()),
        minDeadspace(0) {}
   
   SoftNode(int instr, const SoftNode& BLNode, const SoftNode& TRNode)
      : sign(instr), BLBlock(BLNode.BLBlock), TRblblock(TRNode.BLBlock),
        boundary(instr, BLNode.boundary, TRNode.boundary),
        blkArea(BLNode.blkArea + TRNode.blkArea),
        minArea(max(boundary.min_area(), blkArea)),
        minDeadspace(minArea - blkArea) {}

   SoftNode(int instr, const SoftNode& BLNode, const SoftNode& TRNode,
            double width_limit, double height_limit)
      : sign(instr), BLBlock(BLNode.BLBlock), TRblblock(TRNode.BLBlock),
        boundary(instr, BLNode.boundary, TRNode.boundary, width_limit, height_limit),
        blkArea(BLNode.blkArea + TRNode.blkArea),
        minArea(max(boundary.min_area(), blkArea)),
        minDeadspace(minArea - blkArea) {}

   SoftNode(const SoftNode& sn2)
      : sign(sn2.sign), BLBlock(sn2.BLBlock), TRblblock(sn2.TRblblock),
        boundary(sn2.boundary),
        blkArea(sn2.blkArea),
        minArea(sn2.minArea),
        minDeadspace(sn2.minDeadspace)  {}

   inline void operator =(const SoftNode& sn2)
      {
         sign = sn2.sign;
         BLBlock = sn2.BLBlock;
         TRblblock = sn2.TRblblock;
         boundary = sn2.boundary;

         blkArea = sn2.blkArea;
         minArea = sn2.minArea;
         minDeadspace = sn2.minDeadspace;
      }
   
   int sign;
   int BLBlock;
   int TRblblock;
   BoundaryType boundary;
   
   double blkArea;
   double minArea;
   double minDeadspace;
};
// --------------------------------------------------------

#endif
