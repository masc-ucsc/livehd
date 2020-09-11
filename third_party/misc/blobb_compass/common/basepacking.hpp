/**************************************************************************
***    
*** Copyright (c) 2004-2005 Regents of the University of Michigan,
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
#ifndef BASEPACKING_H
#define BASEPACKING_H

#include <fstream>
#include <string>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cstdlib>
using namespace std;

// struct-like classes for representation conversion
// --------------------------------------------------------
class BasePacking
{
public:
   vector<double> xloc;
   vector<double> yloc;
   vector<double> width;
   vector<double> height;
};
// --------------------------------------------------------
class OrientedPacking : public BasePacking
{
public:
   enum ORIENT {N, E, S, W, FN, FE, FS, FW, UNDEFINED = -1};
   vector<ORIENT> orient;

   inline static ORIENT toOrient(char* orient);
   inline static char* toChar(ORIENT orient);
   inline static ORIENT flip(ORIENT orient);

};
inline ostream& operator << (ostream& outs, OrientedPacking::ORIENT);
inline void Save_bbb(ostream& outs, const OrientedPacking& pk);
inline void Read_bbb(istream& ins, const OrientedPacking& pk);
// --------------------------------------------------------
namespace basepacking_h
{   
   class Dimension
   {
   public:
      vector<double> width;
      vector<double> height;
      
      static const double INFTY; // = 1e100;
      static const double EPSILON_ACCURACY; // = 1e10;
      static const int UNDEFINED; // = -1;
      static const int ORIENT_NUM; // = 8;
   };
}
// --------------------------------------------------------
class HardBlockInfoType
{
public:
   HardBlockInfoType(ifstream& ins,         // formats:
                     const string& format); // "txt" or "blocks"
   const vector<basepacking_h::Dimension>& blocks;
   const vector<string>& block_names;
   inline const basepacking_h::Dimension& operator [](int index) const;
   inline int blocknum() const;
   inline double blockArea() const;

   static const int ORIENT_NUM; // = basepacking_h::Dimension::ORIENT_NUM;

   friend class MixedBlockInfoType;
   friend class MixedBlockInfoTypeFromDB;
   
protected:
   vector<basepacking_h::Dimension> in_blocks;   // store the left & bottom edges at the back
   vector<string> in_block_names; // parallel array with in_blocks

   void set_dimensions(int i, double w, double h);
   void ParseTxt(ifstream& ins);
   void ParseBlocks(ifstream& ins);

   HardBlockInfoType(int blocknum)
      : blocks(in_blocks),
        block_names(in_block_names),
        in_blocks(blocknum+2),
        in_block_names(blocknum+2) {}

   HardBlockInfoType(const HardBlockInfoType&);
};
// --------------------------------------------------------
inline void PrintDimensions(double width, double height);
inline void PrintAreas(double deadspace, double blockArea);
inline void PrintUtilization(double deadspace, double blockArea);
// --------------------------------------------------------

// =========================
//      IMPLEMENTATIONS
// =========================
inline ostream& operator << (ostream& outs,
                             OrientedPacking::ORIENT orient)
{
   switch (orient)
   {
   case OrientedPacking::N:
      outs << "N";
      break;
   case OrientedPacking::E:         
      outs << "E";
      break;
   case OrientedPacking::S:
      outs << "S";
      break;
   case OrientedPacking::W:
      outs << "W";
      break;
   case OrientedPacking::FN:
      outs << "FN";
      break;
   case OrientedPacking::FE:
      outs << "FE";
      break;
   case OrientedPacking::FS:
      outs << "FS";
      break;
   case OrientedPacking::FW:
      outs << "FW";
      break;
   case OrientedPacking::UNDEFINED:
      outs << "--";
      break;
   default:
      cout << "ERROR in outputting orientations." << endl;
      exit(1);
      break;
   }
   return outs;
}
// --------------------------------------------------------
inline void Save_bbb(ostream& outs,
                     const OrientedPacking& pk)
{
   double totalWidth = 0;
   double totalHeight = 0;
   int blocknum = pk.xloc.size();
   for (int i = 0; i < blocknum; i++)
   {
      totalWidth = max(totalWidth, pk.xloc[i]+pk.width[i]);
      totalHeight = max(totalHeight, pk.yloc[i]+pk.height[i]);
   }

   outs << totalWidth << endl;
   outs << totalHeight << endl;
   outs << blocknum << endl;
   for (int i = 0; i < blocknum; i++)
      outs << pk.width[i] << " " << pk.height[i] << endl;
   outs << endl;

   for (int i = 0; i < blocknum; i++)
      outs << pk.xloc[i] << " " << pk.yloc[i] << endl;
}
// --------------------------------------------------------
inline void Read_bbb(istream& ins,
                     OrientedPacking& pk)
{
   double width, height;
   ins >> width >> height;
   
   int blocknum = -1;
   ins >> blocknum;

   pk.xloc.resize(blocknum);
   pk.yloc.resize(blocknum);
   pk.width.resize(blocknum);
   pk.height.resize(blocknum);
   pk.orient.resize(blocknum);
   for (int i = 0; i < blocknum; i++)
   {
      ins >> pk.width[i] >> pk.height[i];
      pk.orient[i] = OrientedPacking::N;
   }

   for (int i = 0; i < blocknum; i++)
      ins >> pk.xloc[i] >> pk.yloc[i];
}   
// --------------------------------------------------------
inline OrientedPacking::ORIENT OrientedPacking::flip(OrientedPacking::ORIENT
                                                     orient)
{
   switch(orient)
   {
   case N: return FE;
   case E: return FN;
   case S: return FW;
   case W: return FS;
   case FN: return E;
   case FE: return N;
   case FS: return W;
   case FW: return S;
   case UNDEFINED: return UNDEFINED;
   default:
      cout << "ERROR: invalid orientation: " << orient << endl;
      exit(1);
      break;
   }
}
// --------------------------------------------------------
inline OrientedPacking::ORIENT OrientedPacking::toOrient(char* orient)
{
   if(!strcmp(orient, "N"))
      return N;
   if(!strcmp(orient, "E"))
      return E;
   if(!strcmp(orient, "S"))
      return S;
   if(!strcmp(orient, "W"))
      return W;
   if(!strcmp(orient, "FN"))
      return FN;
   if(!strcmp(orient, "FE"))
      return FE;
   if(!strcmp(orient, "FS"))
      return FS;
   if(!strcmp(orient, "FW"))
      return FW;
   if (!strcmp(orient, "--"))
      return UNDEFINED;
   
   cout << "ERROR: in converting char* to ORIENT" << endl;
   exit(1);
   return UNDEFINED;
}
// --------------------------------------------------------
inline char* OrientedPacking::toChar(ORIENT orient)
{
  if(orient == N)
     return("N"); 
  if(orient == E)
     return("E"); 
  if(orient == S)
     return("S"); 
  if(orient == W)
     return("W"); 
  if(orient == FN)
     return("FN"); 
  if(orient == FE)
     return("FE"); 
  if(orient == FS)
     return("FS"); 
  if(orient == FW)
     return("FW"); 
  if (orient == UNDEFINED)
     return("--");
  
  cout << "ERROR in converting ORIENT to char* " << endl;
  exit(1);
  return "--";
}
// ========================================================
inline const basepacking_h::Dimension& HardBlockInfoType::operator [](int index) const
{  return in_blocks[index]; }
// --------------------------------------------------------
inline int HardBlockInfoType::blocknum() const
{  return (in_blocks.size()-2); }
// --------------------------------------------------------
inline double HardBlockInfoType::blockArea() const
{
   double sum = 0;
   for (int i = 0; i < blocknum(); i++)
      sum += in_blocks[i].width[0] * in_blocks[i].height[0];
   return sum;
}
// ========================================================
void PrintDimensions(double width, double height)
{
   cout << "width:  " << width << endl;
   cout << "height: " << height << endl;
}
// --------------------------------------------------------
void PrintAreas(double deadspace, double blockArea)
{
   cout << "total area: " << setw(11)
        << deadspace + blockArea << endl;
   cout << "block area: " << setw(11) << blockArea << endl;
   cout << "dead space: " << setw(11) << deadspace
        << " (" << (deadspace / blockArea) * 100 << "%)" << endl;
}
// --------------------------------------------------------
void PrintUtilization(double deadspace, double blockArea)
{
   double totalArea = deadspace + blockArea;
   cout << "area usage   (wrt. total area): "
        << ((1 - (deadspace / totalArea)) * 100) << "%" << endl;
   cout << "dead space % (wrt. total area): "
        << ((deadspace / totalArea) * 100) << "%" << endl;
   cout << "---------------------------" << endl;
}
// ========================================================

#endif
