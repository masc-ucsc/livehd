//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif
