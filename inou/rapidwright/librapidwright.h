#ifndef __LIBRAPIDWRIGHT_H
#define __LIBRAPIDWRIGHT_H

#include <graal_isolate.h>


#if defined(__cplusplus)
extern "C" {
#endif

int RW_create_Design(graal_isolatethread_t*, char*);

int RW_create_FF(graal_isolatethread_t*, char*, int);

int RW_create_AND2(graal_isolatethread_t*, char*, int);

int RW_place_Cell(graal_isolatethread_t*, int, int);

int RW_set_IO_Buffer(graal_isolatethread_t*, int, int);

void RW_place_Design(graal_isolatethread_t*, int);

void RW_connect_Ports(graal_isolatethread_t*, int, int, char*, int, char*);

void RW_route_Design(graal_isolatethread_t*, int);

void RW_write_DCP(graal_isolatethread_t*, char*, int);

void addLogicGate(graal_isolatethread_t*, char*, char*);

#if defined(__cplusplus)
}
#endif
#endif
