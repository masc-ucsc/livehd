// include library
#include <omp.h>
#include <algorithm>
#include <stdexcept>

#include "mappings.hpp"
#include "v3.hpp"

// add namespace for c++
namespace SourceMap
{

	Mappings::Mappings(size_t version)
	{
		this->version = version;
		this->addNewLine(0);
		this->last_ln_col = 0;
	}

	Mappings::Mappings(string VLQ, size_t version)
	{
		this->version = version;
		this->addNewLine(0);
		this->last_ln_col = 0;
		this->init(VLQ);
	}

	Mappings::~Mappings()
	{ }

	const LineMapSP Mappings::getLineMap(size_t idx) const {
		if (idx >= 0 && idx < rows.size()) return rows[idx];
		else throw(invalid_argument("out of bound"));
	}

	const string Mappings::serialize() const
	{
		if (this->version == 3) {
			return SourceMap::Format::V3::serialize(*this);
		} else {
			throw(runtime_error("unknown format"));
		}
	}

	void Mappings::init(const string& str)
	{
		if (this->version == 3) {
			SourceMap::Format::V3::unserialize(*this, str);
		} else {
			throw(runtime_error("unknown format"));
		}
	}

	void Mappings::addNewLine(const size_t len)
	{
		rows.push_back(make_shared<LineMap>());
		this->last_ln_col = 0;
	}


}
// EO namespace

// implement for c
extern "C"
{

}
