#ifndef __GRAAL_ISOLATE_H
#define __GRAAL_ISOLATE_H

/*
 * Structure representing an isolate. A pointer to such a structure can be
 * passed to an entry point as the execution context.
 */
struct __graal_isolate_t;
typedef struct __graal_isolate_t graal_isolate_t;

/*
 * Structure representing a thread that is attached to an isolate. A pointer to
 * such a structure can be passed to an entry point as the execution context,
 * requiring that the calling thread has been attached to that isolate.
 */
struct __graal_isolatethread_t;
typedef struct __graal_isolatethread_t graal_isolatethread_t;

#ifdef _WIN64
typedef unsigned long long ulong;
#else
typedef unsigned long ulong;
#endif


/* Parameters for the creation of a new isolate. */
enum { __graal_create_isolate_params_version = 1 };
struct __graal_create_isolate_params_t {
    int version;                                /* Version of this struct */

    /* Fields introduced in version 1 */
    ulong reserved_address_space_size;  /* Size of address space to reserve */
};
typedef struct __graal_create_isolate_params_t graal_create_isolate_params_t;

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Create a new isolate, considering the passed parameters (which may be NULL).
 * Returns 0 on success, or a non-zero value on failure.
 * On success, the current thread is attached to the created isolate, and the
 * address of the isolate and the isolate thread are written to the passed pointers
 * if they are not NULL.
 */
int graal_create_isolate(graal_create_isolate_params_t* params, graal_isolate_t** isolate, graal_isolatethread_t** thread);

/*
 * Attaches the current thread to the passed isolate.
 * On failure, returns a non-zero value. On success, writes the address of the
 * created isolate thread structure to the passed pointer and returns 0.
 * If the thread has already been attached, the call succeeds and also provides
 * the thread's isolate thread structure.
 */
int graal_attach_thread(graal_isolate_t* isolate, graal_isolatethread_t** thread);

/*
 * Given an isolate to which the current thread is attached, returns the address of
 * the thread's associated isolate thread structure.  If the current thread is not
 * attached to the passed isolate or if another error occurs, returns NULL.
 */
graal_isolatethread_t* graal_get_current_thread(graal_isolate_t* isolate);

/*
 * Given an isolate thread structure, determines to which isolate it belongs and returns
 * the address of its isolate structure. If an error occurs, returns NULL instead.
 */
graal_isolate_t* graal_get_isolate(graal_isolatethread_t* thread);

/*
 * Detaches the passed isolate thread from its isolate and discards any state or
 * context that is associated with it. At the time of the call, no code may still
 * be executing in the isolate thread's context.
 * Returns 0 on success, or a non-zero value on failure.
 */
int graal_detach_thread(graal_isolatethread_t* thread);

/*
 * Using the context of the isolate thread from the first argument, detaches the
 * threads in an array pointed to by the second argument, with the length of the
 * array given in the third argument. All of the passed threads must be in the
 * same isolate, including the first argument. None of the threads to detach may
 * execute Java code at the time of the call or later without reattaching first,
 * or their behavior will be entirely undefined. The current thread may be part of
 * the array, however, using detach_thread() should be preferred for detaching only
 * the current thread.
 * Returns 0 on success, or a non-zero value on failure.
 */
int graal_detach_threads(graal_isolatethread_t* thread, graal_isolatethread_t** array, int length);

/*
 * Tears down the passed isolate, waiting for any attached threads to detach from
 * it, then discards the isolate's objects, threads, and any other state or context
 * that is associated with it.
 * Returns 0 on success, or a non-zero value on failure.
 */
int graal_tear_down_isolate(graal_isolatethread_t* isolateThread);

#if defined(__cplusplus)
}
#endif
#endif
