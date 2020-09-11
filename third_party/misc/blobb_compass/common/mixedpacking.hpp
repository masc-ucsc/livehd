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
#ifndef MIXEDPACKING_H
#define MIXEDPACKING_H

#include "basepacking.hpp"

#include <vector>
#include <string>
using namespace std;

// --------------------------------------------------------
class MixedBlockInfoType
{
public:
   MixedBlockInfoType(const string& blocksfilename,
                      const string& format); // "blocks" or "txt"
   virtual ~MixedBlockInfoType() {}
   
   class BlockARInfo
   {
   public:
      double area;
      vector<double> maxAR; // maxAR/minAR for the "North" orientation
      vector<double> minAR;
      bool isSoft;
   };
   const HardBlockInfoType& currDimensions;
   const vector<BlockARInfo>& blockARinfo;
   static const int ORIENT_NUM; // = HardBlockInfoType::ORIENT_NUM;

   inline void setBlockDimensions(int index, double newWidth, double newHeight,
                                  int theta);

protected:
   HardBlockInfoType _currDimensions;
   vector<BlockARInfo> _blockARinfo;

   void ParseBlocks(ifstream& input);
   inline void set_blockARinfo_AR(int index, double minAR, double maxAR);

   // used by descendent class "MixedBlockInfoTypeFromDB"
   MixedBlockInfoType(int blocknum)
      : currDimensions(_currDimensions),
        blockARinfo(_blockARinfo),
        _currDimensions(blocknum),
        _blockARinfo(blocknum+2)
      {}
};
// --------------------------------------------------------

// ===============
// IMPLEMENTATIONS
// ===============
void MixedBlockInfoType::setBlockDimensions(int index,
                                            double newWidth,
                                            double newHeight,
                                            int theta)
{
   double realWidth = (theta % 2 == 0)? newWidth : newHeight;
   double realHeight = (theta % 2 == 0)? newHeight : newWidth;
   _currDimensions.set_dimensions(index, realWidth, realHeight);
}
// --------------------------------------------------------
void MixedBlockInfoType::set_blockARinfo_AR(int index,
                                            double minAR,
                                            double maxAR)
{
   _blockARinfo[index].minAR.resize(ORIENT_NUM);
   _blockARinfo[index].maxAR.resize(ORIENT_NUM);
   for (int i = 0; i < ORIENT_NUM; i++)
   {
      _blockARinfo[index].minAR[i] = ((i%2 == 0)?
                                      minAR : (1.0 / maxAR));
      _blockARinfo[index].maxAR[i] = ((i%2 == 0)?
                                      maxAR : (1.0 / minAR));
   }
}         
// -------------------------------------------------------- 
#endif
