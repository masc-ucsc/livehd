#ifndef __LIBRAPIDWRIGHT_H
#define __LIBRAPIDWRIGHT_H

#include <graal_isolate.h>


#if defined(__cplusplus)
extern "C" {
#endif

int RW_Create_Design(graal_isolatethread_t*, char*);

void RW_Create_FF(graal_isolatethread_t*, char*, int);

void RW_Create_AND2(graal_isolatethread_t*, char*, int);

int RW_set_IO_Buffer(graal_isolatethread_t*, int, int);

void RW_place_Design(graal_isolatethread_t*, int);

void RW_route_Design(graal_isolatethread_t*, int);

void RW_write_DCP(graal_isolatethread_t*, char*, int);

void loadDevice(graal_isolatethread_t*, char*);

char* getTileName(graal_isolatethread_t*, char*, int, int);

void addLogicGate(graal_isolatethread_t*, char*, char*);

#if defined(__cplusplus)
}
#endif
#endif
