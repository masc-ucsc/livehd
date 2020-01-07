/*
   EESESC: Enhance Super ESCalar simulator
   Copyright (C) 2009 University of California, Santa Cruz.

   Contributed by Jose Renau

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
#ifndef ESTL_H
#define ESTL_H

// Diferent compilers have slightly different calling conventions for
// STL. This file has a cross-compiler implementation.

#ifdef __INTEL_COMPILER
#include <ext/hash_map>
#include <ext/hash_set>
#include <ext/slist>
#include <algorithm>
#define HASH_MAP       stlport::hash_map
#define HASH_SET       stlport::hash_set
#define HASH_MULTIMAP  stlport::hash_multimap
#define HASH           std::hash
#define SLIST          std::slist
#elif USE_STL_PORT
/* Sun Studio or Standard compiler -library=stlport5 */
#include <hash_map>
#include <hash_set>
#include <slist>
#include <algorithm>
#define HASH_MAP       std::hash_map
#define HASH_SET       std::hash_set
#define HASH_MULTIMAP  std::hash_multimap
#define HASH           std::hash
#define SLIST          std::slist

/* GNU C Compiler */
#elif __GNUC__ == 4

/* gcc 4.2 and earlier */
#if __GNUC_MINOR__ < 3
#include <ext/hash_map>
#include <ext/hash_set>
#include <ext/slist>
#include <ext/algorithm>
#define HASH_MAP       __gnu_cxx::hash_map
#define HASH_SET       __gnu_cxx::hash_set
#define HASH_MULTIMAP  __gnu_cxx::hash_multimap
#define HASH           __gnu_cxx::hash
#define SLIST          __gnu_cxx::slist

/* gcc 4.3 */
////#elif __GNUC_MINOR__ == 4 
////#include <backward/hash_set>
//#include <ext/hash_map>
//#include <ext/hash_set>
//#include <ext/slist>
//#include <ext/algorithm>
//#define HASH_MAP       __gnu_cxx::hash_map
//#define HASH_SET       __gnu_cxx::hash_set
//#define HASH_MULTIMAP  __gnu_cxx::hash_multimap
//#define HASH           __gnu_cxx::hash
//#define SLIST          __gnu_cxx::slist

/* gcc 4.4 and later */
#elif __GNUC_MINOR__ > 3
#include <boost/functional/hash.hpp>
#include <boost/utility.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <ext/slist>
//#include <ext/hash_map>
#define HASH_MAP       boost::unordered_map
#define HASH_SET       boost::unordered_set
#define HASH_MULTIMAP  boost::unordered_multimap
#define HASH           boost::hash
#define SLIST          __gnu_cxx::slist
namespace boost {
	template <> struct hash<const char *> {
		std::size_t
		operator()(const char * __val) const {
			std::string v = static_cast<std::string>(__val);
			return hash_range(v.begin(),v.end());
		}
    };

	template <> struct hash<char *> {
		std::size_t
		operator()(char * __val) const {
			std::string v = static_cast<std::string>(__val);
			return hash_range(v.begin(),v.end());
		}
    };
}

#endif // GNUC_MINOR
#endif // GNUC

#include <boost/dynamic_bitset.hpp>

#endif // ESTL_H
