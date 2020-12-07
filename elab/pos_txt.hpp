#ifndef SOURCEMAP_POSITION
#define SOURCEMAP_POSITION

#include <vector>
#include <string>
#include <cstring>
#include <iostream>

#include "sourcemap.hpp"

// add namespace for c++
namespace SourceMap
{

	class SrcMapPos
	{
		friend class LineMap;
		friend class Mappings;
		friend class SrcMapDoc;
		public: // ctor
			SrcMapPos();
			SrcMapPos(const string& data);
			SrcMapPos(size_t row, size_t col);
		public: // operators
			// just compares col and row value
			bool operator== (const SrcMapPos& pos) const;
			bool operator!= (const SrcMapPos& pos) const;
			// this plus operation is not commutative
			// so the order of the summands is important
			const SrcMapPos operator+ (const SrcMapPos& pos);
			// minus is not possible, since we do not know the
			// length of each line, so we cannot calculate col
			friend ostream& operator<<(ostream& os, const SrcMapPos& pos);
		public: // setters
			void getLineMap(size_t row) { this->row = row; };
			void getCol(size_t col) { this->col = col; };
		public: // getters
			size_t getLineMap() const { return row; };
			size_t getCol() const { return col; };
		public: // variables
			size_t row;
			size_t col;

	};

	// are these really that usefull?
	const SrcMapPos SrcMapPosNull(0, 0);
	const SrcMapPos SrcMapPosStart(0, 0);
	const SrcMapPos SrcMapPosError(-1, -1);

}
// EO namespace

#endif
