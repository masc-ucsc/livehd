// include library
#include <omp.h>
#include <string>
#include <istream>
#include <ostream>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#ifndef BUFFERSIZE
#define BUFFERSIZE 1024
#endif

// our own header
#include "sourcemap.hpp"
#include "document.hpp"
#include "decode.h"

JsonNode* json_import(const char* str) {
	if (std::string(str) == "null") { return json_mknull(); }
	else if (std::string(str) == "true") { return json_mkbool(true); }
	else if (std::string(str) == "false") { return json_mkbool(false); }
	else { return json_mkstring(str); }
}

#define json_double(json_node)           \
  (!json_node ? -1 :                     \
      json_node->tag == JSON_NUMBER ?    \
        json_node->number_ :             \
      json_node->tag == JSON_STRING ?    \
        atof(json_node->string_) :       \
          -1                             \
  )

#define json_export(json_node)           \
  (!json_node ? "null" : string(         \
      json_node->tag == JSON_STRING ?    \
        json_node->string_ :             \
          json_stringify(json_node, "")  \
  ))

// add namespace for c++
namespace SourceMap
{

  std::istream& getline(std::istream& is, std::string& t)
  {
      t.clear();

      // The characters in the stream are read one-by-one using a std::streambuf.
      // That is faster than reading them one-by-one using the std::istream.
      // Code that uses streambuf this way must be guarded by a sentry object.
      // The sentry object performs various tasks,
      // such as thread synchronization and updating the stream state.

      std::istream::sentry se(is, true);
      std::streambuf* sb = is.rdbuf();

      for(;;) {
          int c = sb->sbumpc();
          switch (c) {
          case '\n':
              return is;
          case '\r':
              if(sb->sgetc() == '\n')
                  sb->sbumpc();
              return is;
          case EOF:
              // Also handle the case when the last line has no line ending
              if(t.empty())
                  is.setstate(std::ios::eofbit);
              return is;
          default:
              t += (char)c;
          }
      }
  }

	SrcMapDoc::SrcMapDoc ()
	{
		this->map = make_shared<Mappings>();
	}

	SrcMapDoc::~SrcMapDoc ()
	{ }

	SrcMapDoc::SrcMapDoc (const JsonNode& json_node)
	{
		this->init(json_node);
	}

	bool match(const string& haystack, const string& needle, size_t& i)
	{
		size_t l = needle.length();
		while (isspace(haystack[i])) ++i;
		if (haystack.substr(i, l) == needle) {
			i += l; return true;
		}
		return false;
	}

	SrcMapDoc::SrcMapDoc(const string& str)
	{
		// decode the json string first into a json_node
		std::string line;
		std::istringstream s(str);
		while(std::getline(s, line))
		{

			size_t i = 0;
			if (!match(line, "/*#", i)) continue;
			if (!match(line, "sourceMappingURL", i)) continue;
			if (!match(line, "=", i)) continue;
			if (!match(line, "data", i)) continue;
			if (!match(line, ":", i)) continue;
			if (!match(line, "application/json", i)) continue;
			if (!match(line, ";", i)) continue;
			if (!match(line, "base64", i)) continue;
			if (!match(line, ",", i)) continue;
			while (isspace(line[i])) ++i;
			size_t start = i;
			while (line[i]) {
				const char c = line[i];
				if (c >= 'A' && c <= 'Z') { ++i; continue; }
				if (c >= 'a' && c <= 'z') { ++i; continue; }
				if (c >= '0' && c <= '9') { ++i; continue; }
				if (c == '+' || c == '/') { ++i; continue; }
				if (c == '=' || c == '=') { ++i; continue; }
				break;
			}
			size_t stop = i;

			string istr(line.substr(start, stop - start));
			base64::decoder E;
			istringstream istrm(istr);
			stringstream ostrm;
			E.decode(istrm, ostrm);
			string ostr(ostrm.str());

			JsonNode* json_node = json_decode(ostr.c_str());
			if (!json_node) throw(runtime_error("invalid json_node"));
			this->init(*json_node);
			return;

		}

		JsonNode* json_node = json_decode(str.c_str());
		if (!json_node) throw(runtime_error("invalid json_node"));

		this->init(*json_node);
	}

