#ifndef SOURCEMAP_ROW
#define SOURCEMAP_ROW

#include <vector>
#include <string>
#include <cstring>
#include <iostream>

#include "map_col.hpp"
#include "sourcemap.hpp"

// add namespace for c++
namespace SourceMap
{

	class LineMap PANDA_INHERIT
	{
		friend class ColMap;
		friend class Mappings;
		friend class SrcMapDoc;
		public: // ctor
			LineMap();
			virtual ~LineMap();
		public: // setters
			void addEntry(ColMapSP entry);
		public: // getters
			size_t getLength() const;
			size_t getEntryCount() const { return getLength(); };
			const ColMapSP getColMap(size_t idx) const;
			const vector<ColMapSP> at(size_t col) const;
		public: // variables
			vector<ColMapSP> entries;
	};

	// typedef weak_ptr<LineMap> RowWP;
	typedef shared_ptr<LineMap> LineMapSP;

}
// EO namespace

#endif
