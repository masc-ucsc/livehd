#ifndef SOURCEMAP_SRCMAP
#define SOURCEMAP_SRCMAP

#ifdef __cplusplus

#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <fstream>

#include "json.hpp"
#include "sourcemap.hpp"

#include "map_line.hpp"
#include "map_col.hpp"
#include "mappings.hpp"

#ifndef VERSION
#define VERSION "[NA]"
#endif

// add namespace for c++
namespace SourceMap
{

	// define version from arguments
	// compile with g++ -DVERSION="\"vX.X.X\""
	const string SOURCEMAP_VERSION = VERSION;

	/****************************************************************************/
	/****************************************************************************/


	/****************************************************************************/
	/****************************************************************************/


	/****************************************************************************/
	/****************************************************************************/


	class SrcMapDoc PANDA_INHERIT
	{
		friend class ColMap;
		friend class LineMap;
		friend class Mappings;

		public: // ctor
			SrcMapDoc();
			SrcMapDoc(const string& json_str);
			SrcMapDoc(const JsonNode& json_node);
			virtual ~SrcMapDoc();

		public: // setters
			// append to the vectors
			void addToken(string token);
			void addSource(string file);
			// get index or add to vector
			size_t pushToken(string token);
			size_t pushSource(string file);

		public: // getters
			const string getFile() const;
			const string getRoot() const;
			const MappingsSP getMap() const;
			// access the vectors
			const string getToken(size_t idx) const;
			const string getSource(size_t idx) const;
			const string getContent(size_t idx) const;
			// get size of the vectors
			size_t getRowSize() const { return map->rows.size(); };
			size_t getTokenSize() const { return tokens.size(); };
			size_t getSourceSize() const { return sources.size(); };
			// use enc to disable map encoding
			char* serialize(bool enc = true) const;

		public: // methods
			// truncate or increase size
			// call before adding entries
			void truncate(SrcMapPos size);

			// insert the map at the given position
			// ToDo: means size needs to go on to map
			// sourcemaps first needs to adapt mappings
			void insert(SrcMapPos pos, const Mappings& map);
			void insert(SrcMapPos pos, const SrcMapDoc& srcmap);

			// delete the given size from position
			// will truncate the size accordingly
			// only possible because we now the number
			// of columns to be be removed from caller
			void remove(SrcMapPos pos, SrcMapPos del);

			// main manipulation method (delete/insert)
			// ToDo: means size needs to go on to map
			// sourcemaps first needs to adapt mappings
			void splice(SrcMapPos pos, SrcMapPos del, const Mappings& map);
			void splice(SrcMapPos pos, SrcMapPos del, const SrcMapDoc& srcmap);

			// remap is a specific operation to remap/merge an
			// intermediate source-map to its original content
			void remap(SrcMapDoc srcmap);


			// main manipulation method (delete/insert)


			void append(LineMap row);
			void prepend(LineMap row);

			void insert(size_t row, ColMapSP entry, bool after = false);

			void mergePrepare(SrcMapDoc srcmap);
			void setLastLineLength(size_t col);

		public: // route to other functions
			const ColMapSP getColMap(size_t row, size_t idx) const;
			const ColMapSP getColMap(const SrcMapIdx& idx) const;
			const ColMapArr at(size_t row, size_t col) const;
			const ColMapArr at(const SrcMapPos& pos) const;

		public: // variables
			string file;
			string root;
			MappingsSP map;
			string version;
			vector<string> tokens;
			vector<string> sources;
			vector<string> contents;
			void init(const JsonNode& json_node);
	};

	// typedef weak_ptr<SrcMapDoc> SrcMapWP;
	typedef shared_ptr<SrcMapDoc> SrcMapDocSP;

}
// EO namespace

// declare for c
extern "C" {
#endif

// void foobar();

#ifdef __cplusplus
}
#endif

#endif