	const ColMapSP SrcMapDoc::getColMap(size_t row, size_t idx) const
	{
		return map->getLineMap(row)->getColMap(idx);
	}
	const ColMapSP SrcMapDoc::getColMap(const SrcMapIdx& idx) const
	{
		return map->getLineMap(idx.row)->getColMap(idx.idx);
	}
	const vector<ColMapSP> SrcMapDoc::at(size_t row, size_t col) const
	{
		return map->getLineMap(row)->at(col);
	}
	const vector<ColMapSP> SrcMapDoc::at(const SrcMapPos& pos) const
	{
		return map->getLineMap(pos.row)->at(pos.col);
	}

	const string SrcMapDoc::getFile() const { return file; }
	const string SrcMapDoc::getRoot() const { return root; }
	const MappingsSP SrcMapDoc::getMap() const { return map; }

	const string SrcMapDoc::getToken(size_t idx) const
	{
		if (idx < 0 || idx >= tokens.size())
		{ throw(runtime_error("token access out of bound")); }
		return tokens[idx];
	}

	const string SrcMapDoc::getSource(size_t idx) const
	{
		if (idx < 0 || idx >= sources.size())
		{ throw(runtime_error("source access out of bound")); }
		return sources[idx];
	}

	const string SrcMapDoc::getContent(size_t idx) const
	{
		if (idx < 0 || idx >= contents.size())
		{ throw(runtime_error("content access out of bound")); }
		return contents[idx];
	}

	ostream& operator<<(ostream& os, const ColMap& entry)
	{
		if (entry.getType() == 1) {
			return os << "[" << entry.col << "]";
		} else if (entry.getType() == 4) {
			return os << "[" << entry.col << "," << entry.src_idx <<
			       "," << entry.src_line << "," << entry.src_col << "]";
		} else if (entry.getType() == 5) {
			return os << "[" << entry.col << "," << entry.src_idx <<
			       "," << entry.src_line << "," << entry.src_col <<
			       "," << entry.tkn_idx << "]";
		} else {
			return os;
		}
	}

	ostream& operator<<(ostream& os, const Mappings& map)
	{
		size_t count = 0; // count total number of entries
		const_foreach(LineMapSP, row, map.rows) count += (*row)->getLength();
		return os << "{" << map.rows.size() << ":" << count << "}";
	}

	ostream& operator<<(ostream& os, const SrcMapPos& pos)
	{
		return os << "[" << pos.row << "|" << pos.col << "]";
	}


	void SrcMapDoc::init (const JsonNode& json_cnode)
	{


		// remove constness ???!!
		JsonNode json_node = json_cnode;

		if (json_node.tag == JSON_OBJECT) {

			JsonNode* json_file = json_find_member(&json_node, "file"); // string
			JsonNode* json_root = json_find_member(&json_node, "sourceRoot"); // string
			JsonNode* json_version = json_find_member(&json_node, "version"); // string
			JsonNode* json_mappings = json_find_member(&json_node, "mappings"); // string
			JsonNode* json_tokens = json_find_member(&json_node, "names"); // array
			JsonNode* json_sources = json_find_member(&json_node, "sources"); // array
			JsonNode* json_contents = json_find_member(&json_node, "sourcesContent"); // array
			JsonNode* json_lastLineLength = json_find_member(&json_node, "x_lastLineSize"); // string

			file = json_export(json_file);
			root = json_export(json_root);
			version = json_export(json_version);

			// assetion for version (must be defined first in source map!)
			if (json_export(json_version) != "3") {
				// must be defined first in source map actually
				throw(runtime_error("only source map version 3 is supported"));
			}

			if (json_tokens && (
					json_tokens->tag == JSON_ARRAY ||
					json_tokens->tag == JSON_OBJECT
			)) {
				JsonNode* json_token(0);
				json_foreach(json_token, json_tokens) {
					tokens.push_back(json_export(json_token));
				}
			}

			if (json_sources && (
					json_sources->tag == JSON_ARRAY ||
					json_sources->tag == JSON_OBJECT
			)) {
				JsonNode* json_source(0);
				json_foreach(json_source, json_sources) {
					sources.push_back(json_export(json_source));
				}
			}

			if (json_contents && (
					json_contents->tag == JSON_ARRAY ||
					json_contents->tag == JSON_OBJECT
			)) {
				JsonNode* json_content(0);
				json_foreach(json_content, json_contents) {
					contents.push_back(json_export(json_content));
				}
			}

			map = SourceMap::make_shared<Mappings>(json_export(json_mappings));

			map->last_ln_col = (int) json_double(json_lastLineLength);

		}

	}

