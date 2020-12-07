#ifndef SOURCEMAP_FORMAT_V3
#define SOURCEMAP_FORMAT_V3

#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <algorithm>

#include "sourcemap.hpp"
#include "mappings.hpp"

// add namespace for c++
namespace SourceMap
{
	namespace Format
	{
		namespace V3
		{
			const string serialize(const Mappings& map);
			void unserialize(Mappings& map, const string& data);
		}
	}

}
// EO namespace

#endif
