// include library
#include <algorithm>
#include <stdexcept>

#include "pos_idx.hpp"

// add namespace for c++
namespace SourceMap
{

	SrcMapIdx::SrcMapIdx()
	{
		this->row = -1;
		this->idx = -1;
	}

	SrcMapIdx::SrcMapIdx(size_t row, size_t idx)
	{
		this->row = row;
		this->idx = idx;
	}

	const SrcMapIdx SrcMapIdx::operator+ (const SrcMapIdx &pos)
	{
		return SrcMapIdx(row + pos.row, pos.row > 0 ? pos.idx : pos.idx + idx);
	}
	bool SrcMapIdx::operator== (const SrcMapIdx &pos) const
	{
		return row == pos.row && idx == pos.idx;
	}
	bool SrcMapIdx::operator!= (const SrcMapIdx &pos) const
	{
		return row != pos.row || idx != pos.idx;
	}

}
// EO namespace

// implement for c
extern "C"
{

}
