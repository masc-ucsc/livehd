/* 
   ESESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.

   Contributed by Jose Renau
                  Milos Prvulovic

This file is part of ESESC.

ESESC is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation;
either version 2, or (at your option) any later version.

ESESC is    distributed in the  hope that  it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public License along with
ESESC; see the file COPYING.  If not, write to the  Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "Snippets.h"
#include "nanassert.h"

#include <typeinfo>

short log2i(uint32_t n) {
  uint32_t m = 1;
  uint32_t i = 0;

  if (n==1)
    return 0;

  n= roundUpPower2(n);
  //assume integer power of 2
  I((n & (n - 1)) == 0);

  while(m<n) { 
    i++; 
    m <<=1; 
  }

  return i;
}

// this routine computes the smallest power of 2 greater than the
// parameter
uint32_t roundUpPower2(uint32_t x) {  
  // efficient branchless code extracted from "Hacker's Delight" by
  // Henry S. Warren, Jr.

  x = x - 1;
  x = x | (x >>  1);
  x = x | (x >>  2);
  x = x | (x >>  4);
  x = x | (x >>  8);
  x = x | (x >> 16);
  return x + 1;
}

