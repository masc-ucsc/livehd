#ifndef SOURCEMAP_ENTRY
#define SOURCEMAP_ENTRY

#include <set>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <memory>

#include "sourcemap.hpp"
#include "pos_idx.hpp"

// add namespace for c++
namespace SourceMap
{

	class ColMap PANDA_INHERIT
	{
		friend class LineMap;
		friend class Mappings;
		friend class SrcMapDoc;
		public: // ctor
			ColMap();
			ColMap(size_t col);
			ColMap(size_t col, size_t src_idx, size_t src_ln, size_t src_col);
			ColMap(size_t col, size_t src_idx, size_t src_ln, size_t src_col, size_t tkn_idx);
			virtual ~ColMap();
		public: // operators
			bool operator== (const ColMap &entry) const;
			bool operator!= (const ColMap &entry) const;
			bool operator<= (const ColMap &entry) const;
			bool operator< (const ColMap &entry) const;
			bool operator== (ColMap &entry) const;
			bool operator!= (ColMap &entry) const;
			bool operator<= (ColMap &entry) const;
			bool operator< (ColMap &entry) const;
		public: // setters
			void setType(size_t type) { this->type = type; };
			void setCol(size_t col) { this->col = col; };
			void setSource(size_t src_idx) { this->src_idx = src_idx; };
			void setSrcLine(size_t src_ln) { this->src_line = src_ln; };
			void setSrcCol(size_t src_col) { this->src_col = src_col; };
			void setToken(size_t tkn_idx) { this->tkn_idx = tkn_idx; };
		public: // getters
			size_t getType() const;
			size_t getCol() const;
			size_t getSource() const;
			size_t getSrcLine() const;
			size_t getSrcCol() const;
			size_t getToken() const;
		public: // variables
			size_t type;
			size_t col;
			size_t src_idx;
			size_t src_line;
			size_t src_col;
			size_t tkn_idx;
			std::string_view text;
			// SrcMapWP doc;
	};

	// typedef weak_ptr<ColMap> EntryWP;
	typedef shared_ptr<ColMap> ColMapSP;

	struct ColMapComp {
		bool operator() (const ColMapSP& lhs, const ColMapSP& rhs) const
		{
			return lhs.get() && rhs.get() &&
			       lhs->col < rhs->col;
		}
	};

	typedef std::vector<ColMapSP> ColMapArr;
	typedef std::set<ColMapSP, ColMapComp> ColMapSet;

}
// EO namespace

#endif
