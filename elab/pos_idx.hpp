#ifndef SOURCEMAP_INDEX
#define SOURCEMAP_INDEX

#include <vector>
#include <string>
#include <cstring>
#include <iostream>

#include "sourcemap.hpp"

// add namespace for c++
namespace SourceMap
{

	class SrcMapIdx
	{
		friend class LineMap;
		friend class Mappings;
		friend class SrcMapDoc;
		public: // ctor
			SrcMapIdx();
			SrcMapIdx(size_t row, size_t idx);
		public: // operators
			// just compares idx and row value
			bool operator== (const SrcMapIdx& pos) const;
			bool operator!= (const SrcMapIdx& pos) const;
			// this plus operation is not commutative
			// so the order of the summands is important
			const SrcMapIdx operator+ (const SrcMapIdx& pos);
			// minus is not possible, since we do not know the
			// length of each line, so we cannot calculate idx
			friend ostream& operator<<(ostream& os, const SrcMapIdx& pos);
		public: // setters
			void getLineMap(size_t row) { this->row = row; };
			void getCol(size_t idx) { this->idx = idx; };
		public: // getters
			size_t getLineMap() const { return row; };
			size_t getCol() const { return idx; };
		public: // variables
			size_t row;
			size_t idx;

	};

	// are these really that usefull?
	const SrcMapIdx SrcMapIdxNull(0, 0);
	const SrcMapIdx SrcMapIdxError(-1, -1);

}
// EO namespace

#endif
