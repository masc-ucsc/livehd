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
#include "mixedpacking.hpp"
#include "basepacking.hpp"
#include "parsers.hpp"

#include <fstream>
#include <cmath>
using namespace std;
using namespace parse_utils;
using namespace basepacking_h;

const int MixedBlockInfoType::ORIENT_NUM = HardBlockInfoType::ORIENT_NUM;
// --------------------------------------------------------
MixedBlockInfoType::MixedBlockInfoType(const string& blocksfilename,
                                       const string& format)
   : currDimensions(_currDimensions),
     blockARinfo(_blockARinfo),
     _currDimensions(0)
{
   ifstream infile;
   infile.open(blocksfilename.c_str());
   if (!infile.good())
   {
      cout << "ERROR: cannot open file " << blocksfilename << endl;
      exit(1);
   }
  
   if (format == "txt")
   {
      cout << "Sorry, .txt format isn't supported now." << endl;
      exit(0);
   }
   else if (format == "blocks")
      ParseBlocks(infile);
}
// --------------------------------------------------------
void MixedBlockInfoType::ParseBlocks(ifstream& input)
{
   char block_name[1024];
   char block_type[1024];
   char tempWord1[1024];
    
   vector<Point> vertices;
   int numVertices;
   bool success; 
   double width, height;
   
   double area,minAr,maxAr;
   int numSoftBl=0;
   int numHardBl=0;
   int numBl = numSoftBl + numHardBl;
   int numTerm=0;
   
   int indexBlock=0;
   int indexTerm=0;
   
   if(!input)
   {
      cout<<"ERROR: .blocks file could not be opened successfully"<<endl;
      exit(0);
   }

   while(!input.eof())
   {
      input>>tempWord1;
      if(!(strcmp(tempWord1,"NumSoftRectangularBlocks")))
         break;
   }
   input>>tempWord1;
   input>>numSoftBl;

   while(!input.eof())
   {
      input>>tempWord1;
      if(!(strcmp(tempWord1,"NumHardRectilinearBlocks")))
         break;
   }
   input>>tempWord1;
   input>>numHardBl;
    
   while(!input.eof())
   {
      input>>tempWord1;
      if(!(strcmp(tempWord1,"NumTerminals")))
         break;
   }
   input>>tempWord1;
   input>>numTerm;

   numBl = numHardBl + numSoftBl;
   _currDimensions.in_blocks.resize(numBl+2);
   _currDimensions.in_block_names.resize(numBl+2);
   _blockARinfo.resize(numHardBl+numSoftBl+2);
   while(!input.eof())
   {
      block_type[0] = '\0';
      eatblank(input);
      if(input.eof())
         break;
      if(input.peek()=='#')
         eathash(input);
      else
      {
         eatblank(input);
         if(input.peek() == '\n' || input.peek() == '\r')
         {
            input.get();
            continue;
         }
         
         input >> block_name;
         input >> block_type ;
         
         if(!strcmp(block_type,"softrectangular")) 
         {
	    input >> area;
	    input >> minAr;
	    input >> maxAr;

            width = sqrt(area);
            height = sqrt(area);

//             printf("[%d]: area: %.2lf minAR: %.2lf maxAR: %.2lf width: %.2lf height: %.2lf\n",
//                    indexBlock, area, minAr, maxAr, width, height);
            _currDimensions.set_dimensions(indexBlock, width, height);
            if (indexBlock >= int(_currDimensions.block_names.size()))
            {
               cout << "ERROR: too many hard block specified." << endl;
               exit(1);
            }                 
            _currDimensions.in_block_names[indexBlock] = block_name;

            _blockARinfo[indexBlock].area = area;
            set_blockARinfo_AR(indexBlock, min(minAr, maxAr), max(minAr, maxAr));
            _blockARinfo[indexBlock].isSoft = true;
            
	    ++indexBlock;
	    //cout<<block_name<<" "<<area<<endl;
         }
         else if(!strcmp(block_type,"hardrectilinear"))
         {
	    input >> numVertices;
	    Point tempPoint;
	    success = 1;
	    if(numVertices > 4)
               cout<<"ERROR in parsing .blocks file. rectilinear blocks can be only rectangles for now\n";
	    for(int i=0; i<numVertices; ++i)
            {
	       success &= needCaseChar(input, '(');  input.get();
	       input >> tempPoint.x;
	       success &= needCaseChar(input, ',');  input.get();
	       input >> tempPoint.y;
	       success &= needCaseChar(input, ')');  input.get();
	       vertices.push_back(tempPoint);
            }
	    if(!success)
               cout<<"ERROR in parsing .blocks file while processing hardrectilinear blocks"<<endl;
	    
	    width = vertices[2].x - vertices[0].x;
	    height = vertices[2].y - vertices[0].y;
	    area = width*height;
	    minAr = width/height;
	    maxAr = minAr;

            _currDimensions.set_dimensions(indexBlock, width, height);
            if (indexBlock >= int(_currDimensions.block_names.size()))
            {
               cout << "ERROR: too many hard block specified." << endl;
               exit(1);
            }                 
            _currDimensions.in_block_names[indexBlock] = block_name;
            
            _blockARinfo[indexBlock].area = area;
            set_blockARinfo_AR(indexBlock, min(minAr, maxAr), max(minAr, maxAr));
            _blockARinfo[indexBlock].isSoft = false;

	    ++indexBlock;
	    vertices.clear();
	    //cout<<block_name<<" "<<area<<endl;
         }
         else if(!strcmp(block_type,"terminal"))
         {
	    ++indexTerm;
	    //cout<<indexTerm<<"  "<<block_name<<endl;
         }
         /*
           else
	   cout<<"ERROR in parsing .blocks file"<<endl;
         */
      }  
   }
   input.close();
   
   if(numSoftBl+numHardBl != indexBlock)
      cout << "ERROR in parsing .blocks file. No: of blocks do not tally "
           << (indexBlock+indexTerm) << " vs " << (numSoftBl+numHardBl+numTerm)
           << endl;

   _currDimensions.set_dimensions(numBl, 0, Dimension::INFTY);
   _currDimensions.in_block_names[numBl] = "LEFT";
   _blockARinfo[numBl].area = 0;
   _blockARinfo[numBl].minAR.resize(ORIENT_NUM, 0);
   _blockARinfo[numBl].maxAR.resize(ORIENT_NUM, 0);
   _blockARinfo[numBl].isSoft = false;

   _currDimensions.set_dimensions(numBl+1, Dimension::INFTY, 0);
   _currDimensions.in_block_names[numBl+1] = "BOTTOM";
   _blockARinfo[numBl+1].area = 0;
   _blockARinfo[numBl+1].minAR.resize(ORIENT_NUM, Dimension::INFTY);
   _blockARinfo[numBl+1].maxAR.resize(ORIENT_NUM, Dimension::INFTY);
   _blockARinfo[numBl+1].isSoft = false;
}
// --------------------------------------------------------
      
