// include library
#include <omp.h>
#include <set>
#include <stdexcept>
#include <algorithm>

#include "document.hpp"

// using string
using namespace std;

// add namespace for c++
namespace SourceMap
{

	LineMap::LineMap()
	: entries()
	{ }

	LineMap::~LineMap()
	{ }

	size_t LineMap::getLength() const {
		return entries.size();
	}

	const ColMapSP LineMap::getColMap(size_t idx) const {
		if (idx >= 0 && idx < entries.size()) {
			return entries[idx];
		}
		throw(invalid_argument("getColMap out of bound"));
	}

	void LineMap::addEntry(ColMapSP entry)
	{
		entries.push_back(entry);
	}

	const vector<ColMapSP> LineMap::at(size_t col) const
	{
		vector<ColMapSP> entries;
		auto entry_it = this->entries.begin();
		auto entry_end = this->entries.end();
		// search from left up to position
		for(; entry_it != entry_end; ++entry_it) {
			ColMapSP entry = *entry_it;
			// collect entries on position
			if (entry->col == col)
			{ entries.push_back(entry); }
			// abort the loop after position
			else if (entry->col > col) break;
		}
		return entries;
	}


}
// EO namespace
