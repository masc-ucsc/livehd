// include library
#include <omp.h>
#include <algorithm>
#include <stdexcept>

#include "map_col.hpp"

// add namespace for c++
namespace SourceMap
{

	ColMap::ColMap()
	{
		this->type = 0;
		this->col = 0;
		this->src_idx = 0;
		this->src_line = 0;
		this->src_col = 0;
		this->tkn_idx = 0;
		this->text  = std::string_view{""};
	}

	ColMap::~ColMap()
	{ }

	ColMap::ColMap(size_t col)
	{
		this->type = 1;
		this->col = col;
		this->src_idx = 0;
		this->src_line = 0;
		this->src_col = 0;
		this->tkn_idx = 0;
		this->text  = std::string_view{""};
	}

	ColMap::ColMap(size_t col, size_t idx, size_t src_ln, size_t src_col)
	{
		this->type = 4;
		this->col = col;
		this->src_idx = idx;
		this->src_line = src_ln;
		this->src_col = src_col;
		this->tkn_idx = 0;
		this->text  = std::string_view{""};
	}

	ColMap::ColMap(size_t col, size_t idx, size_t src_ln, size_t src_col, size_t token)
	{
		this->type = 5;
		this->col = col;
		this->src_idx = idx;
		this->src_line = src_ln;
		this->src_col = src_col;
		this->tkn_idx = token;
		this->text  = std::string_view{""};
	}

	size_t ColMap::getType() const { return this->type; }

	bool ColMap::operator== (const ColMap &entry) const { return (col == entry.col); }
	bool ColMap::operator!= (const ColMap &entry) const { return (col != entry.col); }
	bool ColMap::operator<  (const ColMap &entry) const { return (col <  entry.col); }
	bool ColMap::operator<= (const ColMap &entry) const { return (col <= entry.col); }

	// ToDo: check for out of bound access
	size_t ColMap::getCol() const { return this->col; }
	size_t ColMap::getSource() const { return this->src_idx; }
	size_t ColMap::getSrcLine() const { return this->src_line; }
	size_t ColMap::getSrcCol() const { return this->src_col; }
	size_t ColMap::getToken() const { return this->tkn_idx; }

}
// EO namespace

// implement for c
extern "C"
{

}
