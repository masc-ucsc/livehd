// include library
#include <algorithm>
#include <stdexcept>

#include "pos_txt.hpp"

// add namespace for c++
namespace SourceMap
{

	SrcMapPos::SrcMapPos()
	{
		this->row = -1;
		this->col = -1;
	}

	SrcMapPos::SrcMapPos(size_t row, size_t col)
	{
		this->row = row;
		this->col = col;
	}

	SrcMapPos::SrcMapPos(const string& data)
	{
		this->row = 0;
		this->col = 0;
		size_t prev = 0;
		size_t last = data.size();
		for(size_t i = 0; i < last; ++i) {
			if (data[i] == '\n') {
				++ this->row;
				prev = i + 1;
			}
		}
		this->col = last - prev;
	}

	const SrcMapPos SrcMapPos::operator+ (const SrcMapPos &pos)
	{
		return SrcMapPos(row + pos.row, pos.row > 0 ? pos.col : pos.col + col);
	}
	bool SrcMapPos::operator== (const SrcMapPos &pos) const
	{
		return row == pos.row && col == pos.col;
	}
	bool SrcMapPos::operator!= (const SrcMapPos &pos) const
	{
		return row != pos.row || col != pos.col;
	}

}
// EO namespace

// implement for c
extern "C"
{

}
