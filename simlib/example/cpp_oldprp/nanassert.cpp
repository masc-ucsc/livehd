/* 
   ESESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.

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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef __cplusplus
#include "estl.h"
#endif

#include <alloca.h>

#include "nanassert.h"

// Any object may redefine this variable as local, and it would be
// printed next to the file name. This is specially useful for
// templetes.
const char *NanassertID = "";

#include <signal.h>

void nanassertexit(){
#if (defined TLS)
  // Raising SIGUSR2 here helps debugging a lot
  // It is ignored in normal execution, but stops execution in gdb
  raise(SIGUSR2);
#endif
  return;
}

/* Compile only when there is no GCC compiler */
#if (defined SUNSTUDIO) || !(defined __GNUC__)

void VoidNoGCCMSG(const char *format, ...) {
}

void VoidNoGCCGMSG(int32_t g, const char *format, ...) {
}

void NoGCCMSG(const char *format, ...) {

  va_list ap;

  va_start(ap, format);
  vfprintf(ASSERTSTREAM, format, ap);
  va_end(ap);
  fprintf(ASSERTSTREAM, "\n");
}

void NoGCCGMSG(int32_t g, const char *format, ...) {

  va_list ap;

  if(!g)
    return;

  va_start(ap, format);
  vfprintf(ASSERTSTREAM, format, ap);
  va_end(ap);
  fprintf(ASSERTSTREAM, "\n");
}

#endif   /* __GNUC__ */

#ifndef SAFE
#ifdef __GNUC__

/* defined in nanassert.h */
#else
void nanassertTRACE(const char *envvar,
                    const char *format,
                    ...)
{                               /* Nothing */
}
#endif   /* __GNUC__ */
#else    /* SAFE */

#ifdef __cplusplus

typedef HASH_MAP<const char *, bool, HASH<const char *> > NanaHash;

static NanaHash *trace;

bool cachedGetenv(const char *envvar)
{
    NanaHash::iterator pos = trace->find(envvar);

    if(pos == trace->end()) {
        if(getenv(envvar)) {
            (*trace)[envvar] = true;
            return true;
        } else
            (*trace)[envvar] = false;
    } else {
        if((*pos).second)
            return true;
    }
    return false;
}
#endif

void nanassertTRACE(const char *envvar,
                    const char *format,
                    ...)
{
  static int32_t doTrace = -1;
  int32_t found;
  va_list ap;

  if(doTrace == -1) {
    if(getenv("TRACE"))
      doTrace = atoi(getenv("TRACE"));
    if(doTrace < 0)
      doTrace = 0;
    else {
      if(doTrace == 1)
        MSG("nanassert::Activating TRACE selectivelly");
      else
        MSG("nanassert::Activating all the TRACEs");
    }
#ifdef __cplusplus
    // new allocation because the object never should be destroyed
    trace = new NanaHash;
#endif
  } else if(doTrace == 0) {
    return;
  } else if(doTrace == 1) {

    I(envvar != 0);

#ifdef __cplusplus
    found = cachedGetenv(envvar) ? 1 : 0;
#else
    found = getenv(envvar) ? 1 : 0;
#endif
    if(!found)
      return;
  }

  fprintf(ASSERTSTREAM, "TRACE:%s", envvar);

  va_start(ap, format);
  vfprintf(ASSERTSTREAM, format, ap);
  va_end(ap);
  fprintf(ASSERTSTREAM, "\n");
}
#endif   /* TRACE */
