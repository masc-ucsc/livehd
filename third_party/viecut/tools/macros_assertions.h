/******************************************************************************
 * macros_assertions.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@kit.edu>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "tools/macros_common.h"

// A custom assertion macro that does not kill the program but prints to
// stderr instead.
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_TRUE(x) do { } while (false);
#else
# define VIECUT_ASSERT_TRUE(expression)                          \
    do {                                                         \
        if (!(expression)) {                                     \
            std::cerr << "ASSERTION FAILED [" << __FILE__ << ":" \
                      << __LINE__ <<                             \
                "]. Asserted: " << STR(expression) << std::endl; \
            abort();                                             \
        }                                                        \
    } while (false)
#endif

// Assert: left != right.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_NEQ(left, right) do { } while (false);
#else
# define VIECUT_ASSERT_NEQ(left, right)                         \
    do {                                                        \
        if ((left) == (right)) {                                \
            std::cerr << "ASSERTION FAILED [" << __FILE__       \
                      << ":" << __LINE__                        \
                      << "]. Asserted: " << STR(left) << " != " \
                      << STR(right) << " but was "              \
                      << left << " == " << right << std::endl;  \
            abort();                                            \
        }                                                       \
    } while (false)
#endif

// Assert: left == right.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_EQ(left, right) do { } while (false);
#else
# define VIECUT_ASSERT_EQ(left, right)                                        \
    do {                                                                      \
        if ((left) != (right)) {                                              \
            std::cerr << "ASSERTION FAILED [" << __FILE__ << ":" << __LINE__  \
                      << "]. Asserted: " << STR(left) << " == " << STR(right) \
                      << " but was "                                          \
                      << left << " != " << right << std::endl;                \
            abort();                                                          \
        }                                                                     \
    } while (false)
#endif

// Assert: left < right.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_LT(left, right) do { } while (false);
#else
# define VIECUT_ASSERT_LT(left, right)                            \
    do {                                                          \
        if ((left) >= (right)) {                                  \
            std::cerr << "ASSERTION FAILED [" << __FILE__ << ":"  \
                      << __LINE__ << "]. Asserted: " << STR(left) \
                      << " < " << STR(right) << " but was "       \
                      << left << " >= " << right << std::endl;    \
            abort();                                              \
        }                                                         \
    } while (false)
#endif

// Assert: left > right.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_GT(left, right) do { } while (false);
#else
# define VIECUT_ASSERT_GT(left, right)                            \
    do {                                                          \
        if ((left) <= (right)) {                                  \
            std::cerr << "ASSERTION FAILED [" << __FILE__ << ":"  \
                      << __LINE__ << "]. Asserted: " << STR(left) \
                      << " > " << STR(right) << " but was "       \
                      << left << " <= " << right << std::endl;    \
            abort();                                              \
        }                                                         \
    } while (false)
#endif

// Assert: left <= right.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_LEQ(left, right) do { } while (false);
#else
# define VIECUT_ASSERT_LEQ(left, right)                                   \
    do {                                                                  \
        if ((left) > (right)) {                                           \
            std::cerr << "ASSERTION FAILED [" << __FILE__                 \
                      << ":" << __LINE__ << "]. Asserted: "               \
                      << STR(left) << " <= " << STR(right) << " but was " \
                      << left << " > " << right << std::endl;             \
            abort();                                                      \
        }                                                                 \
    } while (false)
#endif

// Assert: left >= right.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_GEQ(left, right) do { } while (false);
#else
# define VIECUT_ASSERT_GEQ(left, right)                                   \
    do {                                                                  \
        if ((left) < (right)) {                                           \
            std::cerr << "ASSERTION FAILED [" << __FILE__                 \
                      << ":" << __LINE__ << "]. Asserted: "               \
                      << STR(left) << " >= " << STR(right) << " but was " \
                      << left << " < " << right << std::endl;             \
            abort();                                                      \
        }                                                                 \
    } while (false)
#endif

// Assert: x <= y <= z.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_BETWEEN(x, y, z) do { } while (false);
#else
# define VIECUT_ASSERT_BETWEEN(left, x, right)                       \
    do {                                                             \
        if (((left) > (x)) || ((right) < (x))) {                     \
            std::cerr << "ASSERTION FAILED [" << __FILE__ << ":"     \
                      << __LINE__ << "]. Asserted: "                 \
                      << STR(x) << " in {" << STR(left) << ", ..., " \
                      << STR(right) << "} but was "                  \
                      << x << " not in {" << left                    \
                      << ", ..., " << right << "}." << std::endl;    \
            abort();                                                 \
        }                                                            \
    } while (false)
#endif

// Assert: \forall begin <= i < end: sequence[i] > x.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_RANGE_GT(sequence, begin, end, x, i) do { \
} while (false);
#else
# define VIECUT_ASSERT_RANGE_GT(sequence, begin, end, x, i) \
    for (int i = begin; i < end; ++i) {                     \
        VIECUT_ASSERT_GT(sequence[i], x);                   \
    }
#endif

// Assert: \forall begin <= i < end: sequence[i] >= x.
// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_RANGE_GEQ(sequence, begin, end, x, i) do { \
} while (false);
#else
# define VIECUT_ASSERT_RANGE_GEQ(sequence, begin, end, x, i) \
    for (int i = begin; i < end; ++i) {                      \
        VIECUT_ASSERT_GEQ(sequence[i], x);                   \
    }
#endif

// #ifdef NDEBUG
#if (defined(NDEBUG) || defined(SPEEDPROFILING))
# define VIECUT_ASSERT_RANGE_EQ(sequence, begin, end, x) do { } while (false);
#else
# define VIECUT_ASSERT_RANGE_EQ(sequence, begin, end, x) \
    for (unsigned int i = begin; i < end; ++i) {         \
        VIECUT_ASSERT_EQ(sequence[i], x);                \
    }
#endif
