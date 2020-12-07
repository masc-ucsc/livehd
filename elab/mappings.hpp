#ifndef SOURCEMAP_MAPPING
#define SOURCEMAP_MAPPING

#include <vector>
#include <string>
#include <cstring>
#include <iostream>

#include "map_line.hpp"
#include "sourcemap.hpp"
#include "pos_txt.hpp"

// add namespace for c++
namespace SourceMap
{

	class Mappings PANDA_INHERIT
	{
		friend class ColMap;
		friend class LineMap;
		friend class SrcMapDoc;
		public: // ctor
			Mappings(size_t version = 3);
			// we only implement v3 currently
			Mappings(string VLQ, size_t version = 3);
			virtual ~Mappings();
		public: // operators
			friend ostream& operator<<(ostream& os, const Mappings& map);
		public: // setters
			void addNewLine(size_t len = 0);
			// void addNewLine(const char* line);
		public: // getters
			const LineMapSP getLineMap(size_t idx) const;
			size_t getRowCount() const { return rows.size(); };
			size_t getLastRowSize() const { return last_ln_col; };
			void setLastRowSize(size_t col) { last_ln_col = col; };
			SrcMapPos getSize() const {
				return SrcMapPos(
					getRowCount(),
					getLastRowSize()
				);
			};
		public: // getters
			const string serialize() const;
		public: // route to row
			vector<ColMapSP> at(size_t row, size_t col) const
			{ return getLineMap(row)->at(col); };
			vector<ColMapSP> at(const SrcMapPos& pos) const
			{ return getLineMap(pos.row)->at(pos.col); };
		public: // variables
			// SrcMapPos size;
			vector<LineMapSP> rows;
		private: // variables
			size_t version;
			size_t last_ln_col;
			void init(const string& VLQ);
	};

	// typedef weak_ptr<Mappings> MappingWP;
	typedef shared_ptr<Mappings> MappingsSP;

}
// EO namespace

#endif
