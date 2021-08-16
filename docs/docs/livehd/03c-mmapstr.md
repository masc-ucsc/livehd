# mmap_lib::str 

mmap_lib::str is the LiveHD string class. There are several reasons to have a custom string class:

* LiveHD uses lots of strings, and they are mmap persistent for speed reasons.
  The strings are backup in a mmap, since the mmap locations change at reload
and run-time there is no pointer stability.
* LiveHD string usage allows to optimize for common patters. E.g: append/remove
  beginning of the string is more frequent than the end. Also, the most common
operator is the comparator for hash map search.

## Class Structure

mmap_lib::str is a 16 byte string class with these main optimizations:

* SSO (Small String Optimization). When the string is less than 16 bytes, it
  fits in the mmap_lib::str, and no resource is allocated
* Unique pointer. When the string overflows, each string has a unique pointer.
  This requires a map search for each insert that overflows, but allows to just
compare the pointer to know if two strings are equal.
* mmap_lib::str is a persistent data structure, calls like append, return a new
  str with the change.

---

## API Usage

### Building strings

There are several constructors from std::string, std:string_view, int64_t.

```
mmap_lib::str direct_call("foo");       // == "foo"
auto string_direct = "string"_str;      // == "string"
auto str_for_const = mmap_lib::str(33); // == "33"
```

Append and prepend characters are also common:

* **`str append(char c) const`: Append character `c` to string
* **`str prepend(char c) const`: Prepend character `c` to string
* **`str concat(...)`: Concatenate std::string/std::string_view/integer/mmap_lib::str
* `str strip_leading(char c) const`: Remove all the leading occurances of character `c`

```
auto new_str = mmap_lib::str("hello").append('!');      // == "hello!"
auto prep    = mmap_lib::str("123").prepend('0');       // == "0123"
auto full    = mmap_lib::str::concat("hello", ".", 33); // == "hello.33"
```

###  Auxiliary/Helper Functions

* **`void dump const {}`: print string (useful in gdb)
* `constexpr std::size_t size() const`
* `constexpr std::size_t max_size() const`
* `constexpr bool empty() const`
* `std::string to_s() const`: convert to std::string
* `bool is_i() const {}`: Is integer?
* `int64_t to_i() const`: Convert to integer (fails if not integer)
* `std::vector<str> split(const char chr) const`: Split str in multiple strs

### Traversal/Search Functions

* **`bool starts_with(mmap_lib::str st) const`: True if string starts with `st`
* `bool ends_with(mmap_lib::str en) const`: True if string ends with `en`
* `std::size_t find(const str<m_id> &v, std::size_t pos=0) const {}`: Similar to std::string::find
* `std::size_t rfind(const str<m_id> &v, std::size_t pos=std::string::npos) const {}`: Reverse find
* `str substr(size_t start) const`: Similar to std::string::substr
* `str substr(size_t start, size_t end) const`: Similar to std::string::substr
* `str get_str_after_last(const char chr) const`:
* `str get_str_after_last_if_exists(const char chr) const`:
* `str get_str_after_first(const char chr) const`:
* `str get_str_after_first_if_exists(const char chr) const`:
* `str get_str_before_last(const char chr) const`:
* `str get_str_before_first(const char chr) const`:

