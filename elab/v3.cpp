// include library
#include <stdexcept>

#include "v3.hpp"

// add namespace for c++
namespace SourceMap
{
	namespace Format
	{
		namespace V3
		{

			// A Base64 VLQ digit can represent 5 bits, so it is base-32.
			const int VLQ_BASE_SHIFT = 5;
			const int VLQ_BASE = 1 << VLQ_BASE_SHIFT;

			// A mask of bits for a VLQ digit (11111), 31 decimal.
			const int VLQ_BASE_MASK = VLQ_BASE-1;

			// The continuation bit is the 6th bit.
			const int VLQ_CONTINUATION_BIT = VLQ_BASE;

			/**
			 * Converts from a two-complement value to a value where the sign bit is
			 * is placed in the least significant bit. For example, as decimals:
			 *   1 becomes 2 (10 binary), -1 becomes 3 (11 binary)
			 *   2 becomes 4 (100 binary), -2 becomes 5 (101 binary)
			 */
			int toVLQSigned(int value) {
				if (value < 0) {
					return ((-value) << 1) + 1;
				} else {
					return (value << 1) + 0;
				}
			}

			/**
			 * Converts to a two-complement value from a value where the sign bit is
			 * is placed in the least significant bit. For example, as decimals:
			 *   2 (10 binary) becomes 1, 3 (11 binary) becomes -1
			 *   4 (100 binary) becomes 2, 5 (101 binary) becomes -2
			 */
			int fromVLQSigned(int value) {
				int negate = (value & 1) == 1;
				value = value >> 1;
				return negate ? -value : value;
			}

			/**
			  * Coverts a interger value to base64 char
			*/
			char toBase64(int i) {
				if (i >= 0 && i < 26) { return char(i + 65); }
				else if (i > 25 && i < 52) { return char(i + 71); }
				else if (i > 51 && i < 62) { return char(i - 4); }
				else if (i == 62) { return char(43); }
				else if (i == 63) { return char(47); }
				throw(invalid_argument("base 64 integer out of bound"));
			}

			/**
			  * Coverts a char from base64 to integer value.
			*/
			int fromBase64(char c) {
				if (c > 95 && c < 123) { return c - 71; }
				else if (c > 64 && c < 91) { return c - 65; }
				else if (c > 47 && c < 59) { return c + 4; }
				else if (c == 43) { return 62; }
				else if (c == 47) { return 63; }
				throw(invalid_argument("base 64 char out of bound"));
			}

			/**
			 * Writes a VLQ encoded value to the provide appendable.
			 * @throws IOException
			 */
			string encodeVLQ(int value) {
				string result = "";
				value = toVLQSigned(value);
				do {
					int digit = value & VLQ_BASE_MASK;
					value = (unsigned int)value >> VLQ_BASE_SHIFT;
					if (value > 0) {
						digit |= VLQ_CONTINUATION_BIT;
					}
					result += toBase64(digit);
				} while (value > 0);
				return result;
			}

			/**
			 * Decodes the next VLQValue from the provided iterator.
			 */
			int decodeVLQ(string::const_iterator& it, string::const_iterator& end) {
				int result = 0;
				int continuation;
				int shift = 0;
				do {
					char c = *it;
					int digit = fromBase64(c);
					continuation = (digit & VLQ_CONTINUATION_BIT) != 0;
					digit &= VLQ_BASE_MASK;
					result = result + (digit << shift);
					shift = shift + VLQ_BASE_SHIFT;
					if (continuation) ++it;
				} while (continuation && it != end);
				return fromVLQSigned(result);
			}

			const string serialize(const Mappings& map)
			{

				string result = "";

				size_t column = 0;
				size_t source = 0;
				size_t in_line = 0;
				size_t in_col = 0;
				size_t token = 0;

				for(size_t i = 0; i < map.getRowCount(); ++i) {
					LineMap* row = map.rows[i].get();
					for(size_t n = 0; n < row->getEntryCount(); ++n) {
						const ColMap* entry = row->getColMap(n).get();
						if (entry->getType() == 1) {
							result += encodeVLQ(entry->getCol() - column);
							source = entry->getCol();
						} else if (entry->getType() == 4) {
							result += encodeVLQ(entry->getCol() - column);
							result += encodeVLQ(entry->getSource() - source);
							result += encodeVLQ(entry->getSrcLine() - in_line);
							result += encodeVLQ(entry->getSrcCol() - in_col);
							column = entry->getCol();
							source = entry->getSource();
							in_line = entry->getSrcLine();
							in_col = entry->getSrcCol();
						} else if (entry->getType() == 5) {
							result += encodeVLQ(entry->getCol() - column);
							result += encodeVLQ(entry->getSource() - source);
							result += encodeVLQ(entry->getSrcLine() - in_line);
							result += encodeVLQ(entry->getSrcCol() - in_col);
							result += encodeVLQ(entry->getToken() - token);
							column = entry->getCol();
							source = entry->getSource();
							in_line = entry->getSrcLine();
							in_col = entry->getSrcCol();
							token = entry->getToken();
						}
						if (n + 1 != row->getEntryCount()) result += ",";
					}
					if (i + 1 != map.getRowCount()) result += ";";

				column = 0; }

				return result;

			}

			void unserialize(Mappings& map, const string& data)
			{

				string::const_iterator it = data.begin();
				string::const_iterator end = data.end();

				LineMapSP row = map.rows.back();

				size_t column = 0;
				size_t source = 0;
				size_t in_line = 0;
				size_t in_col = 0;
				size_t token = 0;

				vector<int> offset;
				while(true) {

					// check if we reached a delimiter for entries
					if (it == end || *it == ',' || *it == ';') {

						// only create valid entries
						// checks out of bound access
						if (
							offset.size() == 1 &&
							int(offset[0] + column) >= 0
						) {
							row->addEntry(SourceMap::make_shared<ColMap>(
								column += offset[0]
							));
						}
						else if (
							offset.size() == 4 &&
							int(offset[0] + column) >= 0 &&
							int(offset[1] + source) >= 0 &&
							int(offset[2] + in_line) >= 0 &&
							int(offset[3] + in_col) >= 0
						) {
							row->addEntry(SourceMap::make_shared<ColMap>(
								column += offset[0],
								source += offset[1],
								in_line += offset[2],
								in_col += offset[3]
							));
						}
						else if (
							offset.size() == 5 &&
							int(offset[0] + column) >= 0 &&
							int(offset[1] + source) >= 0 &&
							int(offset[2] + in_line) >= 0 &&
							int(offset[3] + in_col) >= 0 &&
							int(offset[4] + token) >= 0
						) {
							row->addEntry(SourceMap::make_shared<ColMap>(
								column += offset[0],
								source += offset[1],
								in_line += offset[2],
								in_col += offset[3],
								token += offset[4]
							));
						}
						// empty rows are allowed
						else if (offset.size() != 0) {
							// everything else is not valid
							throw(runtime_error("invalid source map entry"));
						}

						// create a new row
						if (*it == ';') {
							map.addNewLine();
							row = map.rows.back();
						column = 0; }

						// clear for next
						offset.clear();

						// exit loop if at end
						if (it == end) break;

					} else {

						// parse a vlq number and put to offset
						offset.push_back(decodeVLQ(it, end));

					}

					// advance
					++it;

				}

			}

		}
	}
}
// EO namespace