	char* SrcMapDoc::serialize(bool enc) const
	{

		JsonNode* json_node = json_mkobject();

		// version must be the first thing (by specification)
		json_append_member(json_node, "version", json_mknumber(3));

		JsonNode* json_file = json_import(file.c_str());
		json_append_member(json_node, "file", json_file);

		size_t sources_size = sources.size();
		JsonNode* json_sources = json_mkarray();
		for (size_t i = 0; i < sources_size; ++i) {
			const char* include = sources[i].c_str();
			JsonNode* json_source = json_import(include);
			json_append_element(json_sources, json_source);
		}
		json_append_member(json_node, "sources", json_sources);

		size_t contents_size = contents.size();
		JsonNode* json_contents = json_mkarray();
		for (size_t i = 0; i < contents_size; ++i) {
			const char* content = contents[i].c_str();
			JsonNode* json_content = json_import(content);
			json_append_element(json_contents, json_content);
		}
		json_append_member(json_node, "sourcesContent", json_contents);

		string mappings = map->serialize();
		JsonNode* json_mappings = json_import(mappings.c_str());
		json_append_member(json_node, "mappings", json_mappings);

		size_t tokens_size = tokens.size();
		JsonNode* json_tokens = json_mkarray();
		for (size_t i = 0; i < tokens_size; ++i) {
			const char* token = tokens[i].c_str();
			JsonNode* json_token = json_import(token);
			json_append_element(json_tokens, json_token);
		}
		json_append_member(json_node, "names", json_tokens);

		// insert optional and custom attribute to store the last line length
		// this information would otherwise be lost and is needed for appends etc.
		json_append_member(json_node, "x_lastLineSize", json_mknumber(map->last_ln_col));

		// get string from json encode
		// caller must free it himself
		char* str = json_encode(json_node);
		json_delete(json_node);
		return str;

	}








	// add sources from srcmap to our own
	// update mappings from srcmap to new index
	void SrcMapDoc::mergePrepare(SrcMapDoc srcmap)
	{

		foreach(string, source, srcmap.sources) {
			sources.push_back(*source);
		}

		size_t offset = sources.size();

		if (true) {
			const_foreach(LineMapSP, row, srcmap.map->rows) {
				cerr << (*row)->entries.size() << endl;
			}
		}

		foreach(LineMapSP, row, srcmap.map->rows) {
			foreach(ColMapSP, entry, (*row)->entries) {
				if ((*entry)->getType() > 0)
					(*entry)->src_idx += offset;
			}
		}

	}

	void SrcMapDoc::append(LineMap row)
	{
	}
	void SrcMapDoc::prepend(LineMap row)
	{
	}

	void SrcMapDoc::remap(SrcMapDoc srcmap)
	{

		size_t i = 0;

		//double start = omp_get_wtime();
		//double end = omp_get_wtime();

		size_t offset = sources.size();

		foreach(string, source, srcmap.sources) {
			sources.push_back(*source);
		}

		foreach(LineMapSP, row, map->rows) {
			foreach_ptr(ColMap, entry, (*row)->entries) {

				if (entry->src_idx != 0) continue;

				// our own source map can only be of one file
				vector<ColMapSP> originals = srcmap.map->rows[i]->at(entry->getCol());

				if (originals.size() >= 1) {
					const_foreach_ptr(ColMap, original, originals) {
						if (entry->getType() > 0) {
							entry->col = original->col;
						}
						if (entry->getType() > 3) {
							entry->src_idx = original->src_idx + offset;
							entry->src_line = original->src_line;
							entry->src_col = original->src_col;
						}
						if (entry->getType() > 4) {
							entry->tkn_idx = original->tkn_idx;
						}
					}
				}
			}
			++i;
		}
		//end = omp_get_wtime();

		//cerr << "Benchmark: " << (end - start) << endl;
		cerr << "Benchmark: (missing, disabled omp_get_wtime)" << endl;

	}

	// STL helper
	struct adjust_col
	{
		public:
			adjust_col(int off) : offset(off) {}
			void operator()(ColMapSP entry)
			{
				entry->setCol(entry->getCol() + offset);
			}
		private:
			int offset;
	};

