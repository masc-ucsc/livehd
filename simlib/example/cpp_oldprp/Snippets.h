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

#ifndef SNIPPETS_H
#define SNIPPETS_H

#include <stdint.h>

//**************************************************
// Types used for time (move to callback?)
typedef uint64_t Time_t;
const uint64_t MaxTime = ((~0ULL) - 1024);  // -1024 is to give a little bit of margin

typedef uint16_t TimeDelta_t;

//x, y are integers and x,y > 0
#define CEILDiv(x,y)            ((x)-1)/(y)+1

uint32_t roundUpPower2(uint32_t x);
short  log2i(uint32_t n);

#define ISPOWER2(x)  (((x) & (x-1))==0)

#define K(n) ((n) * 1024)
#define M(n) (K(n) * 1024)
#define G(n) (M(n) * 1024)

#define AtomicSwap(ptr,val) __sync_lock_test_and_set(ptr, val)
#define AtomicCompareSwap(ptr,tst_ptr,new_ptr) __sync_val_compare_and_swap(ptr, tst_ptr, new_ptr)
#define AtomicAdd(ptr,val) __sync_fetch_and_add(ptr, val)
#define AtomicSub(ptr,val) __sync_fetch_and_sub(ptr, val)

#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)

#endif // SNIPPETS_H
