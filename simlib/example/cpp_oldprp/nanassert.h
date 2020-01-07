/* 
   ESESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.

   Assertion management. Based on:
   xassert from Harald M. Mueller
   nana from GNU

   Contributed by Jose Renau
                  Basilio Fraguela

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

/* Configuration defines:
 *
 * MSG is always active.
 *
 * DEBUG activates all the assertions that you addded in the
 * code. This means IN,I,GIN,GI,GIS,ID,IS,LOG,GLOG
 *
 * NDEBUG deactivates PRE and PREN assertions. Typically, they check
 * the parameters correctness (preconditions).
 *
 * SAFE activates the TRACE command. Remember that to activate a
 * specific TRACE, you must define TRACE=1 and the tracing condition
 * in the environment.
 *
 * ASSERTACTION can be redefined to execute something different than
 * exit(125)
 *
 * ASSERTSTREAM is stderr by default. It also may be changed.
 * 
 */

/*
 * Example:
 *  int32_t a[100];
 *      ID(int32_t i;)  // See that the ; is inside. Required only in C, not C++ 
 *      ID(int32_t j;)
 *      int32_t k;
 *      
 *      for(k=0;k<100;k++)
 *              a[k]=k;
 *
 *      I(k == 100 );
 *
 *      MSG("Hello");
 *
 *      IN(exists((j=0;j<100;j++),a[j]==10));
 *      
 *      IN(existsn((i=1;i<100;i++),
 *                        forall((j=0;j<i;j++),0)
 *                        ));
 *
 *      TRACE("NABI","THis is one");
 * */

#ifndef _NANASSERT_H_
#define _NANASSERT_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* By default cerr, but you can redefine it. */
#ifndef ASSERTSTREAM
#define ASSERTSTREAM    stderr
#endif

/* You can redefine ASSERTACTION to to something different than exit */
#ifndef ASSERTACTION
void nanassertexit();

#define ASSERTACTION    nanassertexit()
#endif

/* Not nested forall and exists */

#define forall(asstFor,asstCond) \
    do {                         \
          aRes =1;                   \
          for asstFor {              \
                  if( !(asstCond) ){     \
                          aRes = 0;          \
                          break;             \
                  }                      \
          }                          \
        }while(0)

#define exists(asstFor,asstCond) \
    do {                         \
          for asstFor {              \
                  if( (asstCond) ){      \
                          aRes = 1;    \
                          break;             \
                  }                      \
          }                          \
        }while(0)

/* Nested forall and exists */

#define foralln(asstFor,asstCond)\
    do {                         \
      int32_t result=1;              \
          for asstFor {              \
          asstCond;              \
                  if( !aRes ){     \
                  result = 0;        \
                          break;             \
                  }                      \
          }                          \
      aRes = result;       \
        }while(0)

#define existsn(asstFor,asstCond)\
    do {                         \
      int32_t result=0;              \
          for asstFor {              \
          asstCond;              \
                  if( aRes ){      \
                  result = 1;        \
                          break;             \
                  }                      \
          }                          \
      aRes = result;       \
        }while(0)

extern const char *NanassertID;

#ifdef NANASSERTFILE
#define doassert()               \
    do {                                   \
      fprintf(ASSERTSTREAM,"%s (%s),%s line %d failed for ",__FILE__,NANASSERTFILE,NanassertID,__LINE__); \
      fprintf(ASSERTSTREAM,"\n");          \
      ASSERTACTION;                        \
    }while(0)
#else
#define doassert()               \
    do {                                   \
      fprintf(ASSERTSTREAM,"%s,%s line %d failed for ",__FILE__,NanassertID,__LINE__);\
      fprintf(ASSERTSTREAM,"\n");          \
      ASSERTACTION;                        \
    }while(0)
#endif