	void SrcMapDoc::insert(SrcMapPos pos, const SrcMapDoc& srcmap)
	{
		// ToDo: adapt srcmap for insert
		insert(pos, *srcmap.map);
	}

	// bread and butter function that implements all operations
	void SrcMapDoc::splice(SrcMapPos pos, SrcMapPos del, const SrcMapDoc& srcmap)
	{
		// ToDo: adapt splice for insert
		splice(pos, del, *srcmap.map);
	}





	void SrcMapDoc::remove(SrcMapPos pos, SrcMapPos del)
	{

		// some basic assertions to avoid strange bugs
		if (pos.col == string::npos) throw(invalid_argument("remove pos.col is invalid"));
		if (pos.row == string::npos) throw(invalid_argument("remove pos.row is invalid"));
		if (del.col == string::npos) throw(invalid_argument("remove del.col is invalid"));
		if (del.row == string::npos) throw(invalid_argument("remove del.row is invalid"));
		if (map->getLastRowSize() == string::npos) throw(invalid_argument("remove size.col is invalid"));
		if (map->getRowCount() == string::npos) throw(invalid_argument("remove size.row is invalid"));
		// check for access violations
		if (map->rows.size() == 0) throw(runtime_error("empty srcmap"));
		if (map->rows.size() <= pos.row) throw(out_of_range("access out of bound"));
		if (map->rows.size() <= pos.row + del.row) throw(out_of_range("delete out of bound"));

		// find the position where the remove should be placed
		vector<ColMapSP> &first_row = map->rows[pos.row]->entries;
		vector<ColMapSP>::iterator pos_it = first_row.begin();
		vector<ColMapSP>::iterator first_row_end = first_row.end();
		// loop until we reach the remove position
		for(; pos_it != first_row_end; ++pos_it)
		{ if ((*pos_it)->col >= pos.col) break; }
		// also get a numeric offset, since any
		// insertion will invalidate all iterators
		// size_t pos_idx = pos_it - first_row.begin();

		if (del.row > 0) {

			// erase everything after the pos.col
			first_row.erase(pos_it, first_row_end);

			vector<ColMapSP> &last_row = map->rows[pos.row + del.row]->entries;
			vector<ColMapSP>::iterator last_row_it = last_row.begin();
			vector<ColMapSP>::iterator last_row_end = last_row.end();
			// loop until we reach the remove position
			for(; last_row_it != last_row_end; ++last_row_it)
			{ if ((*last_row_it)->col >= del.col) break; }

			// adjust the remaining entries for the removed range
			for_each(last_row_it, last_row.end(), adjust_col(- del.col));

			first_row.insert(
				first_row.end(),
				last_row_it,
				last_row.end()
			);

			// remove entries from the last line to remove
			// last_row.erase(last_row.begin(), last_row_it);

			// remove full lines
			// excluding the last
			map->rows.erase(
				map->rows.begin() + pos.row + 1,
				map->rows.begin() + pos.row + 1 + del.row
			);

		} else {

			vector<ColMapSP>::iterator remove_it = pos_it;
			// loop until we reach the remove position
			for(; remove_it != first_row_end; ++remove_it)
			{ if ((*remove_it)->col >= pos.col + del.col) break; }
			// adjust the remaining entries for the removed range
			for_each(remove_it, first_row.end(), adjust_col(- del.col));
			// remove the range we have found
			first_row.erase(pos_it, remove_it);

		}

		// sync the row size stat variable
		// ToDo: use other way, too error prone
		// map->size.row = map->rows.size();

	}
	// EO remove

