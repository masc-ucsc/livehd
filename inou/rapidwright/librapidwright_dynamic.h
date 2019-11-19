#ifndef __LIBRAPIDWRIGHT_H
#define __LIBRAPIDWRIGHT_H

#include <graal_isolate_dynamic.h>


#if defined(__cplusplus)
extern "C" {
#endif

typedef int (*RW_create_Design_fn_t)(graal_isolatethread_t*, char*);

typedef int (*RW_create_FF_fn_t)(graal_isolatethread_t*, char*, int);

typedef int (*RW_create_AND2_fn_t)(graal_isolatethread_t*, char*, int);

typedef void (*RW_place_Cell_fn_t)(graal_isolatethread_t*, int, int);

typedef int (*RW_set_IO_Buffer_fn_t)(graal_isolatethread_t*, int, int);

typedef void (*RW_place_Design_fn_t)(graal_isolatethread_t*, int);

typedef void (*RW_costumRoute_fn_t)(graal_isolatethread_t*, int, int, int);

typedef void (*RW_route_Design_fn_t)(graal_isolatethread_t*, int);

typedef void (*RW_write_DCP_fn_t)(graal_isolatethread_t*, char*, int);

typedef void (*addLogicGate_fn_t)(graal_isolatethread_t*, char*, char*);

#if defined(__cplusplus)
}
#endif
#endif
