#/**************************************************************************
#***    
#*** Copyright (c) 2004 Regents of the University of Michigan,
#***               Hayward H. Chan and Igor L. Markov
#***
#***  Contact author(s): hhchan@umich.edu, imarkov@umich.edu
#***  Original Affiliation:   EECS Department, 
#***                          The University of Michigan,
#***                          Ann Arbor, MI 48109-2122
#***
#***  Permission is hereby granted, free of charge, to any person obtaining 
#***  a copy of this software and associated documentation files (the
#***  "Software"), to deal in the Software without restriction, including
#***  without limitation 
#***  the rights to use, copy, modify, merge, publish, distribute, sublicense, 
#***  and/or sell copies of the Software, and to permit persons to whom the 
#***  Software is furnished to do so, subject to the following conditions:
#***
#***  The above copyright notice and this permission notice shall be included
#***  in all copies or substantial portions of the Software.
#***
#*** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
#*** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
#*** OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
#*** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
#*** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
#*** OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
#*** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#***
#***
#***************************************************************************/

.SUFFIXES:        .o .cpp 

CC = g++
CCFLAGS = -Wall -O3 -ansi $(CCDIRS)
PURIFY = purify -log-file=log.txt
MOREOPT = -funroll-loops

CCDIRS = -I../BTree -I../common -I../CompaSS 
LDDIRS = -L../BTree -L../common -L../CompaSS 

AR = ar cr

blobb_OBJS = bound.o boundfixed.o boundst.o btreefromblobb.o datastr.o datastrhierst.o datastrst.o enginehierst.o enginest.o interface.o main.o nonslice.o parameters.o slice.o slicehier.o vectorize.o

THISLIB = BloBB
LIBNAME = lib$(THISLIB).a

LDFINAL = $(CC)
LIBBASE = ../lib
LIBS = -L$(LIBBASE) -L. -l$(THISLIB) -lBloBB -lBTree -lcommon -lCompaSS

# main.o nonslice.o datastr.o bound.o boundfixed.o slice.o datastrst.o boundst.o enginest.o datastrhierst.o vectorize.o enginehierst.o slicehier.o utilities.o interface.o parameters.o basepacking.o btree.o btreefromsstree.o btreefromblobb.o btreecompact.o btreecompactsstree.o datastrbacksoftst.o datastrfrontsoftst.o iosoftst.o

# -----linking all together-----
lib : $(LIBNAME)

$(LIBNAME) : $(blobb_OBJS)
	/bin/rm -f $(LIBNAME)
	$(AR) $(LIBNAME) $(blobb_OBJS) 
	/bin/rm -f $(LIBBASE)/$(LIBNAME)
	ln -s ../BloBB/$(LIBNAME) $(LIBBASE)/$(LIBNAME)


blobb : $(blobb_OBJS)
	/bin/rm -f blobb
	$(LDFINAL) -o blobb -static $(LIBS)
	/bin/rm -f ../bin/blobb
	ln -s ../BloBB/blobb ../bin/blobb

.cpp.o : 
	$(CC) $(CCFLAGS) -c $*.cpp

clean :
	rm -f *.o *~ blobb *.a