#if (defined __GNUC__)
#define MSG(a...)   do{ fprintf(ASSERTSTREAM,##a); fprintf(ASSERTSTREAM,"\n"); }while(0)
#define GMSG(g,a...) do{ if(g) do{ fprintf(ASSERTSTREAM,##a); fprintf(ASSERTSTREAM,"\n"); }while(0); }while(0)
#else

/* Without GGC is not so nice, but works */

/* The following prototipes are defined in nanassert.c */
void NoGCCMSG(const char *format, ...);
void NoGCCGMSG(int32_t g, const char *format, ...);

void VoidNoGCCMSG(const char *format, ...);
void VoidNoGCCGMSG(int32_t g, const char *format, ...);

#define MSG   NoGCCMSG
#define GMSG  NoGCCGMSG

#endif

/********************************************************
 * NDEBUG Only section 
 */

#ifdef NDEBUG
#define PREN(aC)                /* NDEBUG */
#define PRE(aC)                 /* NDEBUG */
#else    /* !NDEBUG */
#define PREN(aC)   do{ int32_t aRes=0; aC; if(!aRes) doassert(); }while(0)
#define PRE(aC)    do{                 if(!(aC)) doassert(); }while(0)
#endif   /* NDEBUG */

/********************************************************
 * DEBUG Only section 
 */

#ifdef DEBUG
#define IN(aC)   do{ int32_t aRes=0; aC; if(!aRes) doassert(); }while(0)
#define I(aC)    do{                 if(!(aC)) doassert(); }while(0)
#define GIN(g,e) do{ if(g) IN(e); }while(0)
#define GI(g,e)  do{ if(g)  I(e); }while(0)
#define GIS(g,e) do{ if(g)   e;   }while(0)
#define ID(e)        e
#define ID2(e)        e
#define IS(e)    do{ e;           }while(0)

#ifdef DEBUG_SILENT
#ifdef __GNUC__
#define LOG(a...)               /* nodebug */
#define GLOG(g,a...)            /* nodebug */
#else    /* !__GNUC__ */
#define LOG         VoidNoGCCMSG
#define GLOG        VoidNoGCCGMSG
#endif   /* __GNUC__ */
#else
#define LOG    MSG
#define GLOG   GMSG
#endif

#else    /* !DEBUG */
#define IN(asstCond)            /* nodebug */
#define I(asstCond)             /* nodebug */
#define GIN(g,e)                /* nodebug */
#define GI(g,e)                 /* nodebug */
#define GIS(g,e)                /* nodebug */
#ifdef __GNUC__
#define ID(e)                   /* nodebug */
#define ID2(e)                   /* nodebug */
#else
#define ID(e)        e
#define ID2(e)     /*  e */
#endif
#define IS(e)                   /* nodebug */

#ifdef __GNUC__
#define LOG(a...)               /* nodebug */
#define GLOG(g,a...)            /* nodebug */
#else    /* !__GNUC__ */
#define LOG         VoidNoGCCMSG
#define GLOG        VoidNoGCCGMSG
#endif   /* __GNUC__ */

#endif   /* DEBUG */

/********************************************************
 * SAFE Only section: Code in nanasssert.c
 */

#ifdef __GNUC__
#ifdef SAFE
#define TRACE nanassertTRACE
void nanassertTRACE(const char *envvar,
                    const char *format,
                    ...);
#else
#define TRACE(a...)             /* nothing */
#endif
#else    /* !__GNUC__ */
#define TRACE nanassertTRACE
void nanassertTRACE(const char *envvar,
                    const char *format,
                    ...);

/* in nanassert.c there is a null function for non gcc */
#endif   /* __GNUC__ */

/********************************************************
 * pyrope specific checks
 */

#ifdef DEBUG
#define I_ONCE_PER_CYCLE(clk) \
{\
  static Time_t last_called = 0;\
  if (last_called) {\
    I((last_called+1) == clk);\
  }\
  last_called++;\
}
#else
#define I_ONCE_PER_CYCLE(clk) {}
#endif

#endif   /* _NANASSERT_H_ */