	void SrcMapDoc::insert(SrcMapPos pos, const Mappings& insert)
	{

		// some basic assertions to avoid strange bugs
		if (pos.col == string::npos) throw(invalid_argument("insert pos.col is invalid"));
		if (pos.row == string::npos) throw(invalid_argument("insert pos.row is invalid"));
		if (map->getLastRowSize() == string::npos) throw(invalid_argument("remove size.col is invalid"));
		if (map->getRowCount() == string::npos) throw(invalid_argument("remove size.row is invalid"));
		if (insert.getLastRowSize() == string::npos) throw(invalid_argument("insert insert.col is invalid"));
		if (insert.getRowCount() == string::npos) throw(invalid_argument("insert insert.row is invalid"));
		// check for access violations
		if (map->rows.size() == 0) throw(runtime_error("empty srcmap"));
		if (map->rows.size() <= pos.row) throw(out_of_range("access out of bound"));

		// find the position where the insert should be placed
		vector<ColMapSP> &first_row = map->rows[pos.row]->entries;
		vector<ColMapSP>::iterator insert_it = first_row.begin();
		vector<ColMapSP>::iterator first_row_end = first_row.end();
		// loop until we reach the insert position
		for(; insert_it != first_row_end; ++insert_it)
		{ if ((*insert_it)->col >= pos.col) break; }
		// also get a numeric offset, since any
		// insertion will invalidate all iterators
		size_t ins_idx = insert_it - first_row.begin();

		// splitting up a row
		if (insert.rows.size() > 1) {

			// adjust trailing entries to account for new offset
			for_each(insert_it, first_row_end, adjust_col(insert.getLastRowSize() - pos.col));

			// insert all full lines
			// excluding the first line
			map->rows.insert(
				map->rows.begin() + pos.row + 1,
				insert.rows.begin() + 1, insert.rows.end()
			);

			size_t ins_row = insert.getRowCount() - 1;
			// move trailing entries from the first line of insert
			// to the last line that has been inserted previously
			map->rows[pos.row + ins_row]->entries.insert(
				map->rows[pos.row + ins_row]->entries.end(),
				map->rows[pos.row]->entries.begin() + ins_idx,
				map->rows[pos.row]->entries.end()
			);

			// get position to up we will remove the items later
			// we do not remove them right away, as we possibly have
			// the same vector in map and insert. Therefore we first
			// want to make the copy before removing the entries!
			size_t del_stop_idx = map->rows[pos.row]->entries.size();

			// copy entries of first line to be inserted
			// they go the the end of the first insert line
			map->rows[pos.row]->entries.insert(
				map->rows[pos.row]->entries.end(),
				insert.rows[0]->entries.begin(),
				insert.rows[0]->entries.end()
			);

			// remove entries that have been moved
			map->rows[pos.row]->entries.erase(
				map->rows[pos.row]->entries.begin() + ins_idx,
				map->rows[pos.row]->entries.begin() + del_stop_idx
			);

		// operate on existing row
		} else if (insert.rows.size() == 1) {

			// operation is only done on one single line
			vector<ColMapSP> insert_row = insert.rows[0]->entries;
			// adjust the offset to account for inserted data
			for_each(insert_it, first_row_end, adjust_col(insert.getLastRowSize()));
			// then copy the complete first line to be inserted
			first_row.insert(insert_it, insert_row.begin(), insert_row.end());
			// restore the iterator after insertion
			insert_it = first_row.begin() + ins_idx;
			// adjust the offset for the inserts by insert position
			for_each(insert_it, insert_it + insert_row.size(), adjust_col(pos.col));

		} else {

			invalid_argument("insert.size.row is invalid");

		}

		// sync the row size stat variable
		// ToDo: use other way, too error prone
		// map->size.row = map->rows.size();

	}
	// EO insert

	// bread and butter function that implements all operations
	void SrcMapDoc::splice(SrcMapPos pos, SrcMapPos del, const Mappings& map)
	{

		remove(pos, del);
		insert(pos, map);

	}

	void SrcMapDoc::addSource(string file)
	{
		sources.push_back(file);
	}
	void SrcMapDoc::addToken(string token)
	{
		tokens.push_back(token);
	}

	// insert an entry at the given position
	void SrcMapDoc::insert(size_t row, ColMapSP entry, bool after)
	{
		// make sure we have enough room (needed for lookup)
		while (map->rows.size() <= row) { map->addNewLine(); }
		// create position object to pass as argument
		SrcMapPos pos = SrcMapPos(row, entry->getCol() + after);
		// get entries object from row to work on
		vector<ColMapSP> &entries = map->rows[pos.row]->entries;
		// find the position where the insert should be placed
		vector<ColMapSP>::iterator entry_it = entries.begin();
		vector<ColMapSP>::iterator entry_end = entries.end();
		// loop until we have found the position
		for(; entry_it != entry_end; ++entry_it)
		{ if ((*entry_it)->col >= pos.col) break; }
		// insert at end if no position found
		entries.insert(entry_it, entry);
	}





	void SrcMapDoc::setLastLineLength(size_t col)
	{
		map->last_ln_col = col;
	}


}
// EO namespace

// implement for c
extern "C"
{

}
